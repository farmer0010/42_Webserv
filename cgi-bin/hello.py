#!/usr/bin/env python3
# 42_Webserv CGI 동작 확인용 스크립트
# - GET: 환경 변수 일부와 QUERY_STRING 출력
# - POST: stdin 으로 받은 본문 전체를 echo

import os
import sys


def header(text):
    return (
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "\r\n"
        + text
    )


def html(body):
    return (
        "<!DOCTYPE html><html lang='ko'><head><meta charset='UTF-8'>"
        "<title>CGI</title>"
        "<style>body{font-family:-apple-system,system-ui,sans-serif;"
        "max-width:720px;margin:40px auto;padding:0 20px;color:#222}"
        "pre{background:#f4f4f4;padding:12px;border-radius:4px;overflow-x:auto}"
        "h1{border-bottom:2px solid #222;padding-bottom:8px}"
        "a{color:#222}</style></head><body>"
        + body
        + "<p><a href='/'>← 홈으로</a></p></body></html>"
    )


method = os.environ.get("REQUEST_METHOD", "GET")
shown_env = {
    k: os.environ.get(k, "")
    for k in (
        "REQUEST_METHOD",
        "QUERY_STRING",
        "CONTENT_LENGTH",
        "CONTENT_TYPE",
        "SCRIPT_NAME",
        "PATH_INFO",
        "SERVER_NAME",
        "SERVER_PORT",
    )
}

env_block = "<h2>CGI 환경 변수</h2><pre>" + "\n".join(
    "{}={}".format(k, v) for k, v in shown_env.items()
) + "</pre>"

if method == "POST":
    try:
        length = int(os.environ.get("CONTENT_LENGTH", "0") or "0")
    except ValueError:
        length = 0
    body = sys.stdin.buffer.read(length).decode("utf-8", errors="replace") if length > 0 else ""
    body_block = "<h2>요청 본문 ({} bytes)</h2><pre>{}</pre>".format(length, body)
else:
    body_block = "<h2>GET 요청</h2><p class='muted'>QUERY_STRING 또는 폼으로 POST 해 보세요.</p>"

sys.stdout.write(header(html("<h1>Hello from CGI</h1>" + env_block + body_block)))
