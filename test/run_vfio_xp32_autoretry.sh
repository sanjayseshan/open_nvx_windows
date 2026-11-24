#!/usr/bin/env bash
set -euo pipefail
trap '' HUP

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR"
NVX_XP32_IMAGE="${NVX_XP32_IMAGE:-$ROOT_DIR/winxp_x32.qcow2}"

# The guest batch lock survives a hard power-off. Clear that stale lock before
# starting a new run so Startup does not silently skip the test on the next
# boot. Only edit the offline image here, before QEMU takes its write lock.
if command -v guestfish >/dev/null 2>&1 && qemu-img info "$NVX_XP32_IMAGE" >/dev/null 2>&1; then
    guestfish --rw -a "$NVX_XP32_IMAGE" -i rm-rf /test_accel/startup_test.lock
fi

NVX_RUN_LOG="$ROOT_DIR/test_logs/xp_v2_$(date +%Y%m%d-%H%M%S).log"
ln -sfn "$NVX_RUN_LOG" "$ROOT_DIR/vm_serial.log"
touch "$NVX_RUN_LOG"

MAX_RETRIES="${MAX_RETRIES:-10}"
BOOT_TIMEOUT="${BOOT_TIMEOUT:-35}"
NVX_RUN_3DMARK="${NVX_RUN_3DMARK:-0}"
if [ "$NVX_RUN_3DMARK" = "1" ]; then
    NVX_TEST_TIMEOUT="${NVX_TEST_TIMEOUT:-1800}"
else
    NVX_TEST_TIMEOUT="${NVX_TEST_TIMEOUT:-120}"
fi

echo "=========================================================="
echo " Starting Windows XP x32 VFIO Runner with Auto-Retry"
echo " Serial log: $NVX_RUN_LOG"
echo " Max retries: $MAX_RETRIES | Boot detection timeout: ${BOOT_TIMEOUT}s"
echo " Test timeout: ${NVX_TEST_TIMEOUT}s | 3DMark: ${NVX_RUN_3DMARK} | VNC: ${NVX_VNC_ENABLE:-0}"
echo "=========================================================="

attempt=1
while [ "$attempt" -le "$MAX_RETRIES" ]; do
    echo "==> [Attempt $attempt/$MAX_RETRIES] Launching Windows XP VM..."
    > "$NVX_RUN_LOG"

    # Start VM in background
    NVX_SERIAL_LOG="$NVX_RUN_LOG" "$SCRIPT_DIR/run_vfio_vm_xp32.sh" < /dev/null &
    VM_PID=$!

    # Watch for serial progress
    booted=0
    for i in $(seq 1 "$BOOT_TIMEOUT"); do
        sleep 1
        if ! kill -0 "$VM_PID" 2>/dev/null; then
            echo "--> QEMU process exited on attempt $attempt."
            break
        fi

        if grep -q "NVX" "$NVX_RUN_LOG" 2>/dev/null || \
           grep -q "Miniport" "$NVX_RUN_LOG" 2>/dev/null; then
            echo "==> Boot confirmed! Windows XP kernel/driver is actively booting."
            booted=1
            break
        fi
    done

    if [ "$booted" -eq 1 ]; then
        echo "==> Driver activity confirmed (PID $VM_PID). Monitoring session..."

        # Monitor loop for test completion or timeout, then soft powerdown
        for s in $(seq 1 "$NVX_TEST_TIMEOUT"); do
            sleep 1
            if ! kill -0 "$VM_PID" 2>/dev/null; then
                break
            fi

            # Send Enter key at key startup points to ensure automated logon
            if [ "$s" -ge 8 ] && [ "$s" -le 24 ] && [ $((s % 4)) -eq 0 ]; then
                echo "sendkey ret" | socat - UNIX-CONNECT:/tmp/qemu-monitor.sock 2>/dev/null || true
            fi

            if grep -Eq "ALL_BENCHMARKS_FINISHED|\[3DMARK05_(RUN_COMPLETE|NOT_INSTALLED)\]" "$NVX_RUN_LOG" 2>/dev/null; then
                if [ "${NVX_KEEP_RUNNING:-0}" = "1" ]; then
                    echo "==> Test result reached; NVX_KEEP_RUNNING=1, leaving the guest up for VNC review."
                    while kill -0 "$VM_PID" 2>/dev/null; do sleep 60; done
                    break
                fi
                echo "==> All benchmarks completed! Sending soft ACPI powerdown..."
                echo "system_powerdown" | socat - UNIX-CONNECT:/tmp/qemu-monitor.sock 2>/dev/null || true
                sleep 5
                break
            fi

            # If desktop has been active for 75s, perform soft powerdown
            if [ "$NVX_RUN_3DMARK" != "1" ] && [ "$s" -ge 75 ] && grep -q "GPU solid fill" "$NVX_RUN_LOG" 2>/dev/null; then
                echo "==> Desktop active session completed ($s s). Sending soft ACPI powerdown..."
                echo "system_powerdown" | socat - UNIX-CONNECT:/tmp/qemu-monitor.sock 2>/dev/null || true
                sleep 6
                break
            fi
        done

        # Graceful wait for VM exit, fallback to quit
        if kill -0 "$VM_PID" 2>/dev/null; then
            echo "system_powerdown" | socat - UNIX-CONNECT:/tmp/qemu-monitor.sock 2>/dev/null || true
            sleep 4
        fi
        if kill -0 "$VM_PID" 2>/dev/null; then
            echo "quit" | socat - UNIX-CONNECT:/tmp/qemu-monitor.sock 2>/dev/null || true
            sleep 2
        fi

        wait "$VM_PID" || true
        echo "==> Windows XP session completed."
        exit 0
    else
        echo "==> No serial activity within ${BOOT_TIMEOUT}s; GPU did not boot cleanly. Retrying..."
        # Try guest ACPI shutdown first. If the guest never reached an ACPI
        # handler, ask QEMU's monitor to exit; signals are a last resort.
        echo "system_powerdown" | socat - UNIX-CONNECT:/tmp/qemu-monitor.sock 2>/dev/null || true
        for _ in $(seq 1 8); do
            kill -0 "$VM_PID" 2>/dev/null || break
            sleep 1
        done
        if kill -0 "$VM_PID" 2>/dev/null; then
            echo "quit" | socat - UNIX-CONNECT:/tmp/qemu-monitor.sock 2>/dev/null || true
            for _ in $(seq 1 4); do
                kill -0 "$VM_PID" 2>/dev/null || break
                sleep 1
            done
        fi
        if kill -0 "$VM_PID" 2>/dev/null; then
            pkill -TERM -P "$VM_PID" 2>/dev/null || true
            kill -TERM "$VM_PID" 2>/dev/null || true
            sleep 2
        fi
        if kill -0 "$VM_PID" 2>/dev/null; then
            echo "Warning: QEMU did not exit after monitor shutdown; forcing final termination." >&2
            pkill -KILL -P "$VM_PID" 2>/dev/null || true
            kill -KILL "$VM_PID" 2>/dev/null || true
        fi
        wait "$VM_PID" 2>/dev/null || true
        sleep 3
    fi

    attempt=$((attempt + 1))
done

echo "Error: Exceeded max retries without successful boot." >&2
exit 1
