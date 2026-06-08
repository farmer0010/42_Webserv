#!/usr/bin/env python3
import os
import sys
import html
import urllib.parse

method = os.environ.get("REQUEST_METHOD", "GET")

msg = ""
if method == "POST":
    try:
        cl = int(os.environ.get("CONTENT_LENGTH", "0") or 0)
    except ValueError:
        cl = 0
    raw = sys.stdin.buffer.read(cl).decode("utf-8", errors="replace") if cl > 0 else ""
    form = urllib.parse.parse_qs(raw, keep_blank_values=True)
    msg = (form.get("msg") or [""])[0].strip()

safe_msg = html.escape(msg)

if safe_msg:
    body = (
        f"<h1>{safe_msg}</h1>"
        f"<p><a href='/'>홈으로</a></p>"
    )
else:
    body = (
        "<h1>Hello</h1>"
        "<form method='POST' action='/cgi-bin/hello.py'>"
        "<input name='msg' placeholder='메시지를 입력하세요' autofocus>"
        "<button type='submit'>인사</button>"
        "</form>"
    )

sys.stdout.write("Content-Type: text/html; charset=UTF-8\r\n\r\n")
sys.stdout.write(
    "<!DOCTYPE html><html lang='ko'><head><meta charset='UTF-8'>"
    "<title>Hello</title>"
    "<style>"
    "body{display:flex;justify-content:center;align-items:center;"
    "height:100vh;margin:0;font-family:-apple-system,system-ui,sans-serif;}"
    ".box{text-align:center;}"
    "h1{font-size:5rem;color:#222;margin:0 0 1rem;}"
    "form{display:flex;gap:.5rem;justify-content:center;}"
    "input,button{font-size:1.1rem;padding:.5rem .8rem;"
    "border:1px solid #ccc;border-radius:6px;}"
    "button{background:#222;color:#fff;cursor:pointer;border-color:#222;}"
    "a{color:#666;text-decoration:none;}"
    "a:hover{text-decoration:underline;}"
    "</style></head><body>"
    f"<div class='box'>{body}</div>"
    "</body></html>"
)
