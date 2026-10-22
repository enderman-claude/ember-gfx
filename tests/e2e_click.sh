#!/bin/sh
# End-to-end: runs the Ember click program on the current X display, sends pointer
# clicks to it, and checks the result on screen. Usage: e2e_click.sh APP INJECTOR
set -u
APP=$1
INJECTOR=$2
"$APP" &
PID=$!
"$INJECTOR"
STATUS=$?
kill "$PID" 2>/dev/null
wait "$PID" 2>/dev/null
exit "$STATUS"
