#!/usr/bin/env python3
import os
import sys

sys.stdout.write("Content-Type: text/plain; charset=UTF-8\r\n\r\n")
sys.stdout.write("=== CGI environment ===\n")
for k in sorted(os.environ):
    sys.stdout.write("{}={}\n".format(k, os.environ[k]))
