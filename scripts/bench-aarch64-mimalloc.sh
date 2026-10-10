#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCH_JS="${BENCH_JS:-${ROOT_DIR}/benchmark/small-alloc.js}"
REPEAT="${REPEAT:-10}"

XUAN_OFF="${XUAN_OFF:-${ROOT_DIR}/build-aarch64-off/xuan}"
XUAN_ON="${XUAN_ON:-${ROOT_DIR}/build-aarch64-on/xuan}"

if [[ $# -ge 1 ]]; then
	XUAN_OFF="$1"
fi
if [[ $# -ge 2 ]]; then
	XUAN_ON="$2"
fi

if [[ ! -x "${XUAN_OFF}" ]]; then
	echo "Error: XUAN_OFF not executable: ${XUAN_OFF}"
	exit 1
fi
if [[ ! -x "${XUAN_ON}" ]]; then
	echo "Error: XUAN_ON not executable: ${XUAN_ON}"
	exit 1
fi
if [[ ! -f "${BENCH_JS}" ]]; then
	echo "Error: benchmark script not found: ${BENCH_JS}"
	exit 1
fi
if [[ ! -x "/usr/bin/time" ]]; then
	echo "Error: /usr/bin/time not found"
	exit 1
fi

BENCH_WARMUP_ROUNDS="${BENCH_WARMUP_ROUNDS:-100}"
BENCH_ROUNDS="${BENCH_ROUNDS:-800}"
BENCH_BATCH="${BENCH_BATCH:-4000}"
BENCH_MIN_SIZE="${BENCH_MIN_SIZE:-16}"
BENCH_MAX_SIZE="${BENCH_MAX_SIZE:-256}"
BENCH_LIVE_SET="${BENCH_LIVE_SET:-12000}"
BENCH_RETAIN_PCT="${BENCH_RETAIN_PCT:-20}"
BENCH_GC_EVERY="${BENCH_GC_EVERY:-40}"
BENCH_SEED="${BENCH_SEED:-324478056}"

WORK_DIR="$(mktemp -d /tmp/xuan-mi-bench.XXXXXX)"
trap 'rm -rf "${WORK_DIR}"' EXIT

OFF_OPS_FILE="${WORK_DIR}/off.ops"
ON_OPS_FILE="${WORK_DIR}/on.ops"
OFF_RSS_FILE="${WORK_DIR}/off.rss"
ON_RSS_FILE="${WORK_DIR}/on.rss"
OFF_ELAPSED_FILE="${WORK_DIR}/off.elapsed"
ON_ELAPSED_FILE="${WORK_DIR}/on.elapsed"

touch "${OFF_OPS_FILE}" "${ON_OPS_FILE}" "${OFF_RSS_FILE}" "${ON_RSS_FILE}" "${OFF_ELAPSED_FILE}" "${ON_ELAPSED_FILE}"

metric_value() {
	local key="$1"
	local file="$2"
	awk -v k="${key}" '
	$1=="SMALL_ALLOC_METRIC" {
		for (i = 2; i <= NF; i++) {
			split($i, kv, "=");
			if (kv[1] == k) {
				print kv[2];
				exit;
			}
		}
	}' "${file}"
}

detect_time_mode() {
	/usr/bin/time -f "ELAPSED_SEC=%e RSS_KB=%M" ls >/dev/null 2>"${WORK_DIR}/time.detect" || true
	if grep -q "ELAPSED_SEC=" "${WORK_DIR}/time.detect"; then
		echo "gnu"
		return
	fi
	/usr/bin/time -l -p ls >/dev/null 2>"${WORK_DIR}/time.detect" || true
	if grep -q "^real[[:space:]]" "${WORK_DIR}/time.detect"; then
		echo "bsd"
		return
	fi
	echo "unknown"
}

TIME_MODE="$(detect_time_mode)"
if [[ "${TIME_MODE}" == "unknown" ]]; then
	echo "Error: unsupported /usr/bin/time flavor"
	exit 1
fi

time_value() {
	local key="$1"
	local file="$2"
	if [[ "${TIME_MODE}" == "gnu" ]]; then
		awk -v k="${key}" '
		{
			for (i = 1; i <= NF; i++) {
				split($i, kv, "=");
				if (kv[1] == k) {
					print kv[2];
					exit;
				}
			}
		}' "${file}"
		return
	fi

	if [[ "${key}" == "RSS_KB" ]]; then
		awk '/maximum resident set size/ { print $1; exit }' "${file}"
		return
	fi
	if [[ "${key}" == "ELAPSED_SEC" ]]; then
		awk '/^real[[:space:]]+/ { print $2; exit }' "${file}"
		return
	fi
}

run_one() {
	local label="$1"
	local runtime="$2"
	local idx="$3"
	local out_file="${WORK_DIR}/${label}.${idx}.out"
	local time_file="${WORK_DIR}/${label}.${idx}.time"

	if [[ "${TIME_MODE}" == "gnu" ]]; then
		set +e
		BENCH_WARMUP_ROUNDS="${BENCH_WARMUP_ROUNDS}" \
		BENCH_ROUNDS="${BENCH_ROUNDS}" \
		BENCH_BATCH="${BENCH_BATCH}" \
		BENCH_MIN_SIZE="${BENCH_MIN_SIZE}" \
		BENCH_MAX_SIZE="${BENCH_MAX_SIZE}" \
		BENCH_LIVE_SET="${BENCH_LIVE_SET}" \
		BENCH_RETAIN_PCT="${BENCH_RETAIN_PCT}" \
		BENCH_GC_EVERY="${BENCH_GC_EVERY}" \
		BENCH_SEED="${BENCH_SEED}" \
		/usr/bin/time -f "ELAPSED_SEC=%e RSS_KB=%M MINFLT=%R MAJFLT=%F" \
			"${runtime}" "${BENCH_JS}" >"${out_file}" 2>"${time_file}"
		set -e
	else
		set +e
		BENCH_WARMUP_ROUNDS="${BENCH_WARMUP_ROUNDS}" \
		BENCH_ROUNDS="${BENCH_ROUNDS}" \
		BENCH_BATCH="${BENCH_BATCH}" \
		BENCH_MIN_SIZE="${BENCH_MIN_SIZE}" \
		BENCH_MAX_SIZE="${BENCH_MAX_SIZE}" \
		BENCH_LIVE_SET="${BENCH_LIVE_SET}" \
		BENCH_RETAIN_PCT="${BENCH_RETAIN_PCT}" \
		BENCH_GC_EVERY="${BENCH_GC_EVERY}" \
		BENCH_SEED="${BENCH_SEED}" \
		/usr/bin/time -l -p \
			"${runtime}" "${BENCH_JS}" >"${out_file}" 2>"${time_file}"
		set -e
	fi

	local ops
	local elapsed_ms
	local rss_kb
	local elapsed_sec
	ops="$(metric_value "ops_per_sec" "${out_file}")"
	elapsed_ms="$(metric_value "elapsed_ms" "${out_file}")"
	rss_kb="$(time_value "RSS_KB" "${time_file}")"
	elapsed_sec="$(time_value "ELAPSED_SEC" "${time_file}")"

	if [[ -z "${rss_kb}" ]]; then
		rss_kb="-1"
	fi

	if [[ -z "${ops}" || -z "${elapsed_ms}" || -z "${elapsed_sec}" ]]; then
		echo "Error: failed parsing metrics for ${label} run ${idx}"
		echo "stdout:"
		cat "${out_file}"
		echo "stderr:"
		cat "${time_file}"
		exit 1
	fi

	if [[ "${label}" == "off" ]]; then
		echo "${ops}" >>"${OFF_OPS_FILE}"
		echo "${elapsed_ms}" >>"${OFF_ELAPSED_FILE}"
		echo "${rss_kb}" >>"${OFF_RSS_FILE}"
	else
		echo "${ops}" >>"${ON_OPS_FILE}"
		echo "${elapsed_ms}" >>"${ON_ELAPSED_FILE}"
		echo "${rss_kb}" >>"${ON_RSS_FILE}"
	fi

	printf "run=%02d variant=%s ops_per_sec=%s elapsed_ms=%s rss_kb=%s elapsed_sec=%s\n" "${idx}" "${label}" "${ops}" "${elapsed_ms}" "${rss_kb}" "${elapsed_sec}"
}

calc_median() {
	local file="$1"
	sort -n "${file}" | awk '
	{ a[NR] = $1 }
	END {
		if (NR == 0) {
			print "0";
		} else if (NR % 2 == 1) {
			print a[(NR + 1) / 2];
		} else {
			print (a[NR / 2] + a[NR / 2 + 1]) / 2;
		}
	}'
}

calc_mean() {
	local file="$1"
	awk '
	{ s += $1; n += 1 }
	END {
		if (n == 0) {
			print "0";
		} else {
			print s / n;
		}
	}' "${file}"
}

calc_min() {
	local file="$1"
	sort -n "${file}" | head -n 1
}

calc_max() {
	local file="$1"
	sort -n "${file}" | tail -n 1
}

pct_delta() {
	local newer="$1"
	local older="$2"
	awk -v n="${newer}" -v o="${older}" 'BEGIN { if (o == 0) { print 0 } else { print ((n - o) / o) * 100 } }'
}

echo "== xuan small alloc benchmark (A/B) =="
echo "bench=${BENCH_JS}"
echo "off=${XUAN_OFF}"
echo "on=${XUAN_ON}"
echo "repeat=${REPEAT}"
echo "params: warmup=${BENCH_WARMUP_ROUNDS} rounds=${BENCH_ROUNDS} batch=${BENCH_BATCH} size=[${BENCH_MIN_SIZE},${BENCH_MAX_SIZE}] live_set=${BENCH_LIVE_SET} retain_pct=${BENCH_RETAIN_PCT} gc_every=${BENCH_GC_EVERY}"
echo ""

for i in $(seq 1 "${REPEAT}"); do
	if (( i % 2 == 1 )); then
		run_one "off" "${XUAN_OFF}" "${i}"
		run_one "on" "${XUAN_ON}" "${i}"
	else
		run_one "on" "${XUAN_ON}" "${i}"
		run_one "off" "${XUAN_OFF}" "${i}"
	fi
done

off_ops_median="$(calc_median "${OFF_OPS_FILE}")"
on_ops_median="$(calc_median "${ON_OPS_FILE}")"
off_ops_mean="$(calc_mean "${OFF_OPS_FILE}")"
on_ops_mean="$(calc_mean "${ON_OPS_FILE}")"

off_rss_median="$(calc_median "${OFF_RSS_FILE}")"
on_rss_median="$(calc_median "${ON_RSS_FILE}")"

off_elapsed_median="$(calc_median "${OFF_ELAPSED_FILE}")"
on_elapsed_median="$(calc_median "${ON_ELAPSED_FILE}")"

ops_gain_pct="$(pct_delta "${on_ops_median}" "${off_ops_median}")"
rss_reduce_pct="$(awk -v off="${off_rss_median}" -v on="${on_rss_median}" 'BEGIN { if (off == 0) { print 0 } else { print ((off - on) / off) * 100 } }')"
elapsed_reduce_pct="$(awk -v off="${off_elapsed_median}" -v on="${on_elapsed_median}" 'BEGIN { if (off == 0) { print 0 } else { print ((off - on) / off) * 100 } }')"

echo ""
echo "== Summary (median first) =="
printf "ops/s   off median=%s mean=%s min=%s max=%s\n" "${off_ops_median}" "${off_ops_mean}" "$(calc_min "${OFF_OPS_FILE}")" "$(calc_max "${OFF_OPS_FILE}")"
printf "ops/s   on  median=%s mean=%s min=%s max=%s\n" "${on_ops_median}" "${on_ops_mean}" "$(calc_min "${ON_OPS_FILE}")" "$(calc_max "${ON_OPS_FILE}")"
printf "rss_kb  off median=%s mean=%s min=%s max=%s\n" "${off_rss_median}" "$(calc_mean "${OFF_RSS_FILE}")" "$(calc_min "${OFF_RSS_FILE}")" "$(calc_max "${OFF_RSS_FILE}")"
printf "rss_kb  on  median=%s mean=%s min=%s max=%s\n" "${on_rss_median}" "$(calc_mean "${ON_RSS_FILE}")" "$(calc_min "${ON_RSS_FILE}")" "$(calc_max "${ON_RSS_FILE}")"
printf "ms      off median=%s mean=%s min=%s max=%s\n" "${off_elapsed_median}" "$(calc_mean "${OFF_ELAPSED_FILE}")" "$(calc_min "${OFF_ELAPSED_FILE}")" "$(calc_max "${OFF_ELAPSED_FILE}")"
printf "ms      on  median=%s mean=%s min=%s max=%s\n" "${on_elapsed_median}" "$(calc_mean "${ON_ELAPSED_FILE}")" "$(calc_min "${ON_ELAPSED_FILE}")" "$(calc_max "${ON_ELAPSED_FILE}")"
echo ""
printf "delta_ops_median_pct=%s\n" "${ops_gain_pct}"
printf "delta_rss_median_pct=%s\n" "${rss_reduce_pct}"
printf "delta_elapsed_median_pct=%s\n" "${elapsed_reduce_pct}"
