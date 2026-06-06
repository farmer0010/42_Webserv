#!/usr/bin/env python3
import os
import json
import sys

sys.stdout.write("Content-Type: application/json\r\n\r\n")

UPLOAD_DIR = "www/uploads"

try:
    if not os.path.isdir(UPLOAD_DIR):
        sys.stdout.write(json.dumps({"files": []}))
    else:
        files = []
        for name in sorted(os.listdir(UPLOAD_DIR)):
            if name.startswith('.'):
                continue
            path = os.path.join(UPLOAD_DIR, name)
            if os.path.isfile(path):
                files.append({
                    "name": name,
                    "size": os.path.getsize(path)
                })
        sys.stdout.write(json.dumps({"files": files}))
except Exception as e:
    sys.stdout.write(json.dumps({"error": str(e), "files": []}))
