
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p logs

extract_stats() {
    grep -E '^(Simulation time:|Total orders:|Delivered orders:|Failed orders:|Total flight distance:|Delivery success rate:)' \
        | tr -d '\r'
}

check_scenario() {
    local scenario="$1"
    local id="t$$_$(basename "$scenario" .txt | cut -c1-2)"
    local reference output actual
    reference="$(./fly_simulator --auto --quiet --scenario "$scenario" | extract_stats)"
    for mode in unnamed named; do
        echo "[PROCESS TEST] ${scenario}: fork-${mode}"
        output="$(timeout 20s "./fly_fork_${mode}" --instance "${id}_${mode}" --quiet --scenario "$scenario")"
        actual="$(echo "$output" | extract_stats)"
        if [[ "$reference" != "$actual" ]]; then
            echo "Expected:"; echo "$reference"; echo "Got:"; echo "$actual"; return 1
        fi
    done
    for mode in shm; do
        echo "[PROCESS TEST] ${scenario}: external-${mode}"
        output="$(timeout 25s bash scripts/run_external.sh --instance "${id}_${mode}" \
            --quiet --scenario "$scenario" --log "logs/test_${mode}.log")"
        actual="$(echo "$output" | extract_stats)"
        if [[ "$reference" != "$actual" ]]; then
            echo "Expected:"; echo "$reference"; echo "Got:"; echo "$actual"; return 1
        fi
    done
}

for scenario in scenarios/small.txt scenarios/deadline.txt scenarios/demo.txt scenarios/batch.txt; do
    check_scenario "$scenario"
done

for mode in unnamed named; do
    id="interrupt${mode}$$"
    echo "[PROCESS TEST] SIGINT: fork-${mode}"
    "./fly_fork_${mode}" --quiet --instance "$id" --delay 60 >"logs/test_interrupt_${mode}.log" &
    pid=$!
    sleep .25
    kill -INT "$pid" 2>/dev/null || true
    if wait "$pid"; then
        echo "Interrupted controller unexpectedly reported success" >&2
        exit 1
    fi
    grep -q 'interrupted by user' "logs/test_interrupt_${mode}.log"
    if [[ "$mode" == named && -e "/dev/shm/dd_${id}_state" ]]; then
        echo "IPC state leaked after SIGINT" >&2
        exit 1
    fi
done

for mode in shm; do
    id="iex_${mode}_$$"
    echo "[PROCESS TEST] SIGINT: external-${mode}"
    ./fly_external_shm --quiet --delay 60 \
        --instance "$id" >"logs/test_interrupt_external_${mode}.log" &
    controller=$!
    sleep .05
    children=()
    for role in orders dispatcher drones stations; do
        ./fly_process_worker --transport "$mode" --role "$role" --instance "$id" \
            >"logs/test_interrupt_external_${mode}_${role}.log" &
        children+=("$!")
    done
    sleep .25
    kill -INT "$controller" 2>/dev/null || true
    if wait "$controller"; then
        echo "Interrupted external controller unexpectedly reported success" >&2
        exit 1
    fi
    for pid in "${children[@]}"; do
        wait "$pid"
    done
    grep -q 'interrupted by user' "logs/test_interrupt_external_${mode}.log"
    if [[ -e "/dev/shm/dd_${id}_state" ]]; then
        echo "IPC state or queue leaked after SIGINT" >&2
        exit 1
    fi
done

echo "[PROCESS TEST] All 15 process integration checks passed."
