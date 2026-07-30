#!/usr/bin/env bash
set -euo pipefail

LOGIN_USER="${SUDO_USER:-$(id -un)}"
FINGER="${1:-}"

for cmd in sudo systemctl fprintd-list fprintd-verify grep sed; do
  command -v "$cmd" >/dev/null 2>&1 || {
    printf 'Missing command: %s\n' "$cmd" >&2
    exit 1
  }
done

[[ "$LOGIN_USER" != root ]] || {
  printf '%s\n' 'Run this script from a normal user account, not a root shell.' >&2
  exit 1
}

sudo -v
sudo systemctl restart fprintd.service
LIST_OUTPUT="$(sudo env LC_ALL=C fprintd-list "$LOGIN_USER" 2>&1)"
printf '%s\n' "$LIST_OUTPUT"
if [[ -z "$FINGER" ]]; then
  FINGER="$(grep -Eo -- '(left|right)-(thumb|index-finger|middle-finger|ring-finger|little-finger)' <<<"$LIST_OUTPUT" | head -n 1 || true)"
fi
[[ -n "$FINGER" ]] || {
  printf '%s\n' 'No enrolled finger was found.' >&2
  exit 1
}

printf '%s\n' ''
printf '%s\n' 'Verification instructions:'
printf '%s\n' '1. Keep the fingerprint sensor empty until verification starts.'
printf '%s\n' '2. When prompted, place the enrolled finger on the sensor.'
printf '%s\n' '3. Do not press Enter.'
sleep 3
sudo env LC_ALL=C fprintd-verify -f "$FINGER" "$LOGIN_USER"
printf 'GOODIX_BETA_VERIFY=PASS user:%s finger:%s\n' "$LOGIN_USER" "$FINGER"
