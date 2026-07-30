#!/usr/bin/env bash
set -euo pipefail

INSTALL_ROOT="/opt/goodix-27c6-5125"
CURRENT_LINK="$INSTALL_ROOT/libfprint/current"
LIB_PATH="$CURRENT_LINK/lib/libfprint-2.so.2"
PSK_PATH="/etc/goodix-27c6-5125/psk.hex"
DROPIN_PATH="/etc/systemd/system/fprintd.service.d/50-goodix5125.conf"
PAM_TARGET="/etc/pam.d/sudo"
PAM_WRAPPER="$INSTALL_ROOT/pam/pam_goodix5125_fprintd.so"
LOGIN_USER="${SUDO_USER:-$(id -un)}"
FAILURES=0

pass() { printf 'GOODIX_BETA_STATUS_%s=PASS %s\n' "$1" "$2"; }
fail() { printf 'GOODIX_BETA_STATUS_%s=FAIL %s\n' "$1" "$2"; FAILURES=$((FAILURES + 1)); }

printf '%s\n' '=== GOODIX 27C6:5125 BETA STATUS ==='

if command -v lsusb >/dev/null 2>&1 && lsusb -d 27c6:5125 | grep -Fq '27c6:5125'; then
  pass DEVICE 'vid:0x27c6 pid:0x5125 connected:1'
else
  fail DEVICE 'vid:0x27c6 pid:0x5125 connected:0'
fi

if sudo test -f "$PSK_PATH"; then
  PSK_UID="$(sudo stat -Lc '%u' "$PSK_PATH" 2>/dev/null || true)"
  PSK_MODE="$(sudo stat -Lc '%a' "$PSK_PATH" 2>/dev/null || true)"
  if [[ "$PSK_UID" == 0 && "$PSK_MODE" == 600 ]]; then
    pass PSK "path:$PSK_PATH owner:root mode:0600 value_printed:0"
  else
    fail PSK "path:$PSK_PATH owner_uid:${PSK_UID:-unknown} mode:${PSK_MODE:-unknown} value_printed:0"
  fi
else
  fail PSK "path:$PSK_PATH present:0"
fi

if sudo test -L "$CURRENT_LINK" && sudo test -s "$LIB_PATH"; then
  LIB_REAL="$(sudo readlink -f "$LIB_PATH" 2>/dev/null || true)"
  pass LIBFPRINT "current:$CURRENT_LINK library:${LIB_REAL:-unknown}"
else
  fail LIBFPRINT "current:$CURRENT_LINK installed:0"
fi

if sudo test -f "$DROPIN_PATH" && sudo grep -Fq '# managed-by-goodix5125-beta' "$DROPIN_PATH"; then
  pass SYSTEMD "dropin:$DROPIN_PATH managed:1"
else
  fail SYSTEMD "dropin:$DROPIN_PATH managed:0"
fi

if sudo systemctl is-active --quiet fprintd.service; then
  pass FPRINTD 'service_active:1'
else
  fail FPRINTD 'service_active:0'
fi

DEVICE_OUTPUT="$(sudo busctl --system call net.reactivated.Fprint /net/reactivated/Fprint/Manager net.reactivated.Fprint.Manager GetDefaultDevice 2>/dev/null || true)"
DEVICE_PATH="$(awk -F'"' 'NF >= 2 {print $2; exit}' <<<"$DEVICE_OUTPUT")"
if [[ "$DEVICE_PATH" == /net/reactivated/Fprint/Device/* ]]; then
  pass DBUS "device:$DEVICE_PATH"
else
  fail DBUS 'device:not-found'
fi

LIST_OUTPUT="$(sudo env LC_ALL=C fprintd-list "$LOGIN_USER" 2>&1 || true)"
if grep -Fq 'Goodix 27c6:5125 ChicagoHS native' <<<"$LIST_OUTPUT"; then
  pass PROFILE 'family:ChicagoHS chip_id:0x2504 sensor_profile:0x0c driver_name_verified:1'
else
  fail PROFILE 'family:ChicagoHS driver_name_verified:0'
fi
FINGER_COUNT="$(grep -Eoc -- '(left|right)-(thumb|index-finger|middle-finger|ring-finger|little-finger)' <<<"$LIST_OUTPUT" || true)"
if [[ "$FINGER_COUNT" =~ ^[0-9]+$ && "$FINGER_COUNT" -gt 0 ]]; then
  pass ENROLLMENT "user:$LOGIN_USER enrolled_fingers:$FINGER_COUNT"
else
  fail ENROLLMENT "user:$LOGIN_USER enrolled_fingers:0"
fi

if sudo test -f "$PAM_WRAPPER" && sudo test -f "$PAM_TARGET" && \
   sudo grep -Fq 'pam_goodix5125_fprintd.so max-tries=3 timeout=30' "$PAM_TARGET"; then
  pass PAM 'sudo_enabled:1 attempts:3 timeout:30 password_fallback_expected:1'
else
  fail PAM 'sudo_enabled:0'
fi

if [[ "$FAILURES" -eq 0 ]]; then
  printf '%s\n' 'GOODIX_BETA_STATUS=PASS'
else
  printf 'GOODIX_BETA_STATUS=FAIL failed_checks:%d\n' "$FAILURES"
  exit 1
fi
