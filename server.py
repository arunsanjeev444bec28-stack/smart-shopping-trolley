import hmac
import json
import os
import secrets
import sqlite3
import base64
import html
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import db

BASE = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(BASE, "config.json")


def load_config():
    if not os.path.exists(CONFIG_PATH):
        cfg = {
            "store_name": "SMART TROLLEY STORE",
            "port": 8000,
            "dashboard_user": "admin",
            "dashboard_password": secrets.token_urlsafe(9),
            "device_token": secrets.token_urlsafe(24),
        }
        with open(CONFIG_PATH, "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=2)
        print("config.json created. Open it to see your login and device token.")
    with open(CONFIG_PATH, encoding="utf-8") as f:
        return json.load(f)


CFG = load_config()


def esc(x):
    return html.escape(str(x))


def money(x):
    return f"{float(x):,.2f}"


# ---------------- business logic ----------------
def save_checkout(data):
    """Validate the cart, price it from the products table, save the bill.
    Returns (http_status, dict)."""
    cart_id = str(data.get("cart_id", "")).strip()
    trolley_id = str(data.get("trolley_id", "")).strip()[:20]
    lines = data.get("lines")
    if not cart_id or len(cart_id) > 40:
        return 400, {"error": "invalid cart_id"}
    if not isinstance(lines, list) or not lines:
        return 400, {"error": "cart has no items"}

    cn = db.connect()
    try:
        row = cn.execute("SELECT bill_id, bill_no, total_items, total_amount "
                         "FROM bills WHERE cart_id=?", (cart_id,)).fetchone()
        if row:   # retry of an already saved checkout
            return 200, {"status": "already_saved", "bill_id": row["bill_id"],
                         "bill_no": row["bill_no"], "items": row["total_items"],
                         "total": row["total_amount"]}

        merged, unknown = {}, []
        for ln in lines:
            try:
                uid = str(ln["uid"]).strip().upper()
                qty = int(ln["qty"])
            except (KeyError, TypeError, ValueError):
                return 400, {"error": "bad line format"}
            if qty < 1 or qty > 1000:
                return 400, {"error": f"bad quantity for {uid}"}
            merged[uid] = merged.get(uid, 0) + qty

        priced = []
        for uid, qty in merged.items():
            p = cn.execute("SELECT item_id, item_name, price FROM products "
                           "WHERE rfid_uid=? AND active=1", (uid,)).fetchone()
            if not p:
                unknown.append(uid)
                continue
            priced.append((p["item_id"], p["item_name"], qty, p["price"],
                           p["price"] * qty))
        if unknown:
            return 422, {"error": "unknown product tags", "uids": unknown}

        items = sum(x[2] for x in priced)
        total = sum(x[4] for x in priced)
        try:
            cur = cn.execute(
                "INSERT INTO bills (cart_id, trolley_id, total_items, total_amount) "
                "VALUES (?,?,?,?)", (cart_id, trolley_id, items, total))
            bill_id = cur.lastrowid
            bill_no = f"B{bill_id:06d}"
            cn.execute("UPDATE bills SET bill_no=? WHERE bill_id=?", (bill_no, bill_id))
            cn.executemany(
                "INSERT INTO bill_items (bill_id,item_id,item_name,quantity,price,subtotal) "
                "VALUES (?,?,?,?,?,?)",
                [(bill_id, a, b, c, d, e) for a, b, c, d, e in priced])
            cn.commit()
        except sqlite3.IntegrityError:
            cn.rollback()
            row = cn.execute("SELECT bill_id, bill_no, total_items, total_amount "
                             "FROM bills WHERE cart_id=?", (cart_id,)).fetchone()
            if row:
                return 200, {"status": "already_saved", "bill_id": row["bill_id"],
                             "bill_no": row["bill_no"], "items": row["total_items"],
                             "total": row["total_amount"]}
            raise
        print(f"[{datetime.now():%H:%M:%S}] BILL {bill_no} | {trolley_id} | "
              f"{items} items | Rs {total:.2f}")
        return 201, {"status": "saved", "bill_id": bill_id, "bill_no": bill_no,
                     "items": items, "total": total}
    finally:
        cn.close()


# ---------------- pages ----------------
STYLE = """
body{font-family:Segoe UI,Arial,sans-serif;margin:0;background:#f4f6f8;color:#1b1f23}
header{background:#1f3a5f;color:#fff;padding:14px 24px;font-size:20px;font-weight:600}
main{max-width:980px;margin:20px auto;padding:0 16px}
.cards{display:flex;gap:14px;flex-wrap:wrap;margin-bottom:18px}
.card{background:#fff;border-radius:8px;padding:14px 20px;box-shadow:0 1px 3px #0002;min-width:170px}
.card b{display:block;font-size:24px;margin-top:4px}
table{width:100%;border-collapse:collapse;background:#fff;border-radius:8px;overflow:hidden;box-shadow:0 1px 3px #0002}
th,td{padding:10px 12px;border-bottom:1px solid #e5e8eb;text-align:left;font-size:14px}
th{background:#eaeff5}
.r{text-align:right}
a.btn,button{background:#1f3a5f;color:#fff;border:0;padding:6px 12px;border-radius:5px;text-decoration:none;cursor:pointer;font-size:13px}
.tag{background:#fff3cd;color:#7a5b00;padding:2px 8px;border-radius:10px;font-size:12px}
.muted{color:#666;font-size:12px}
"""


def page_dashboard():
    cn = db.connect()
    bills = cn.execute("SELECT * FROM bills ORDER BY bill_id DESC LIMIT 100").fetchall()
    today = cn.execute(
        "SELECT COUNT(*) c, COALESCE(SUM(total_items),0) i, COALESCE(SUM(total_amount),0) t "
        "FROM bills WHERE date(created_at)=date('now','localtime')").fetchone()
    cn.close()
    rows = ""
    for b in bills:
        d, t = b["created_at"].split(" ")
        rows += (f"<tr><td>{esc(b['bill_no'])}</td><td>{esc(d)}</td><td>{esc(t)}</td>"
                 f"<td>{esc(b['trolley_id'])}</td><td class=r>{b['total_items']}</td>"
                 f"<td class=r>&#8377;{money(b['total_amount'])}</td>"
                 f"<td><span class=tag>{esc(b['payment_status'])}</span></td>"
                 f"<td><a class=btn href='/bill/{b['bill_id']}'>View / Print</a></td></tr>")
    if not rows:
        rows = "<tr><td colspan=8 class=muted>No bills yet.</td></tr>"
    return f"""<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<meta http-equiv=refresh content=10><title>Trolley Dashboard</title>
<style>{STYLE}</style></head><body>
<header>{esc(CFG['store_name'])} &mdash; Dashboard</header><main>
<div class=cards>
<div class=card>Bills today<b>{today['c']}</b></div>
<div class=card>Items today<b>{today['i']}</b></div>
<div class=card>Billed amount today<b>&#8377;{money(today['t'])}</b></div></div>
<table><tr><th>Bill No</th><th>Date</th><th>Time</th><th>Trolley</th>
<th class=r>Items</th><th class=r>Amount</th><th>Payment</th><th></th></tr>{rows}</table>
<p class=muted>Page refreshes every 10 seconds. "UNVERIFIED" means the amount is billed but
payment has not been confirmed by this system. Check it in your UPI app.</p>
</main></body></html>"""


def page_bill(bill_id):
    cn = db.connect()
    b = cn.execute("SELECT * FROM bills WHERE bill_id=?", (bill_id,)).fetchone()
    items = cn.execute("SELECT * FROM bill_items WHERE bill_id=? ORDER BY id",
                       (bill_id,)).fetchall() if b else []
    cn.close()
    if not b:
        return None
    d, t = b["created_at"].split(" ")
    rows = "".join(
        f"<tr><td>{esc(i['item_id'])}</td><td>{esc(i['item_name'])}</td>"
        f"<td class=r>{i['quantity']}</td><td class=r>{money(i['price'])}</td>"
        f"<td class=r>{money(i['subtotal'])}</td></tr>" for i in items)
    return f"""<!doctype html><html><head><meta charset=utf-8>
<title>Bill {esc(b['bill_no'])}</title>
<style>{STYLE}
main{{max-width:560px;background:#fff;padding:20px 24px;border-radius:8px;box-shadow:0 1px 3px #0002}}
h1{{text-align:center;margin:0;font-size:20px}} h2{{text-align:center;margin:4px 0 14px;font-size:14px;color:#555}}
.meta{{display:flex;justify-content:space-between;font-size:14px;margin-bottom:10px}}
.total{{text-align:right;font-size:18px;font-weight:700;margin-top:12px}}
@media print{{body{{background:#fff}}main{{box-shadow:none}}.noprint{{display:none}}}}
</style></head><body><main>
<h1>{esc(CFG['store_name'])}</h1><h2>SHOPPING BILL</h2>
<div class=meta><div>Bill No: <b>{esc(b['bill_no'])}</b><br>Trolley: {esc(b['trolley_id'])}</div>
<div style="text-align:right">Date: {esc(d)}<br>Time: {esc(t)}</div></div>
<table><tr><th>Item ID</th><th>Item Name</th><th class=r>Qty</th><th class=r>Price</th><th class=r>Amount</th></tr>
{rows}</table>
<div class=total>Total items: {b['total_items']} &nbsp;|&nbsp; Total: &#8377;{money(b['total_amount'])}</div>
<p class=muted style="text-align:center">Pay using the UPI QR shown on the trolley.<br>
Payment is confirmed in the shop's UPI app, not by this bill.<br>Thank you for shopping!</p>
<p class=noprint style="text-align:center"><button onclick="window.print()">Print</button>
&nbsp;<a class=btn href="/">Back</a></p>
</main></body></html>"""


# ---------------- HTTP ----------------
class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def send(self, code, body, ctype="text/html; charset=utf-8", headers=None):
        raw = body if isinstance(body, bytes) else body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(raw)))
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(raw)

    def send_json(self, code, obj):
        self.send(code, json.dumps(obj), "application/json")

    def dashboard_ok(self):
        h = self.headers.get("Authorization", "")
        if h.startswith("Basic "):
            try:
                u, p = base64.b64decode(h[6:]).decode("utf-8").split(":", 1)
                if (hmac.compare_digest(u, CFG["dashboard_user"]) and
                        hmac.compare_digest(p, CFG["dashboard_password"])):
                    return True
            except Exception:
                pass
        self.send(401, "Login required",
                  headers={"WWW-Authenticate": 'Basic realm="Trolley Dashboard"'})
        return False

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/api/health":
            return self.send_json(200, {"status": "ok"})
        if not self.dashboard_ok():
            return
        if path == "/":
            return self.send(200, page_dashboard())
        if path.startswith("/bill/"):
            try:
                page = page_bill(int(path[6:]))
            except ValueError:
                page = None
            if page:
                return self.send(200, page)
        self.send(404, "Not found")

    def do_POST(self):
        if self.path.split("?")[0] != "/api/checkout":
            return self.send(404, "Not found")
        tok = self.headers.get("X-Device-Token", "")
        if not hmac.compare_digest(tok, CFG["device_token"]):
            return self.send_json(401, {"error": "bad device token"})
        try:
            n = int(self.headers.get("Content-Length", "0"))
            if n <= 0 or n > 20000:
                return self.send_json(400, {"error": "bad body size"})
            data = json.loads(self.rfile.read(n).decode("utf-8"))
            if not isinstance(data, dict):
                raise ValueError
        except Exception:
            return self.send_json(400, {"error": "invalid JSON"})
        try:
            code, obj = save_checkout(data)
        except Exception as e:
            print("Server error:", e)
            return self.send_json(500, {"error": "server error, retry"})
        self.send_json(code, obj)


if __name__ == "__main__":
    db.init_db()
    port = int(CFG["port"])
    print(f"Dashboard : http://localhost:{port}")
    print("Login     : see config.json (dashboard_user / dashboard_password)")
    print("Press Ctrl+C to stop.")
    ThreadingHTTPServer(("0.0.0.0", port), Handler).serve_forever()
