#!/usr/bin/env bash
# Headless test run of the Linux build: Xvfb + software WSI, no audio, no real
# controllers, keyboard-emulated pad driven by xdotool. Safe to run while the
# user is using the desktop, and safe to run in parallel (one display and one
# run directory per agent).
#
# usage: tools/linux/headless_run.sh <tag> <seconds> [time:key ...]
#   time:key  press <key> at <time> seconds after launch (xdotool key names;
#             Return = Start, space = A). Default: the "start a new game" script.
#
# environment:
#   BUILD_DIR    build to test (default out/build/linux-amd64-relwithdebinfo)
#   RUN_DIR      isolated run directory (default /tmp/sh-headless-$USER-$DISPLAY_NUM)
#   OUT_DIR      where logs/screenshots go (default $RUN_DIR/out)
#   DISPLAY_NUM  Xvfb display number (default 90 + hash of $PWD, pick your own per agent)
#   RUNS         repeat the run N times and print a crash tally (default 1)
#   GDB=1        run under gdb; on the first unhandled guest AV, dump all backtraces and exit
#   PIN          taskset CPU list for the game (e.g. "2" for single core)
#   EXTRA        extra game command-line flags (e.g. "--ignore_thread_affinities=false")
set -u
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
TAG="${1:?tag}"; DUR="${2:?seconds}"; shift 2
KEYS=("$@")
[ ${#KEYS[@]} -eq 0 ] && KEYS=(30:Return 40:Return 50:space 60:space 70:space 80:space 90:space)

BUILD_DIR="${BUILD_DIR:-$REPO/out/build/linux-amd64-relwithdebinfo}"
DISPLAY_NUM="${DISPLAY_NUM:-$((90 + $(echo "$PWD" | cksum | cut -c1-2) % 9))}"
RUN_DIR="${RUN_DIR:-/tmp/sh-headless-$USER-$DISPLAY_NUM}"
OUT_DIR="${OUT_DIR:-$RUN_DIR/out}"
RUNS="${RUNS:-1}"
EXTRA="${EXTRA:-}"

for tool in Xvfb xdotool import gdb; do
  command -v $tool >/dev/null || { echo "missing $tool (apt install xvfb xdotool imagemagick gdb)"; exit 1; }
done
[ -x "$BUILD_DIR/splatterhouse" ] || { echo "no build at $BUILD_DIR"; exit 1; }

mkdir -p "$RUN_DIR" "$OUT_DIR"
cp -f "$BUILD_DIR/splatterhouse" "$BUILD_DIR"/*.so "$RUN_DIR/"
[ -e "$RUN_DIR/assets" ] || ln -s "$REPO/game" "$RUN_DIR/assets"
mkdir -p "$RUN_DIR/dlc"
cat > "$RUN_DIR/splatterhouse.toml" <<EOF
log_file = 'splatterhouse.log'
mnk_mode = true
EOF

AV_LINE=$(grep -n "Unhandled guest access violation" "$REPO/extern/rexglue-sdk/src/system/xmemory.cpp" | head -1 | cut -d: -f1)
cat > "$RUN_DIR/av.gdb" <<EOF
set pagination off
set confirm off
set debuginfod enabled off
set print thread-events off
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGUSR1 nostop noprint pass
handle SIGUSR2 nostop noprint pass
handle SIGPIPE nostop noprint pass
set breakpoint pending on
break xmemory.cpp:$((AV_LINE - 1))
commands
  silent
  printf "=== AV BREAK ===\n"
  bt 60
  printf "=== ALL THREADS ===\n"
  thread apply all bt 25
  kill
  quit
end
run
EOF

crashes=0
for run in $(seq 1 "$RUNS"); do
  id="${TAG}_$run"
  Xvfb ":$DISPLAY_NUM" -screen 0 1920x1080x24 -nolisten tcp >/dev/null 2>&1 &
  XPID=$!
  sleep 1
  LAUNCH=(./splatterhouse)
  [ "${GDB:-0}" = 1 ] && LAUNCH=(gdb -q -batch -x av.gdb --args ./splatterhouse)
  [ -n "${PIN:-}" ] && LAUNCH=(taskset -c "$PIN" "${LAUNCH[@]}")
  (
    cd "$RUN_DIR" || exit 1
    rm -f splatterhouse*.log
    exec env -u WAYLAND_DISPLAY DISPLAY=":$DISPLAY_NUM" SDL_VIDEODRIVER=x11 MESA_VK_WSI_DEBUG=sw \
      SDL_JOYSTICK_HIDAPI=0 \
      "${LAUNCH[@]}" --audio_mute --input_backend=none --log_max_file_size_mb=2000 $EXTRA
  ) > "$OUT_DIR/$id.stdout.txt" 2>&1 &
  GPID=$!
  START=$(date +%s)
  for k in "${KEYS[@]}"; do
    t=${k%%:*}; key=${k#*:}
    while [ $(( $(date +%s) - START )) -lt "$t" ]; do sleep 0.2; done
    kill -0 $GPID 2>/dev/null || break
    DISPLAY=":$DISPLAY_NUM" import -window root -resize 640x360 "$OUT_DIR/${id}_${t}.png" 2>/dev/null
    W=$(DISPLAY=":$DISPLAY_NUM" xdotool search --onlyvisible --name . 2>/dev/null | tail -1)
    [ -z "$W" ] && continue
    DISPLAY=":$DISPLAY_NUM" xdotool windowfocus --sync "$W" 2>/dev/null
    DISPLAY=":$DISPLAY_NUM" xdotool keydown --window "$W" "$key" 2>/dev/null
    sleep 0.15
    DISPLAY=":$DISPLAY_NUM" xdotool keyup --window "$W" "$key" 2>/dev/null
  done
  while [ $(( $(date +%s) - START )) -lt "$DUR" ] && kill -0 $GPID 2>/dev/null; do sleep 0.5; done
  DISPLAY=":$DISPLAY_NUM" import -window root -resize 640x360 "$OUT_DIR/${id}_end.png" 2>/dev/null
  pkill -P $GPID 2>/dev/null; kill $GPID 2>/dev/null; sleep 1
  pkill -9 -P $GPID 2>/dev/null; kill -9 $GPID 2>/dev/null
  kill $XPID 2>/dev/null; wait $XPID 2>/dev/null
  cat "$RUN_DIR"/splatterhouse*.log > "$OUT_DIR/$id.log" 2>/dev/null

  pads=$(grep -c "OnControllerDeviceAdded" "$OUT_DIR/$id.log")
  avs=$(grep -c "Unhandled guest access violation" "$OUT_DIR/$id.log")
  avb=$(grep -c "=== AV BREAK ===" "$OUT_DIR/$id.stdout.txt")
  heap=$(grep -c "BaseHeap::Alloc failed" "$OUT_DIR/$id.log")
  uf=$(grep -c "unregistered function" "$OUT_DIR/$id.log")
  bad=$(( avs + avb + heap + uf ))
  [ $bad -gt 0 ] && crashes=$((crashes + 1))
  echo "$id: guest-AV=$avs gdb-AV=$avb heap-fail=$heap unregistered=$uf real-pads-opened=$pads -> $([ $bad -gt 0 ] && echo CRASH || echo ok)"
  [ "$pads" -gt 0 ] && echo "WARNING: a real controller was opened; check SDL env vars before running again"
done
echo "$TAG: $crashes/$RUNS runs crashed. Output in $OUT_DIR"
