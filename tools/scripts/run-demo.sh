#!/usr/bin/env bash
# Runs both games' scripted demos headlessly and reports their summaries.
# This is the "launch the application and verify the feature" step of the
# milestone checklist, automated.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

echo "== Tactical FPS =="
"${root}/build/bin/valgo_fps" --headless --demo --deterministic \
    --width 320 --height 180 --frames 400 --log-level info 2>&1 | grep -E "fired|hits|session"

echo "== Orbit Runner =="
"${root}/build/bin/valgo_orbit" --headless --demo --deterministic \
    --width 320 --height 180 --frames 400 --log-level info 2>&1 | grep -E "session|run finished"
