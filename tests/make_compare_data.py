#!/usr/bin/env python3
"""Generate QQQ_fake_long.csv: a comparison series on the exact same
window_start grid as SPY_fake_long.csv (different ticker, price level
and walk), for testing the StockChart comparative plot."""
import csv
import random
import sys

src_path = sys.argv[1] if len(sys.argv) > 1 else "tests/data/SPY_fake_long.csv"
dst_path = sys.argv[2] if len(sys.argv) > 2 else "tests/data/QQQ_fake_long.csv"

with open(src_path) as f:
    rows = list(csv.DictReader(f))

random.seed(424242)
price = 512.0
out = []
for r in rows:
    o = price
    c = price + (random.random() - 0.475) * 2.6
    h = max(o, c) + random.random() * 0.7
    lo = min(o, c) - random.random() * 0.7
    vol = int(3000 + random.random() * 30000)
    out.append(
        {
            "ticker": "QQQ",
            "volume": vol,
            "open": f"{o:.2f}",
            "close": f"{c:.2f}",
            "high": f"{h:.2f}",
            "low": f"{lo:.2f}",
            "window_start": r["window_start"],
            "transactions": int(vol / 7),
        }
    )
    price = c

with open(dst_path, "w", newline="") as f:
    w = csv.DictWriter(
        f,
        fieldnames=[
            "ticker",
            "volume",
            "open",
            "close",
            "high",
            "low",
            "window_start",
            "transactions",
        ],
    )
    w.writeheader()
    w.writerows(out)
print(f"wrote {len(out)} QQQ bars to {dst_path}")
