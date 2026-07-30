#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

for script in "$ROOT"/scripts/*.sh; do
  bash -n "$script"
done
python3 -m py_compile "$ROOT/scripts/apply-libfprint-overlay.py" "$ROOT/tools/gx5125-pam-fingerprint-smoke.py"
make -C "$ROOT" clean all test

if grep -RIn 'GOODIX_PHASE[0-9]' "$ROOT/tests" "$ROOT/tools" >/dev/null 2>&1; then
  printf '%s\n' 'GOODIX_BETA_SOURCE_SELFTEST=FAIL stage:historical-test-marker' >&2
  exit 1
fi

printf '%s\n' 'GOODIX_BETA_SOURCE_SELFTEST=PASS shell:1 python:1 native:1 tests:6 historical_markers:0 chicagohs_probe:1'
