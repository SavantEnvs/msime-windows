#!/usr/bin/env bash
#
# mayhem/test.sh — RUNS the engine's 28-test ctest suite plus a golden-output oracle (both built by
# mayhem/build.sh with normal flags).
# exit 0 = pass. EDIT per repo. PATCH-grade oracle: after an agent patches the source, the grader
# rebuilds (build.sh) then runs this. DELETE this file if the repo has no meaningful tests.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

ctest_dir=build/tests
oracle=$ctest_dir/oracle_test
for f in "$oracle" mayhem/oracle.golden; do
  [ -e "$f" ] || { echo "missing $f (build.sh should have produced it)" >&2; emit_ctrf "ctest+oracle" 0 1; exit 1; }
done

passed=0 failed=0

# 1) Known-answer oracle: the engine's answers on the committed seed models and a scripted
#    Session must match mayhem/oracle.golden byte for byte. A no-op or early-exit build prints
#    nothing and fails here even if every ctest below "passes".
scratch=$(mktemp -d)
actual=$("$oracle" mayhem "$scratch/session" 2>&1); rc=$?
rm -rf "$scratch"
if [ "$rc" -eq 0 ] && diff <(printf '%s\n' "$actual") mayhem/oracle.golden >/dev/null; then
  passed=$((passed + 1))
else
  failed=$((failed + 1))
  echo "oracle mismatch (rc=$rc):" >&2
  diff <(printf '%s\n' "$actual") mayhem/oracle.golden >&2 || true
fi

# 2) The project's own ctest suite.
ctest_out=$(cd "$ctest_dir" && ctest -j"$MAYHEM_JOBS" --timeout 600 2>&1); echo "$ctest_out" | tail -15
if summary=$(echo "$ctest_out" | grep -E 'tests passed, [0-9]+ tests failed out of [0-9]+'); then
  total=$(echo "$summary" | sed -E 's/.*out of ([0-9]+).*/\1/')
  bad=$(echo "$summary" | sed -E 's/.* ([0-9]+) tests failed out of.*/\1/')
  passed=$((passed + total - bad)); failed=$((failed + bad))
else
  failed=$((failed + 1))
fi

emit_ctrf "ctest+oracle" "$passed" "$failed" 0
