#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NVX_MESA_ARCH=x86 exec "$ROOT/test/build_mesa_xp64.sh" "$@"
