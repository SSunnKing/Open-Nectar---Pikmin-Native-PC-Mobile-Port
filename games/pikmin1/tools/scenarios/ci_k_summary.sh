#!/usr/bin/env bash
# Prints the throw_release calibration K for each rate from the scenario logs
# (run_standin_scenario.cmake LOG_DIR) and the runner self-test `[check]` lines
# from CTest's LastTest.log, and appends them to $GITHUB_STEP_SUMMARY when it is
# set. Reports only; never fails on a K value.
#   ci_k_summary.sh <build dir> <label>
build="$1"
label="$2"
dir="$build/scenario-logs"
rows=""
for rate in 30 60; do
    log="$dir/scenario_throw_release_${rate}.log"
    if [ -f "$log" ]; then
        line=$(tr -d '\r' < "$log" | awk '/\[scenario\] throw_release calibration: K =/ { print; exit }')
        verdict=$(tr -d '\r' < "$log" | awk '/\[scenario\] (PASS|FAIL)/ { print; exit }')
        [ -n "$line" ] || line="(no calibration line)"
        [ -n "$verdict" ] || verdict="(no verdict line)"
    else
        line="(no log: $log)"
        verdict=""
    fi
    echo "K ${label} ${rate} Hz: ${line} ${verdict}"
    rows="${rows}| ${rate} | \`${line}\` | \`${verdict}\` |
"
done
checks=$(cat "$build"/Testing/Temporary/LastTest*.log 2>/dev/null | tr -d '\r' | awk '/^[0-9]+\/[0-9]+ Test: scenario_(save_dir|exit_)/ || /^\[check\]/ { print }')
if [ -n "$checks" ]; then
    echo "Runner self-tests (${label}):"
    echo "$checks"
fi
if [ -n "$GITHUB_STEP_SUMMARY" ]; then
    {
        echo "### throw_release calibration K (${label})"
        echo
        echo "| Rate | Calibration line | Verdict |"
        echo "|------|------------------|---------|"
        printf '%s' "$rows"
        echo
        if [ -n "$checks" ]; then
            echo "<details><summary>Runner self-tests</summary>"
            echo
            echo '```'
            echo "$checks"
            echo '```'
            echo "</details>"
            echo
        fi
    } >> "$GITHUB_STEP_SUMMARY"
fi
exit 0
