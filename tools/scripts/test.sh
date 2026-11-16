#!/usr/bin/env bash
# Run the full automated suite with a readable summary.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [[ ! -d "${root}/build" ]]; then
    echo "no build directory; run tools/scripts/build.sh first" >&2
    exit 2
fi
ctest --test-dir "${root}/build" --output-on-failure "$@"
