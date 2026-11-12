#!/usr/bin/env bash
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1
instance="run$$_shm"
scenario="scenarios/demo.txt"
log_path=""
controller_args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --scenario) [[ $# -ge 2 ]] || exit 2; scenario="$2"; shift 2 ;;
        --instance) [[ $# -ge 2 ]] || exit 2; instance="$2"; shift 2 ;;
        --log) [[ $# -ge 2 ]] || exit 2; log_path="$2"; shift 2 ;;
        *) controller_args+=("$1"); shift ;;
    esac
done
if [[ -z "$log_path" ]]; then log_path="logs/shm_${instance}.log"; fi
mkdir -p "$(dirname "$log_path")" || exit 1
make -s fly_external_shm fly_process_worker || exit 1
(
    pid_controller=""
    child_pids=()
    cleanup() {
        if [[ -n "$pid_controller" ]]; then kill -TERM "$pid_controller" 2>/dev/null || true; fi
        for pid in "${child_pids[@]}"; do kill -TERM "$pid" 2>/dev/null || true; done
    }
    trap cleanup EXIT INT TERM
    ./fly_external_shm --instance "$instance" --scenario "$scenario" "${controller_args[@]}" &
    pid_controller=$!
    sleep 0.05
    for role in orders dispatcher drones stations; do
        ./fly_process_worker --transport shm --role "$role" --instance "$instance" \
            --scenario "$scenario" &
        child_pids+=("$!")
    done
    wait "$pid_controller"
    result=$?
    for pid in "${child_pids[@]}"; do wait "$pid" || result=1; done
    exit "$result"
) 2>&1 | tee "$log_path"
result=${PIPESTATUS[0]}
./fly_external_shm --cleanup --instance "$instance" >/dev/null
printf "Saved combined log: %s\n" "$log_path"
exit "$result"
