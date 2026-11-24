#!/bin/sh
# Host (Linux) test of the SunSpec module: compiles the real
# src/plugins/SunSpec/*.h against small stubs (POSIX sockets instead of
# AsyncTCP) and runs tools/sunspec_read.py against it.
# Built twice: minimal (default) and with the optional models 120 / 160.
# No ESP toolchain needed. Usage: ./run.sh
set -e
cd "$(dirname "$0")"
B=$(mktemp -d)
mkdir -p "$B/plugins/SunSpec" "$B/hm"
cp -r stubs/* "$B/"
cp ../../src/plugins/SunSpec/*.h "$B/plugins/SunSpec/"
cp ../../src/hm/hmDefines.h "$B/hm/"
cp main.cpp "$B/"
PORT=15502

run() { # $1: mode, $2: allow list, $3: phase scenario, rest: client args
    m=$1; a=$2; p=$3; shift 3
    "$B/sshost" $PORT $m "$a" $p >/dev/null & PID=$!
    sleep 0.5
    python3 -I ../sunspec_read.py 127.0.0.1 --port $PORT "$@" || { kill $PID; exit 1; }
    kill $PID; wait $PID 2>/dev/null || true
}

for FLAGS in "" "-DSUNSPEC_MODEL_120 -DSUNSPEC_MODEL_160"; do
    echo "=== build: ${FLAGS:-minimal} ==="
    g++ -std=gnu++17 -DESP32 $FLAGS -Wall -Wextra -Werror -I "$B/inc" -I "$B" -o "$B/sshost" "$B/main.cpp"
    echo "--- combined, HM-800 (L1) + HMT-2250 ---"
    run 0 "" "" --negative
    echo "--- combined, single phase inverters on L1 only ---"
    run 0 "" 1ph
    echo "--- combined, HM-800 on L1 + HMS on L2 ---"
    run 0 "" 2ph
    echo "--- separate ---"
    run 1 "" "" --scan 1-4
done
rm -rf "$B"
