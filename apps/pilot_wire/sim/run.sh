#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# run.sh <zephyr.exe> [test.py]: pilot_wire on native_sim against a bumble
# central from sim/ (default central.py; shell_test.py needs overlay-test).
# Two bumble virtual controllers share one link: the app gets the first over
# TCP (--bt-dev), central.py drives the second. BUMBLE_PY: python with bumble.
PY=${BUMBLE_PY:-python3}
EXE=$(realpath $1)
DIR=$(dirname $(realpath $0))
cd $(mktemp -d)
$PY -m bumble.apps.controllers tcp-server:127.0.0.1:9000 tcp-server:127.0.0.1:9001 > ctrl.log 2>&1 &
until ss -ltn | grep -q :9001; do sleep 0.2; done
$EXE --bt-dev=127.0.0.1:9000 > zephyr.log 2>&1 &
sleep 2
timeout 60 $PY $DIR/${2:-central.py} tcp-client:127.0.0.1:9001
RC=$?
sleep 1; kill %1 %2 2>/dev/null; wait 2>/dev/null
echo "== app console ($PWD)"; cat zephyr.log
exit $RC
