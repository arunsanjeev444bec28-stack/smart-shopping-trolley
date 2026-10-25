import sqlite3
import os

DB_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "trolley.db")

SCHEMA = """
CREATE TABLE IF NOT EXISTS products (
  item_id   TEXT PRIMARY KEY,
  rfid_uid  TEXT NOT NULL UNIQUE,
  item_name TEXT NOT NULL,
  price     REAL NOT NULL,
  active    INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE IF NOT EXISTS bills (
  bill_id        INTEGER PRIMARY KEY AUTOINCREMENT,
  bill_no        TEXT UNIQUE,
  cart_id        TEXT NOT NULL UNIQUE,
  trolley_id     TEXT,
  created_at     TEXT NOT NULL DEFAULT (datetime('now','localtime')),
  total_items    INTEGER NOT NULL,
  total_amount   REAL NOT NULL,
  payment_status TEXT NOT NULL DEFAULT 'UNVERIFIED'
);

CREATE TABLE IF NOT EXISTS bill_items (
  id        INTEGER PRIMARY KEY AUTOINCREMENT,
  bill_id   INTEGER NOT NULL REFERENCES bills(bill_id) ON DELETE CASCADE,
  item_id   TEXT NOT NULL,
  item_name TEXT NOT NULL,
  quantity  INTEGER NOT NULL,
  price     REAL NOT NULL,
  subtotal  REAL NOT NULL
);
"""

SEED = [
    ("P01", "03432B07", "MARIGOLD BISCUIT", 10),
    ("P02", "BA62CA01", "GOODDAY BISCUIT",  10),
    ("P03", "B69D4E06", "MILKBIKIS",        10),
    ("P04", "A9217106", "RICE 100G",        30),
    ("P05", "AD8FB302", "URAD DHAL 100G",   50),
    ("P06", "A6C21407", "WHEAT 100G",       25),
]


def connect():
    cn = sqlite3.connect(DB_PATH, timeout=10)
    cn.row_factory = sqlite3.Row
    cn.execute("PRAGMA foreign_keys = ON")
    return cn


def init_db():
    cn = connect()
    cn.executescript(SCHEMA)
    cn.executemany(
        "INSERT OR IGNORE INTO products (item_id, rfid_uid, item_name, price) VALUES (?,?,?,?)",
        SEED)
    cn.commit()
    cn.close()


if __name__ == "__main__":
    init_db()
    cn = connect()
    print("Database file:", DB_PATH)
    print("Products:")
    for r in cn.execute("SELECT item_id, rfid_uid, item_name, price FROM products ORDER BY item_id"):
        print(f"  {r['item_id']}  {r['rfid_uid']}  {r['item_name']:<18} Rs {r['price']:.0f}")
    cn.close()
