#!/usr/bin/env bash
set -u
cd "$(dirname "$0")/.."

PORT=8080
BASE="http://127.0.0.1:${PORT}"

apt-get update -qq >/dev/null
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
    build-essential python3 php-cli curl procps >/dev/null

make -j"$(nproc)" >/tmp/make.log 2>&1 || { tail -40 /tmp/make.log; exit 1; }

./webserv conf/default.conf >/tmp/server.log 2>&1 &
SPID=$!
sleep 1

hr() { printf '\n──── %s ────\n' "$1"; }
req() {
    local title="$1"; shift
    hr "$title"
    curl -sS -o /tmp/body -w "status: %{http_code}  ct: %{content_type}  bytes: %{size_download}\n" "$@"
    head -c 600 /tmp/body; echo
}

req "GET  /cgi-bin/hello.py"        "$BASE/cgi-bin/hello.py"
req "GET  /cgi-bin/env.py"          "$BASE/cgi-bin/env.py" | head -25
req "GET  /cgi-bin/echo.py?x=1&y=z" "$BASE/cgi-bin/echo.py?x=1&y=z"
req "POST /cgi-bin/echo.py body"    -X POST -H "Content-Type: text/plain" --data-binary "hello body" "$BASE/cgi-bin/echo.py"
req "GET  /cgi-bin/sleep.py?2"      "$BASE/cgi-bin/sleep.py?2"
req "GET  /cgi-bin/fail.py (→ 500)" "$BASE/cgi-bin/fail.py"
req "GET  /cgi-bin/info.php"        "$BASE/cgi-bin/info.php"
req "POST /cgi-bin/echo.php body"   -X POST -H "Content-Type: application/json" --data-binary '{"name":"taewonki"}' "$BASE/cgi-bin/echo.php"
req "GET  /cgi-bin/missing.py (→ 404)" "$BASE/cgi-bin/missing.py"

echo
echo "-- server log (tail) --"
tail -20 /tmp/server.log

kill -INT $SPID 2>/dev/null || true
sleep 1
kill -TERM $SPID 2>/dev/null || true
wait $SPID 2>/dev/null || true
echo "Done."
