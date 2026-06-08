#!/usr/bin/env bash
set -u
cd "$(dirname "$0")/.."

PORT=8080
HOST=127.0.0.1
BASE="http://${HOST}:${PORT}"

apt-get update -qq >/dev/null
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
    build-essential valgrind siege curl python3 procps >/dev/null

make -j"$(nproc)" >/tmp/make.log 2>&1 || { tail -40 /tmp/make.log; exit 1; }

echo "================================================================"
echo "Stage 1 — valgrind (memcheck + fd tracking) with mixed traffic"
echo "================================================================"
rm -f /tmp/valgrind.log /tmp/server.log
valgrind --leak-check=full --show-leak-kinds=all --track-fds=yes \
         --errors-for-leak-kinds=definite --error-exitcode=42 \
         --log-file=/tmp/valgrind.log \
         ./webserv conf/default.conf >/tmp/server.log 2>&1 &
VPID=$!
sleep 3

echo "-- warmup --"
curl -s -o /dev/null -w "GET /            -> %{http_code}\n" $BASE/
curl -s -o /dev/null -w "GET /index.html  -> %{http_code}\n" $BASE/index.html
curl -s -o /dev/null -w "GET /nope        -> %{http_code}\n" $BASE/nope
curl -s -o /dev/null -w "POST /uploads    -> %{http_code}\n" \
    -X POST --data-binary "hello world" $BASE/uploads
curl -s -o /dev/null -w "DELETE /uploads  -> %{http_code}\n" -X DELETE $BASE/uploads/nope
curl -s -o /dev/null -w "BAD method PUT   -> %{http_code}\n" -X PUT $BASE/

echo "-- light siege (10s, c=10) to exercise keep-alive / many fds --"
siege -q -b -c 10 -t 10S $BASE/ >/tmp/siege_vg.log 2>&1 || true
tail -15 /tmp/siege_vg.log

echo "-- stopping server (SIGINT) --"
kill -INT $VPID 2>/dev/null || true
for i in 1 2 3 4 5 6 7 8 9 10; do
    kill -0 $VPID 2>/dev/null || break
    sleep 1
done
if kill -0 $VPID 2>/dev/null; then
    echo "(server didn't exit on SIGINT — sending SIGTERM)"
    kill -TERM $VPID 2>/dev/null || true
    sleep 2
fi
wait $VPID 2>/dev/null || true

echo
echo "---- valgrind heap summary ----"
grep -E "in use at exit|total heap usage|definitely lost|indirectly lost|possibly lost|still reachable|suppressed" /tmp/valgrind.log
echo
echo "---- valgrind FILE DESCRIPTORS ----"
awk '/FILE DESCRIPTORS/{flag=1} flag{print} /ERROR SUMMARY/{flag=0}' /tmp/valgrind.log | head -80
echo
echo "---- valgrind ERROR SUMMARY ----"
grep -E "ERROR SUMMARY" /tmp/valgrind.log

echo
echo "================================================================"
echo "Stage 2 — siege benchmark against unwrapped server"
echo "================================================================"
./webserv conf/default.conf >/tmp/server2.log 2>&1 &
SPID=$!
sleep 1
curl -s -o /dev/null -w "warmup -> %{http_code}\n" $BASE/

echo "-- siege 30s, c=25 --"
siege -b -c 25 -t 30S $BASE/ 2>&1 | tail -25

kill -INT $SPID 2>/dev/null || true
sleep 1
kill -TERM $SPID 2>/dev/null || true
wait $SPID 2>/dev/null || true

echo
echo "Done."
