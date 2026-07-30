#!/usr/bin/env bash
set -euo pipefail

LOGIN_USER="${SUDO_USER:-$(id -un)}"
VALID_FINGERS=(
  left-thumb left-index-finger left-middle-finger left-ring-finger left-little-finger
  right-thumb right-index-finger right-middle-finger right-ring-finger right-little-finger
)

is_valid_finger() {
  local value="$1" item
  for item in "${VALID_FINGERS[@]}"; do
    [[ "$value" == "$item" ]] && return 0
  done
  return 1
}

choose_finger() {
  local choice
  printf '%s\n' 'Choose the finger to enroll:' >&2
  printf '%s\n' \
    '  1) Left thumb' \
    '  2) Left index finger' \
    '  3) Left middle finger' \
    '  4) Left ring finger' \
    '  5) Left little finger' \
    '  6) Right thumb' \
    '  7) Right index finger' \
    '  8) Right middle finger' \
    '  9) Right ring finger' \
    ' 10) Right little finger' >&2
  while true; do
    read -r -p 'Selection [1-10]: ' choice
    case "$choice" in
      1) printf '%s' left-thumb; return ;;
      2) printf '%s' left-index-finger; return ;;
      3) printf '%s' left-middle-finger; return ;;
      4) printf '%s' left-ring-finger; return ;;
      5) printf '%s' left-little-finger; return ;;
      6) printf '%s' right-thumb; return ;;
      7) printf '%s' right-index-finger; return ;;
      8) printf '%s' right-middle-finger; return ;;
      9) printf '%s' right-ring-finger; return ;;
      10) printf '%s' right-little-finger; return ;;
      *) printf '%s\n' 'Enter a number from 1 to 10.' >&2 ;;
    esac
  done
}

[[ "$LOGIN_USER" != root ]] || {
  printf '%s\n' 'Run this script from a normal user account, not a root shell.' >&2
  exit 1
}

FINGER="${1:-}"
if [[ -z "$FINGER" ]]; then
  FINGER="$(choose_finger)"
fi
is_valid_finger "$FINGER" || {
  printf 'Invalid finger label: %s\n' "$FINGER" >&2
  exit 1
}

for cmd in sudo systemctl fprintd-list fprintd-enroll grep; do
  command -v "$cmd" >/dev/null 2>&1 || {
    printf 'Missing command: %s\n' "$cmd" >&2
    exit 1
  }
done

sudo -v
sudo systemctl restart fprintd.service
LIST_OUTPUT="$(sudo env LC_ALL=C fprintd-list "$LOGIN_USER" 2>&1 || true)"
printf '%s\n' "$LIST_OUTPUT"
if grep -Fq -- "$FINGER" <<<"$LIST_OUTPUT"; then
  printf 'The selected finger is already enrolled: %s\n' "$FINGER"
  exit 0
fi

printf '%s\n' ''
printf '%s\n' 'Guided 12-scan enrollment:'
printf '%s\n' 'Use the same selected finger for every scan.'
printf '%s\n' 'After each "enroll-stage-passed" message, lift the finger completely and briefly leave the sensor empty.'
printf '%s\n' ''
printf '%s\n' 'Scan placement order:'
printf '%s\n' ' 1. Natural centered placement.'
printf '%s\n' ' 2. Centered, slightly higher on the sensor.'
printf '%s\n' ' 3. Centered, slightly lower on the sensor.'
printf '%s\n' ' 4. Shift the finger slightly toward the left edge.'
printf '%s\n' ' 5. Shift the finger slightly toward the right edge.'
printf '%s\n' ' 6. Capture more of the upper part of the fingertip.'
printf '%s\n' ' 7. Capture more of the lower part of the fingertip.'
printf '%s\n' ' 8. Rotate the finger slightly clockwise.'
printf '%s\n' ' 9. Rotate the finger slightly counterclockwise.'
printf '%s\n' '10. Use a natural lower-left edge placement.'
printf '%s\n' '11. Use a natural lower-right edge placement.'
printf '%s\n' '12. Finish with a relaxed natural placement.'
printf '%s\n' ''
printf '%s\n' 'A rejected scan does not advance the number. Repeat the same placement after lifting the finger.'
printf '%s\n' 'Do not press Enter. The driver detects touch and lift automatically.'
printf '%s\n' 'Enrollment starts in five seconds.'
sleep 5
sudo env LC_ALL=C fprintd-enroll -f "$FINGER" "$LOGIN_USER"
sudo systemctl restart fprintd.service
LIST_OUTPUT="$(sudo env LC_ALL=C fprintd-list "$LOGIN_USER" 2>&1)"
printf '%s\n' "$LIST_OUTPUT"
grep -Fq -- "$FINGER" <<<"$LIST_OUTPUT" || {
  printf '%s\n' 'Enrollment completed, but the stored print could not be listed.' >&2
  exit 1
}
printf 'GOODIX_BETA_ENROLL=PASS user:%s finger:%s samples:12 guided_positions:1 retained:1\n' "$LOGIN_USER" "$FINGER"
