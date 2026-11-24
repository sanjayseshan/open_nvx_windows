#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mode=${1:-quiet}

case "$mode" in
    quiet)
        export NVX_PERF_MODE=quiet
        export NVX_GPU_READBACK_TRACE=0
        export NVX_3DMARK_DIAG=0
        export NVX_NINE_DEBUG=0
        ;;
    diagnostic)
        export NVX_PERF_MODE=diagnostic
        export NVX_GPU_READBACK_TRACE=1
        export NVX_3DMARK_DIAG=1
        export NVX_NINE_DEBUG=1
        ;;
    *)
        echo "Usage: $0 quiet|diagnostic" >&2
        exit 2
        ;;
esac

export NVX_AUTO_RETRY=1
export NVX_RUN_3DMARK=1
export NVX_BUILD_MESA_ICD=1
export NVX_D3D9_BACKEND=nine
export NVX_NINE_ONLY=0

exec "$repo_root/test/test_full_driver.sh"
