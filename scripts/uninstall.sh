#!/usr/bin/env bash
set -euo pipefail

INSTALL_ROOT="/opt/goodix-27c6-5125"
LIB_INSTALL_BASE="$INSTALL_ROOT/libfprint"
PAM_INSTALL="$INSTALL_ROOT/pam/pam_goodix5125_fprintd.so"
DROPIN_DIR="/etc/systemd/system/fprintd.service.d"
DROPIN_PATH="$DROPIN_DIR/50-goodix5125.conf"
DROPIN_MARKER="# managed-by-goodix5125-beta"
OLD_DROPIN_MARKER="# managed-by-goodix5125-phase8"
PAM_TARGET="/etc/pam.d/sudo"
PAM_STATE_DIR="/var/lib/goodix-27c6-5125/beta"
PAM_STATE_FILE="$PAM_STATE_DIR/sudo.before-goodix"
PAM_STATE_META="$PAM_STATE_DIR/sudo.before-goodix.meta"
LOGIN_USER="${SUDO_USER:-$(id -un)}"

for cmd in sudo systemctl grep rm rmdir; do
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

if sudo test -f "$PAM_STATE_META"; then
  if sudo grep -Fqx 'target_existed=1' "$PAM_STATE_META"; then
    sudo test -f "$PAM_STATE_FILE" || {
      printf '%s\n' 'The saved sudo PAM configuration is missing.' >&2
      exit 1
    }
    sudo cp -a -- "$PAM_STATE_FILE" "$PAM_TARGET"
  else
    sudo rm -f -- "$PAM_TARGET"
  fi
else
  printf '%s\n' 'No beta PAM state was found; sudo PAM was left unchanged.'
fi

if sudo test -e "$DROPIN_PATH"; then
  if ! sudo grep -Fq "$DROPIN_MARKER" "$DROPIN_PATH" && ! sudo grep -Fq "$OLD_DROPIN_MARKER" "$DROPIN_PATH"; then
    printf '%s\n' 'Refusing to remove an unmanaged fprintd service drop-in.' >&2
    exit 1
  fi
fi

sudo systemctl stop fprintd.service >/dev/null 2>&1 || true
sudo rm -f -- "$DROPIN_PATH" "$PAM_INSTALL"
sudo rmdir "$DROPIN_DIR" >/dev/null 2>&1 || true
sudo rm -rf -- "$LIB_INSTALL_BASE"
sudo rmdir "$INSTALL_ROOT/pam" >/dev/null 2>&1 || true
sudo rmdir "$INSTALL_ROOT" >/dev/null 2>&1 || true
sudo rm -rf -- "$PAM_STATE_DIR"
sudo systemctl daemon-reload
sudo systemctl start fprintd.service >/dev/null 2>&1 || true

printf '%s\n' 'GOODIX_BETA_UNINSTALL=PASS driver_removed:1 sudo_pam_restored:1 password_fallback:1'
printf '%s\n' 'The encrypted fingerprint record and /etc/goodix-27c6-5125/psk.hex were intentionally retained.'
