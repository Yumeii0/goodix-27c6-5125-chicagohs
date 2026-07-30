#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
LOGIN_USER="${SUDO_USER:-$(id -un)}"
PSK_PATH="/etc/goodix-27c6-5125/psk.hex"
INSTALL_ROOT="/opt/goodix-27c6-5125"
DROPIN_PATH="/etc/systemd/system/fprintd.service.d/50-goodix5125.conf"
PAM_TARGET="/etc/pam.d/sudo"
BETA_BEGIN="# managed-by-goodix5125-beta begin"
BETA_END="# managed-by-goodix5125-beta end"
OLD_BEGIN="# managed-by-goodix5125-phase8-part7 begin"
OLD_END="# managed-by-goodix5125-phase8-part7 end"
LOG_DIR="$ROOT/logs"
STAMP="$(date +%Y%m%d-%H%M%S)"
LOG_FILE="$LOG_DIR/fresh-reset-$STAMP.log"

mkdir -p "$LOG_DIR"
exec > >(tee "$LOG_FILE") 2>&1

fail() {
  printf 'GOODIX_BETA_FRESH_RESET=FAIL stage:%s\n' "$1"
  printf 'GOODIX_BETA_LOG=%s\n' "$LOG_FILE"
  exit 1
}

[[ "$LOGIN_USER" != root ]] || fail "run-from-normal-user"
for cmd in sudo systemctl fprintd-list fprintd-delete grep rm test; do
  command -v "$cmd" >/dev/null 2>&1 || fail "missing-command-$cmd"
done

printf '%s\n' '=== GOODIX 27C6:5125 FRESH RESET ==='
printf '%s\n' 'This removes the installed Goodix driver, sudo fingerprint integration, the enrolled fingerprint for this device, and the stored device PSK.'
printf '%s\n' 'The source directory is kept. System packages are not removed.'
printf '%s\n' 'You must have the 64-character device PSK available before reinstalling.'
printf '%s\n' ''
read -r -p 'Type RESET to continue: ' confirmation
[[ "$confirmation" == RESET ]] || fail "confirmation-declined"
unset confirmation
sudo -v || fail "sudo-authentication"

sudo systemctl restart fprintd.service >/dev/null 2>&1 || true
LIST_BEFORE="$(sudo env LC_ALL=C fprintd-list "$LOGIN_USER" 2>&1 || true)"
printf '%s\n' "$LIST_BEFORE"
if grep -Eq -- '(left|right)-(thumb|index-finger|middle-finger|ring-finger|little-finger)' <<<"$LIST_BEFORE"; then
  sudo env LC_ALL=C fprintd-delete "$LOGIN_USER" >/dev/null 2>&1 || fail "fingerprint-delete"
  LIST_AFTER="$(sudo env LC_ALL=C fprintd-list "$LOGIN_USER" 2>&1 || true)"
  if grep -Eq -- '(left|right)-(thumb|index-finger|middle-finger|ring-finger|little-finger)' <<<"$LIST_AFTER"; then
    fail "fingerprint-delete-verification"
  fi
  printf 'GOODIX_BETA_FRESH_RESET_PRINT=PASS user:%s removed:1\n' "$LOGIN_USER"
else
  printf 'GOODIX_BETA_FRESH_RESET_PRINT=PASS user:%s removed:0 already_absent:1\n' "$LOGIN_USER"
fi

"$ROOT/scripts/uninstall.sh" || fail "driver-uninstall"
sudo rm -f -- "$PSK_PATH"
sudo rmdir /etc/goodix-27c6-5125 >/dev/null 2>&1 || true

rm -rf -- "$ROOT/build" "$ROOT/.work" "$ROOT/.cache"

sudo test ! -e "$INSTALL_ROOT" || fail "install-root-still-present"
sudo test ! -e "$DROPIN_PATH" || fail "dropin-still-present"
sudo test ! -e "$PSK_PATH" || fail "psk-still-present"
if sudo test -f "$PAM_TARGET"; then
  if sudo grep -Fq "$BETA_BEGIN" "$PAM_TARGET" ||
     sudo grep -Fq "$BETA_END" "$PAM_TARGET" ||
     sudo grep -Fq "$OLD_BEGIN" "$PAM_TARGET" ||
     sudo grep -Fq "$OLD_END" "$PAM_TARGET" ||
     sudo grep -Fq 'pam_goodix5125_fprintd.so' "$PAM_TARGET"; then
    fail "pam-managed-block-still-present"
  fi
fi

sudo systemctl daemon-reload >/dev/null 2>&1 || fail "systemd-daemon-reload"
sudo systemctl restart fprintd.service >/dev/null 2>&1 || true

printf '%s\n' 'GOODIX_BETA_FRESH_RESET=PASS driver_removed:1 sudo_pam_removed:1 fingerprint_removed:1 psk_removed:1 build_cache_removed:1 source_retained:1 packages_retained:1'
printf '%s\n' 'Run ./scripts/install.sh to exercise the real clean-install path.'
printf 'GOODIX_BETA_LOG=%s\n' "$LOG_FILE"
