#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_DIR="${ROOT_DIR}/deps"

usage() {
	cat <<'EOF'
Usage:
  ./scripts/reset-deps-checkout.sh [--dry-run] [dep_name ...]

Description:
  Run `git checkout -f` for dependency directories under deps/.
  If no dep_name is given, all git repos under deps/ are processed.

Examples:
  ./scripts/reset-deps-checkout.sh
  ./scripts/reset-deps-checkout.sh --dry-run
  ./scripts/reset-deps-checkout.sh quickjs libuv mimalloc
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" || "${1:-}" == "help" ]]; then
	usage
	exit 0
fi

DRY_RUN=0
if [[ "${1:-}" == "--dry-run" || "${1:-}" == "-n" ]]; then
	DRY_RUN=1
	shift
fi

if [[ ! -d "${DEPS_DIR}" ]]; then
	echo "Error: deps directory not found: ${DEPS_DIR}" >&2
	exit 1
fi

declare -a targets=()
if [[ "$#" -eq 0 ]]; then
	while IFS= read -r dep_path; do
		targets+=("${dep_path}")
	done < <(find "${DEPS_DIR}" -mindepth 1 -maxdepth 1 -type d | sort)
else
	for dep_name in "$@"; do
		dep_path="${DEPS_DIR}/${dep_name}"
		if [[ ! -d "${dep_path}" ]]; then
			echo "Skip: not found ${dep_path}"
			continue
		fi
		targets+=("${dep_path}")
	done
fi

if [[ "${#targets[@]}" -eq 0 ]]; then
	echo "No dependency directories to process."
	exit 0
fi

for dep_path in "${targets[@]}"; do
	dep_name="$(basename "${dep_path}")"
	if ! git -C "${dep_path}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
		echo "Skip: ${dep_name} (not a git work tree)"
		continue
	fi
	dep_top="$(git -C "${dep_path}" rev-parse --show-toplevel 2>/dev/null || true)"
	if [[ -z "${dep_top}" || "$(cd "${dep_path}" && pwd -P)" != "$(cd "${dep_top}" && pwd -P)" ]]; then
		echo "Skip: ${dep_name} (belongs to parent repo)"
		continue
	fi

	echo "Reset: ${dep_name}"
	if [[ "${DRY_RUN}" -eq 1 ]]; then
		echo "  (dry-run) git -C ${dep_path} checkout -f"
	else
		git -C "${dep_path}" checkout -f
	fi
done

echo "Done."
