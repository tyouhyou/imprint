#!/bin/bash
# The studio client's pty smoke (P1-10, the tianji smoke_pty precedent):
# the SIXEL exe under a real pty, driven with a keyboard script, against
# the stdlib-only mock bridge (no tianji repo needed). The round-trip
# assertion reads the mock's seen-request marker (STUDIO_MOCK_SEEN) —
# frame counts alone are a weak signal here (every fp-poll repaints in
# the mock, since it never answers "unchanged"):
#   1. the shell survives the whole session (no crash, exit via timeout)
#   2. the typed speak REACHED the bridge (a console.input with the
#      typed text is in the marker file) and the turn event came back
#      (the SIXEL frames grew past the boot repaints)
#   3. the quiet variant sees no console.input at all
# Usage: studio/test/smoke_pty.sh <path-to-studio-exe>
set -u

EXE=${1:?usage: smoke_pty.sh <studio-exe>}
BRIDGE=$(cd "$(dirname "$0")/.." && pwd)/bridge/mock_bridge.py
LOG=$(mktemp)
SEEN=$(mktemp)
FIFO=$(mktemp -u)
mkfifo "$FIFO" || exit 1
trap 'rm -f "$FIFO" "$LOG" "$SEEN"' EXIT

# the driver: type a speak line (the mock turns it into turn_started +
# turn.finished), submit it, then let the timeout end the session
(
    sleep 3
    printf '写一段开场'
    sleep 0.5
    printf '\r'
    sleep 3
) > "$FIFO" &

TIANJI_HOME=/tmp \
STUDIO_BRIDGE_CMD="python3 '$BRIDGE'" \
STUDIO_MOCK_SEEN="$SEEN" \
timeout 10 script -qec "IM_TERM_SIZE=1200x800 '$EXE'" "$LOG" < "$FIFO"
STATUS=$?
wait

# 1. the session ran to the timeout (a crash exits earlier with 139+)
if [ "$STATUS" -ne 124 ]; then
    echo "SMOKE FAIL: the shell exited early (status $STATUS)"
    exit 1
fi

# 2. the speak round-tripped: the bridge SAW the console.input
if ! grep -q '写一段开场' "$SEEN"; then
    echo "SMOKE FAIL: the typed speak never reached the bridge"
    echo "  seen requests: $(wc -l < "$SEEN")"
    exit 1
fi
FRAMES=$(grep -o $'\x1bP' "$LOG" 2>/dev/null | wc -l)
if [ "$FRAMES" -lt 3 ]; then
    echo "SMOKE FAIL: too few SIXEL frames ($FRAMES)"
    exit 1
fi

echo "SMOKE PASS: speak round-tripped ($FRAMES frames), session survived"

# the quiet variant: no driver input -- the marker must show probes
# only (hello / project.get), never a console.input
LOG2=$(mktemp)
SEEN2=$(mktemp)
trap 'rm -f "$FIFO" "$LOG" "$SEEN" "$LOG2" "$SEEN2"' EXIT
TIANJI_HOME=/tmp \
STUDIO_BRIDGE_CMD="python3 '$BRIDGE'" \
STUDIO_MOCK_SEEN="$SEEN2" \
timeout 6 script -qec "IM_TERM_SIZE=1200x800 '$EXE'" "$LOG2" < /dev/null
STATUS2=$?
if [ "$STATUS2" -ne 124 ]; then
    echo "SMOKE FAIL: the quiet session exited early ($STATUS2)"
    exit 1
fi
if grep -q 'console.input' "$SEEN2"; then
    echo "SMOKE FAIL: the quiet session invented input"
    exit 1
fi
if ! grep -q 'bridge.hello' "$SEEN2"; then
    echo "SMOKE FAIL: the quiet session never said hello"
    exit 1
fi
echo "SMOKE PASS: quiet variant probed the bridge, no invented input"
exit 0
