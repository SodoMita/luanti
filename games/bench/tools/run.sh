#!/bin/bash
# Rendering benchmark runner for Luanti
# usage: run.sh <scene> [count] [seconds] [tag] [extra settings...]
#   BIN=/path/to/binary  (default: /home/user/luanti/bin/luanti)
SCENE=${1:-none}
COUNT=${2:-1000}
SECS=${3:-40}
TAG=${4:-run}
shift 4 2>/dev/null
EXTRA="$*"

RUN_DIR=/home/user/bench
LOGDIR=$RUN_DIR/logs
mkdir -p "$LOGDIR"
LOGF="$LOGDIR/${TAG}_${SCENE}_${COUNT}.log"
CONF=$RUN_DIR/bench.conf
BIN=${BIN:-/home/user/luanti/bin/luanti}
BINDIR=$(dirname "$BIN")

cat > "$CONF" <<EOF
bench_scene = $SCENE
bench_count = $COUNT
bench_freeze = true
bench_dist = 20
bench_spacing = 1.05
bench_grid = 16
bench_pitch = 0.0

fps_max = 100000
fps_max_unfocused = 100000
vsync = false
window_size = 640,480
screen_w = 640
screen_h = 480
fullscreen = false
profiler_print_interval = 1
debug_log_level = info

viewing_range = 100
client_mesh_chunk = 2
enable_shadows = false
enable_bloom = false
enable_water_reflections = false
enable_waving_water = false
enable_waving_leaves = false
enable_waving_plants = false
enable_dynamic_shadows = false
enable_fog = false
time_speed = 0
client_mapblock_limit = 0
smooth_lighting = true
opaque_water = false
connected_glass = false
anisotropic_filter = 1
bilinear_filter = false
trilinear_filter = false
mip_map = false
texture_minimap = false
num_emit_threads = 2

bench_screenshot_interval = 4
bench_screenshot_max = 3
bench_screenshot_skip = 60
$EXTRA
EOF

WORLD=$RUN_DIR/worlds/bench

cd "$BINDIR"
export SDL_VIDEODRIVER=offscreen
export LIBGL_ALWAYS_SOFTWARE=1
export GALLIUM_DRIVER=llvmpipe
export LP_NUM_THREADS=2
export XDG_RUNTIME_DIR=/tmp/xdgr
mkdir -p /tmp/xdgr

timeout -s INT "$SECS" "$BIN" \
    --config "$CONF" \
    --world "$WORLD" \
    --gameid bench \
    --go --info \
    > "$LOGF" 2>&1
echo "exit: $? -> $LOGF"

if [ -z "$NOSUMMARY" ]; then python3 /home/user/bench/summarize.py "$LOGF"; fi
