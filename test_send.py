import json
import os
import urllib.request

cfg = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json")))
url = f"http://localhost:{cfg['port']}/api/checkout"

import time
cart = "TEST-" + str(int(time.time()))
body = {"cart_id": cart, "trolley_id": "T01", "lines": [
    {"uid": "A9217106", "qty": 1},
    {"uid": "03432B07", "qty": 4},
    {"uid": "AD8FB302", "qty": 2}]}

req = urllib.request.Request(url, data=json.dumps(body).encode(), method="POST",
      headers={"Content-Type": "application/json", "X-Device-Token": cfg["device_token"]})
for attempt in (1, 2):   # second send tests duplicate protection
    with urllib.request.urlopen(req) as r:
        print(attempt, r.status, r.read().decode())
