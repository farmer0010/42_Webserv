#!/usr/bin/env python3
import os
import sys
import time

try:
    sec = int(os.environ.get("QUERY_STRING", "2") or 2)
except ValueError:
    sec = 2
sec = max(0, min(sec, 5))

time.sleep(sec)

sys.stdout.write("Content-Type: text/plain\r\n\r\n")
sys.stdout.write("slept {}s\n".format(sec))
