#!/usr/bin/env bash
# Deterministic screenshot of the FPS training range.
# Usage: tools/scripts/capture.sh [output.png] [frames]
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
output="${1:-${root}/build/capture.png}"
frames="${2:-120}"

"${root}/build/bin/valgo_fps" --headless --deterministic \
    --width 640 --height 360 --frames "${frames}" \
    --capture "${output}" --log-level info
echo "wrote ${output}"
