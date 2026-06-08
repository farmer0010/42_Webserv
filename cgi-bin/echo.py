#!/usr/bin/env python3
import os
import sys
import json

try:
    cl = int(os.environ.get("CONTENT_LENGTH", "0") or 0)
except ValueError:
    cl = 0

body = b""
if cl > 0:
    body = sys.stdin.buffer.read(cl)

payload = {
    "engine": "python3",
    "method": os.environ.get("REQUEST_METHOD", ""),
    "uri": os.environ.get("REQUEST_URI", ""),
    "path_info": os.environ.get("PATH_INFO", ""),
    "query": os.environ.get("QUERY_STRING", ""),
    "content_type": os.environ.get("CONTENT_TYPE", ""),
    "content_length": cl,
    "server_protocol": os.environ.get("SERVER_PROTOCOL", ""),
    "server_name": os.environ.get("SERVER_NAME", ""),
    "server_port": os.environ.get("SERVER_PORT", ""),
    "host": os.environ.get("HTTP_HOST", ""),
    "body": body.decode("utf-8", errors="replace"),
}

sys.stdout.write("Content-Type: application/json\r\n\r\n")
sys.stdout.write(json.dumps(payload, indent=2, ensure_ascii=False))
sys.stdout.write("\n")
