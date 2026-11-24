#!/usr/bin/env bash
set -u

VM_WRAPPER_PID=${1:?usage: sample_qemu_pc.sh vm-wrapper-pid output-log}
OUTPUT=${2:?usage: sample_qemu_pc.sh vm-wrapper-pid output-log}
INTERVAL=${NVX_PC_SAMPLE_INTERVAL:-2}
MONITOR=/tmp/qemu-profile-monitor.sock
CLK_TCK=$(getconf CLK_TCK 2>/dev/null || echo 100)
START_NS=$(date +%s%N)
PREVIOUS_SAMPLE_NS=0
declare -A PREVIOUS_TICKS

find_qemu_pid() {
    local file pid args
    for file in /proc/[0-9]*/cmdline; do
        [ -r "$file" ] || continue
        args=$(tr '\000' ' ' < "$file" 2>/dev/null) || continue
        case "$args" in
            *qemu-system-x86_64*"$MONITOR"*)
                pid=${file#/proc/}
                printf '%s\n' "${pid%/cmdline}"
                return 0
                ;;
        esac
    done
    return 1
}

thread_ticks() {
    local stat rest
    stat=$(cat "$1" 2>/dev/null) || return 1
    rest=${stat##*) }
    # After removing pid and comm, fields 12 and 13 are utime and stime.
    awk '{print $12 + $13}' <<< "$rest"
}

thread_name() {
    tr '\n' ' ' < "$1/comm" 2>/dev/null || printf '?'
}

mkdir -p "$(dirname "$OUTPUT")"
{
    printf 'QEMU_PC_SAMPLE_START interval_s=%s vm_wrapper_pid=%s\n' \
        "$INTERVAL" "$VM_WRAPPER_PID"
    printf 'Notes: RIP/EIP is sampled state, not a CPU utilization metric.\n'
} >> "$OUTPUT"

while kill -0 "$VM_WRAPPER_PID" 2>/dev/null; do
    sleep "$INTERVAL"
    QEMU_PID=$(find_qemu_pid 2>/dev/null || true)
    [ -n "$QEMU_PID" ] || continue
    NOW_NS=$(date +%s%N)
    ELAPSED=$(( (NOW_NS - START_NS) / 1000000000 ))
    if [ "$PREVIOUS_SAMPLE_NS" -eq 0 ]; then
        WALL_INTERVAL_NS=$((NOW_NS - START_NS))
    else
        WALL_INTERVAL_NS=$((NOW_NS - PREVIOUS_SAMPLE_NS))
    fi
    PREVIOUS_SAMPLE_NS=$NOW_NS
    {
        printf '\nPC_SAMPLE elapsed_s=%s wall_interval_ms=%s qemu_pid=%s\n' \
            "$ELAPSED" "$((WALL_INTERVAL_NS / 1000000))" "$QEMU_PID"
        printf 'HOST_THREAD_CPU_DELTA (cpu_percent normalized to wall_interval_ms)\n'
    } >> "$OUTPUT"

    # Sample each QEMU thread's host CPU time over the interval. KVM vCPU
    # threads commonly have CPU-N names; device/emulator threads are retained
    # too, to reveal host-side work outside the guest.
    for stat_file in /proc/"$QEMU_PID"/task/*/stat; do
        [ -r "$stat_file" ] || continue
        tid=${stat_file%/stat}
        tid=${tid##*/}
        ticks=$(thread_ticks "$stat_file") || continue
        old=${PREVIOUS_TICKS[$tid]:-$ticks}
        delta=$((ticks - old))
        PREVIOUS_TICKS[$tid]=$ticks
        name=$(thread_name "${stat_file%/stat}")
        awk -v n="$name" -v id="$tid" -v d="$delta" \
            -v hz="$CLK_TCK" -v wall_ns="$WALL_INTERVAL_NS" \
            'BEGIN { pct = wall_ns > 0 ? d*100000000000/(hz*wall_ns) : 0; printf "  tid=%s name=%s cpu_ms=%0.1f cpu_percent=%0.1f\n", id, n, d*1000/hz, pct }' \
            >> "$OUTPUT"
    done

    # Query vCPU registers through the dedicated HMP socket. A separate
    # connection avoids contention with auto-retry sendkey/powerdown traffic.
    {
        for cpu in 0 1 2 3 4 5; do
            printf 'cpu %s\ninfo registers\n' "$cpu"
        done
    } | timeout 5 socat -T 2 - UNIX-CONNECT:"$MONITOR" 2>&1 |
        sed 's/^/  HMP /' >> "$OUTPUT" || true
done

printf '\nQEMU_PC_SAMPLE_END elapsed_s=%s\n' \
    "$(( ($(date +%s%N) - START_NS) / 1000000000 ))" >> "$OUTPUT"
