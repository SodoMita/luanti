#!/bin/bash
# Run a whole set of benchmark scenes and collect the numbers.
# usage: suite.sh <tag> <binary path> [seconds]
TAG=$1
BIN=$2
SECS=${3:-35}
OUT=/home/user/bench/results/$TAG.txt
mkdir -p /home/user/bench/results
: > "$OUT"

run() { # scene count extra...
	local scene=$1 count=$2; shift 2
	local extra="$*"
	echo "### $scene x$count $extra" | tee -a "$OUT"
	BIN="$BIN" bash /home/user/bench/run.sh "$scene" "$count" "$SECS" "$TAG" "$extra" >/dev/null 2>&1
	python3 /home/user/bench/summarize.py "/home/user/bench/logs/${TAG}_${scene}_${count}.log" 8 | tee -a "$OUT"
}

{
echo "=============================================================="
echo "benchmark suite: $TAG   binary: $BIN"
echo "date: $(date -u)"
echo "renderer: llvmpipe (SDL offscreen), 640x480, 2 vCPU"
echo "=============================================================="
} | tee -a "$OUT"

run none 0
run ent_cube 200
run ent_cube 1000
run ent_sprite 1000
run ent_mesh_low 1000
run ent_mesh 200
run ent_mesh 1000
run ent_mesh_hi 200
run node_plain 16
run node_nodebox 16
run node_mesh 16
echo "done: $OUT"
