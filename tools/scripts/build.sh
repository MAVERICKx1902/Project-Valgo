#!/usr/bin/env bash
# Configure and build Project Valgo. Usage: tools/scripts/build.sh [debug|release] [extra cmake args...]
set -euo pipefail

config="${1:-debug}"
shift || true

case "${config}" in
    debug|Debug)   build_type=Debug ;;
    release|Release) build_type=Release ;;
    *) echo "usage: $0 [debug|release]" >&2; exit 2 ;;
esac

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
generator="Ninja"
if ! command -v ninja >/dev/null 2>&1; then
    generator="Unix Makefiles"
fi

cmake -S "${root}" -B "${root}/build" -G "${generator}" \
      -DCMAKE_BUILD_TYPE="${build_type}" \
      -DVALGO_BUILD_GAMES=ON \
      "$@"
cmake --build "${root}/build" --parallel
