#!/bin/sh
#
# xuan benchmark harness -- measures one target per invocation.
#
# Reports (as key=value lines and as a TSV row):
#   binary size, cold start (min/median/p95 ms) for `-e 0` and for a script,
#   and RSS (kB) of an idle script and of a minimal MQTT subscriber, each as
#   the peak and the settled floor over a fixed sampling window.
#
# Usage:
#   bench/run.sh [--xuan PATH] [--label NAME] [--runs N] [--rss-secs S]
#                [--mqtt-url URL] [--out DIR] [--no-mqtt]
#
# Env equivalents: XUAN, LABEL, RUNS, RSS_SECS, MQTT_URL, OUT
#
# The MQTT figure needs a reachable broker (default mqtt://127.0.0.1:1883);
# start one with e.g. `mosquitto -p 1883` or pass --no-mqtt to skip it.
#
# RSS is sampled once per second for RSS_SECS seconds. `peak` is the high-water
# mark of that window and `floor` is the settled value. On macOS the kernel
# reclaims clean pages, so RSS drifts over time; on Linux VmRSS is used.
#
set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

XUAN="${XUAN:-}"
LABEL="${LABEL:-}"
RUNS="${RUNS:-200}"
RSS_SECS="${RSS_SECS:-30}"
MQTT_URL="${MQTT_URL:-mqtt://127.0.0.1:1883}"
OUT="${OUT:-$script_dir/results}"
DO_MQTT=1

need() { [ "$#" -ge 2 ] || { echo "missing value for $1" >&2; exit 2; }; }

while [ "$#" -gt 0 ]; do
    case "$1" in
        --xuan)     need "$@"; XUAN="$2"; shift 2 ;;
        --label)    need "$@"; LABEL="$2"; shift 2 ;;
        --runs)     need "$@"; RUNS="$2"; shift 2 ;;
        --rss-secs) need "$@"; RSS_SECS="$2"; shift 2 ;;
        --mqtt-url) need "$@"; MQTT_URL="$2"; shift 2 ;;
        --out)      need "$@"; OUT="$2"; shift 2 ;;
        --no-mqtt)  DO_MQTT=0; shift ;;
        -h|--help)  sed -n '2,22p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [ -z "$XUAN" ]; then
    if [ -x "$script_dir/../build/xuan" ]; then
        XUAN="$script_dir/../build/xuan"
    elif command -v xuan >/dev/null 2>&1; then
        XUAN=$(command -v xuan)
    else
        echo "xuan not found; pass --xuan PATH" >&2
        exit 2
    fi
fi
[ -x "$XUAN" ] || { echo "not executable: $XUAN" >&2; exit 2; }

[ -n "$LABEL" ] || LABEL="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
mkdir -p "$OUT"

# --- clocks and memory ----------------------------------------------------

now_ms() {
    if date +%s%N 2>/dev/null | grep -qE '^[0-9]{16,}$'; then
        date +%s%N | awk '{ printf "%.3f", $1 / 1000000 }'
    elif command -v python3 >/dev/null 2>&1; then
        python3 -c 'import time; print("%.3f" % (time.time() * 1000))'
    elif command -v perl >/dev/null 2>&1; then
        perl -MTime::HiRes=time -e 'printf "%.3f", time * 1000'
    else
        echo 0
    fi
}

rss_kb() {
    pid="$1"
    if [ -r "/proc/$pid/status" ]; then
        awk '/^VmRSS:/ { print $2; exit }' "/proc/$pid/status"
    else
        ps -o rss= -p "$pid" 2>/dev/null | tr -d ' ' | head -n1
    fi
}

# Samples RSS once per second for $2 seconds. Prints: "peak floor" (kB).
rss_stats_kb() {
    pid="$1"; secs="$2"; tmp="$OUT/.rss.$$"; : > "$tmp"; i=0
    while [ "$i" -lt "$secs" ]; do
        r=$(rss_kb "$pid")
        [ -n "$r" ] && printf '%s\n' "$r" >> "$tmp"
        sleep 1
        i=$((i + 1))
    done
    peak=$(sort -n "$tmp" 2>/dev/null | tail -n1)
    floor=$(sort -n "$tmp" 2>/dev/null | head -n1)
    rm -f "$tmp"
    echo "${peak:-n/a} ${floor:-n/a}"
}

# --- fixed facts ----------------------------------------------------------

bin_bytes=$(wc -c < "$XUAN" | tr -d ' ')
versions=$("$XUAN" -e 'console.log(JSON.stringify(process.versions))' 2>/dev/null | tail -n1)
uname_s=$(uname -s); uname_m=$(uname -m); uname_r=$(uname -r)

# --- cold start -----------------------------------------------------------
# Prints: "min median p95" (ms). Falls back to a bulk-timed loop if the
# in-runtime spawner cannot be used on this target.
cold_start() {
    args="$1"
    out=$(XUAN_BIN="$XUAN" BENCH_ARGS="$args" RUNS="$RUNS" \
        "$XUAN" "$script_dir/coldstart.js" 2>"$OUT/.cold.err")
    min=$(printf '%s\n' "$out" | sed -n 's/^min=//p')
    med=$(printf '%s\n' "$out" | sed -n 's/^median=//p')
    p95=$(printf '%s\n' "$out" | sed -n 's/^p95=//p')
    if [ -z "$med" ]; then
        t0=$(now_ms); i=0
        while [ "$i" -lt "$RUNS" ]; do
            # shellcheck disable=SC2086
            "$XUAN" $args >/dev/null 2>&1
            i=$((i + 1))
        done
        t1=$(now_ms)
        med=$(awk -v a="$t0" -v b="$t1" -v n="$RUNS" 'BEGIN { printf "%.3f", (b - a) / n }')
        min='-'; p95='-'
    fi
    echo "$min $med $p95"
}

cold_start "-e 0" > "$OUT/.cold_eval"
read -r cold_eval_min cold_eval_median cold_eval_p95 < "$OUT/.cold_eval"

cold_start "$script_dir/hello.js" > "$OUT/.cold_script"
read -r cold_script_min cold_script_median cold_script_p95 < "$OUT/.cold_script"

# --- RSS: idle ------------------------------------------------------------

"$XUAN" "$script_dir/idle.js" >/dev/null 2>&1 &
idle_pid=$!
rss_stats_kb "$idle_pid" "$RSS_SECS" > "$OUT/.rss_idle"
read -r idle_rss_peak_kb idle_rss_floor_kb < "$OUT/.rss_idle"
kill "$idle_pid" 2>/dev/null
wait "$idle_pid" 2>/dev/null

# --- RSS: MQTT subscriber -------------------------------------------------

mqtt_rss_peak_kb="n/a"
mqtt_rss_floor_kb="n/a"

if [ "$DO_MQTT" = 1 ]; then
    log="$OUT/.mqtt.log"
    MQTT_URL="$MQTT_URL" "$XUAN" "$script_dir/mqtt-sub.js" > "$log" 2>&1 &
    mqtt_pid=$!
    i=0; ready=0
    while [ "$i" -lt 15 ]; do
        if grep -q '^READY' "$log" 2>/dev/null; then ready=1; break; fi
        kill -0 "$mqtt_pid" 2>/dev/null || break
        sleep 1
        i=$((i + 1))
    done
    if [ "$ready" = 1 ]; then
        rss_stats_kb "$mqtt_pid" "$RSS_SECS" > "$OUT/.rss_mqtt"
        read -r mqtt_rss_peak_kb mqtt_rss_floor_kb < "$OUT/.rss_mqtt"
    else
        echo "warning: MQTT subscriber not ready (url=$MQTT_URL)" >&2
        sed -n '1,3p' "$log" >&2
    fi
    kill "$mqtt_pid" 2>/dev/null
    wait "$mqtt_pid" 2>/dev/null
    rm -f "$log"
fi

# --- report ---------------------------------------------------------------

printf '%s\n' \
    "label=$LABEL" \
    "uname=$uname_s $uname_m $uname_r" \
    "xuan=$XUAN" \
    "versions=$versions" \
    "bin_bytes=$bin_bytes" \
    "cold_eval_min=$cold_eval_min" \
    "cold_eval_median=$cold_eval_median" \
    "cold_eval_p95=$cold_eval_p95" \
    "cold_script_min=$cold_script_min" \
    "cold_script_median=$cold_script_median" \
    "cold_script_p95=$cold_script_p95" \
    "idle_rss_peak_kb=$idle_rss_peak_kb" \
    "idle_rss_floor_kb=$idle_rss_floor_kb" \
    "mqtt_rss_peak_kb=$mqtt_rss_peak_kb" \
    "mqtt_rss_floor_kb=$mqtt_rss_floor_kb"

tsv="$OUT/$LABEL.tsv"
if [ ! -f "$tsv" ]; then
    printf 'label\tuname\tbin_bytes\tcold_eval_min\tcold_eval_median\tcold_eval_p95\tcold_script_min\tcold_script_median\tcold_script_p95\tidle_rss_peak_kb\tidle_rss_floor_kb\tmqtt_rss_peak_kb\tmqtt_rss_floor_kb\tversions\n' > "$tsv"
fi
printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$LABEL" "$uname_s $uname_m $uname_r" "$bin_bytes" \
    "$cold_eval_min" "$cold_eval_median" "$cold_eval_p95" \
    "$cold_script_min" "$cold_script_median" "$cold_script_p95" \
    "$idle_rss_peak_kb" "$idle_rss_floor_kb" \
    "$mqtt_rss_peak_kb" "$mqtt_rss_floor_kb" "$versions" >> "$tsv"
echo "wrote $tsv"
