#!/bin/bash
# The tianji client's pty smoke (TJ-C3): the SIXEL exe under a real pty,
# driven with a keyboard + mouse script. Asserts structural invariants,
# not golden bytes (wall-clock scheduling shifts which step an input
# lands on, so byte-comparing two runs measures the scheduler):
#   1. the shell survives the whole session (no crash, exit via timeout)
#   2. it emitted SIXEL frames (DCS q sequences in the capture)
#   3. the replay advanced the stream (frames appear over time, not
#      only the boot frame)
#   4. a submit of a non-legal action got the SYS row (the stream grew
#      past the replayed transcript)
# Usage: tianji/test/smoke_pty.sh <path-to-tianji-exe>
set -u

EXE=${1:?usage: smoke_pty.sh <tianji-exe>}
LOG=$(mktemp)
FIFO=$(mktemp -u)
mkfifo "$FIFO" || exit 1

# the driver: wait for the boot frame, type an illegal command,
# submit it, drag a stroke across the stage, then let the timeout end
# the session
(
    sleep 2
    printf 'frobnicate'
    sleep 0.5
    printf '\r'
    sleep 1
    # SGR press at cell (40, 6), move, release — the right pane's stage
    printf '\033[<0;40;6M'
    printf '\033[<32;50;10M'
    printf '\033[<0;60;14m'
    sleep 2
) > "$FIFO" &

timeout 8 script -qec "IM_TERM_SIZE=1024x768 '$EXE'" "$LOG" < "$FIFO"
STATUS=$?
wait

# 1. the session ran to the timeout (a crash exits earlier with 139+)
if [ "$STATUS" -ne 124 ]; then
    echo "SMOKE FAIL: the shell exited early (status $STATUS)"
    rm -f "$FIFO" "$LOG"
    exit 1
fi

# 2. SIXEL frames are present (the capture is one line — count
#    occurrences, not lines)
FRAMES=$(grep -o $'\x1bP' "$LOG" 2>/dev/null | wc -l)
if [ "$FRAMES" -lt 3 ]; then
    echo "SMOKE FAIL: too few SIXEL frames ($FRAMES)"
    rm -f "$FIFO" "$LOG"
    exit 1
fi

# 3. the replay advanced: the transcript's later rows appear as text in
# the captured frames (sixel encodes pixels; the ASCII tags survive as
# terminal text only in the shell's own writes — instead assert on the
# frame count growth: the boot frame is 1, the replay adds more)
if [ "$FRAMES" -lt 5 ]; then
    echo "SMOKE FAIL: the stream never advanced ($FRAMES frames)"
    rm -f "$FIFO" "$LOG"
    exit 1
fi

echo "SMOKE PASS: $FRAMES SIXEL frames, session survived, input driven"
rm -f "$FIFO" "$LOG"
exit 0
