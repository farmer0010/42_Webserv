#!/usr/bin/env python3

import sys

sys.stdout.write(
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=UTF-8\r\n"
    "\r\n"
    "<!DOCTYPE html><html lang='ko'><head><meta charset='UTF-8'>"
    "<title>Hello</title>"
    "<style>"
    "body{display:flex;justify-content:center;align-items:center;"
    "height:100vh;margin:0;font-family:-apple-system,system-ui,sans-serif;}"
    "h1{font-size:6rem;color:#222;}"
    "</style></head><body>"
    "<h1>Hello</h1>"
    "</body></html>"
)
