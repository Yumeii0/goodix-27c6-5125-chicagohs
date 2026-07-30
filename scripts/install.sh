#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BETA_VERSION="$(cat "$ROOT/VERSION")"
LIBFPRINT_VERSION="1.94.10"
LIBFPRINT_BASE_URL="https://deb.debian.org/debian/pool/main/libf/libfprint"
CACHE_DIR="$ROOT/.cache"
WORK_DIR="$ROOT/.work"
SOURCE_DIR="$WORK_DIR/libfprint-source"
BUILD_DIR="$WORK_DIR/libfprint-build"
LOG_DIR="$ROOT/logs"
STAMP="$(date +%Y%m%d-%H%M%S)"
LOG_FILE="$LOG_DIR/install-$STAMP.log"
PSK_PATH="/etc/goodix-27c6-5125/psk.hex"
INSTALL_ROOT="/opt/goodix-27c6-5125"
LIB_INSTALL_BASE="$INSTALL_ROOT/libfprint"
CURRENT_LINK="$LIB_INSTALL_BASE/current"
DROPIN_DIR="/etc/systemd/system/fprintd.service.d"
DROPIN_PATH="$DROPIN_DIR/50-goodix5125.conf"
DROPIN_MARKER="# managed-by-goodix5125-beta"
OLD_DROPIN_MARKER="# managed-by-goodix5125-phase8"
PAM_SOURCE="$ROOT/pam/pam_goodix5125_fprintd_english.c"
PAM_BUILD="$WORK_DIR/pam_goodix5125_fprintd.so"
PAM_INSTALL_DIR="$INSTALL_ROOT/pam"
PAM_INSTALL="$PAM_INSTALL_DIR/pam_goodix5125_fprintd.so"
PAM_HELPER="$ROOT/tools/gx5125-pam-fingerprint-smoke.py"
PAM_TARGET="/etc/pam.d/sudo"
PAM_VENDOR="/usr/lib/pam.d/sudo"
PAM_TEST_SERVICE="goodix5125-beta-fingerprint-test"
PAM_TEST_PATH="/etc/pam.d/$PAM_TEST_SERVICE"
PAM_STATE_DIR="/var/lib/goodix-27c6-5125/beta"
PAM_STATE_FILE="$PAM_STATE_DIR/sudo.before-goodix"
PAM_STATE_META="$PAM_STATE_DIR/sudo.before-goodix.meta"
OLD_PAM_STATE_FILE="/var/lib/goodix-27c6-5125/pam/sudo.pre-goodix5125-part8"
OLD_PAM_STATE_META="/var/lib/goodix-27c6-5125/pam/sudo.pre-goodix5125-part8.meta"
BETA_BEGIN="# managed-by-goodix5125-beta begin"
BETA_END="# managed-by-goodix5125-beta end"
OLD_BEGIN="# managed-by-goodix5125-phase8-part7 begin"
OLD_END="# managed-by-goodix5125-phase8-part7 end"
PAM_LINE="auth      sufficient      $PAM_INSTALL max-tries=3 timeout=30"
LOGIN_USER="${SUDO_USER:-$(id -un)}"

SYSTEM_ROLLBACK_ARMED=0
PAM_ROLLBACK_ARMED=0
WRAPPER_ROLLBACK_ARMED=0
PREVIOUS_CURRENT_TARGET=""
PREVIOUS_DROPIN_EXISTS=0
PREVIOUS_WRAPPER_EXISTS=0
NEW_VERSION_DIR=""
TEST_SERVICE_CREATED=0
GUARD_DIR=""
GUARD_PID=""
GUARD_DISARM=""
GUARD_TRIGGER=""
CURRENT_PAM_BACKUP="$WORK_DIR/sudo.current.before-beta"

mkdir -p "$CACHE_DIR" "$WORK_DIR" "$LOG_DIR"
exec > >(tee "$LOG_FILE") 2>&1

fail() {
  printf 'GOODIX_BETA_INSTALL=FAIL stage:%s\n' "$1"
  printf 'GOODIX_BETA_LOG=%s\n' "$LOG_FILE"
  exit 1
}

cleanup_test_service() {
  if [[ "$TEST_SERVICE_CREATED" -eq 1 ]]; then
    sudo rm -f -- "$PAM_TEST_PATH" >/dev/null 2>&1 || true
    TEST_SERVICE_CREATED=0
  fi
}

rollback() {
  local rc=$?
  set +e
  cleanup_test_service

  if [[ "$PAM_ROLLBACK_ARMED" -eq 1 ]]; then
    [[ -n "$GUARD_TRIGGER" ]] && : > "$GUARD_TRIGGER" 2>/dev/null || true
    if [[ -f "$CURRENT_PAM_BACKUP" ]]; then
      sudo -n cp -a -- "$CURRENT_PAM_BACKUP" "$PAM_TARGET" >/dev/null 2>&1 || true
    fi
    [[ -n "$GUARD_PID" ]] && wait "$GUARD_PID" >/dev/null 2>&1 || true
  fi

  if [[ "$WRAPPER_ROLLBACK_ARMED" -eq 1 ]]; then
    if [[ "$PREVIOUS_WRAPPER_EXISTS" -eq 1 && -f "$WORK_DIR/pam-wrapper.previous" ]]; then
      sudo -n install -o root -g root -m 0644 "$WORK_DIR/pam-wrapper.previous" "$PAM_INSTALL" >/dev/null 2>&1 || true
    else
      sudo -n rm -f -- "$PAM_INSTALL" >/dev/null 2>&1 || true
    fi
  fi

  if [[ "$SYSTEM_ROLLBACK_ARMED" -eq 1 ]]; then
    sudo -n systemctl stop fprintd.service >/dev/null 2>&1 || true
    if [[ "$PREVIOUS_DROPIN_EXISTS" -eq 1 && -f "$WORK_DIR/dropin.previous" ]]; then
      sudo -n install -D -m 0644 "$WORK_DIR/dropin.previous" "$DROPIN_PATH" >/dev/null 2>&1 || true
    else
      sudo -n rm -f -- "$DROPIN_PATH" >/dev/null 2>&1 || true
      sudo -n rmdir "$DROPIN_DIR" >/dev/null 2>&1 || true
    fi
    if [[ -n "$PREVIOUS_CURRENT_TARGET" ]]; then
      sudo -n ln -sfn "$PREVIOUS_CURRENT_TARGET" "$LIB_INSTALL_BASE/.current.rollback.$$" >/dev/null 2>&1 || true
      sudo -n mv -Tf "$LIB_INSTALL_BASE/.current.rollback.$$" "$CURRENT_LINK" >/dev/null 2>&1 || true
    else
      sudo -n rm -f -- "$CURRENT_LINK" >/dev/null 2>&1 || true
    fi
    sudo -n systemctl daemon-reload >/dev/null 2>&1 || true
    sudo -n systemctl start fprintd.service >/dev/null 2>&1 || true
  fi

  [[ -n "$GUARD_DIR" ]] && rm -rf -- "$GUARD_DIR" >/dev/null 2>&1 || true
  exit "$rc"
}
trap rollback EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

wait_default_device() {
  local count output device
  for count in $(seq 1 100); do
    output="$(sudo busctl --system call net.reactivated.Fprint \
      /net/reactivated/Fprint/Manager net.reactivated.Fprint.Manager \
      GetDefaultDevice 2>/dev/null || true)"
    device="$(awk -F'"' 'NF >= 2 { print $2 }' <<<"$output")"
    if [[ "$device" == /net/reactivated/Fprint/Device/* ]]; then
      printf '%s' "$device"
      return 0
    fi
    sleep 0.1
  done
  return 1
}

bus_owner_pid() {
  sudo busctl --system call org.freedesktop.DBus /org/freedesktop/DBus \
    org.freedesktop.DBus GetConnectionUnixProcessID s net.reactivated.Fprint \
    2>/dev/null | awk '$1 == "u" { print $2 }'
}

valid_psk_file() {
  sudo python3 - "$PSK_PATH" <<'PY' >/dev/null 2>&1
from pathlib import Path
import re
import sys
path = Path(sys.argv[1])
try:
    value = path.read_text(encoding="ascii").strip()
except OSError:
    raise SystemExit(1)
raise SystemExit(0 if re.fullmatch(r"[0-9A-Fa-f]{64}", value) else 1)
PY
}

install_psk() {
  local psk_value
  if valid_psk_file; then
    printf 'A valid device PSK is already installed at %s. It will be reused.\n' "$PSK_PATH"
    return 0
  fi

  printf '%s\n' 'The device PSK is required to establish the encrypted sensor session.'
  printf '%s\n' 'The value will not be displayed, logged, or passed as a command-line argument.'
  while true; do
    read -r -s -p 'Enter the 64-character hexadecimal device PSK: ' psk_value
    printf '%s\n' ''
    if [[ "$psk_value" =~ ^[0-9A-Fa-f]{64}$ ]]; then
      break
    fi
    printf '%s\n' 'Invalid PSK. Enter exactly 64 hexadecimal characters.' >&2
  done
  psk_value="${psk_value,,}"
  sudo install -d -o root -g root -m 0700 "$(dirname "$PSK_PATH")"
  printf '%s\n' "$psk_value" | sudo sh -c '
    set -eu
    destination="$1"
    umask 077
    temporary="${destination}.tmp.$$"
    cat > "$temporary"
    chown root:root "$temporary"
    chmod 0600 "$temporary"
    mv -f "$temporary" "$destination"
  ' sh "$PSK_PATH"
  unset psk_value
  valid_psk_file || fail "psk-storage-validation"
  printf 'GOODIX_BETA_PSK=PASS path:%s owner:root mode:0600 value_logged:0\n' "$PSK_PATH"
}

probe_chicagohs_compatibility() {
  local was_active=0
  local probe_output
  local probe_rc

  if sudo systemctl is-active --quiet fprintd.service; then
    was_active=1
  fi
  sudo systemctl stop fprintd.service >/dev/null 2>&1 || true

  if probe_output="$(sudo env LC_ALL=C "$ROOT/build/gx5125-chicagohs-probe" "$PSK_PATH" 2>&1)"; then
    probe_rc=0
  else
    probe_rc=$?
  fi
  printf '%s\n' "$probe_output"

  if [[ "$was_active" -eq 1 ]]; then
    sudo systemctl start fprintd.service >/dev/null 2>&1 || true
  fi
  [[ "$probe_rc" -eq 0 ]] || fail "chicagohs-compatibility-probe"
  printf '%s\n' 'GOODIX_BETA_HARDWARE_PROFILE=PASS family:ChicagoHS chip_id:0x2504 sensor_profile:0x0c geometry:64x80 biometric_capture:0'
}

printf '%s\n' '=== GOODIX 27C6:5125 BETA INSTALLER ==='
printf 'GOODIX_BETA_VERSION=%s\n' "$BETA_VERSION"
printf '%s\n' 'GOODIX_BETA_SCOPE=dependencies:1 psk:secure hardware-profile:ChicagoHS-strict driver:system fprintd:1 enrollment:12-varied sudo-pam:3-tries prompt:generic-English password-fallback:1'

[[ "$LOGIN_USER" != root ]] || fail "run-from-normal-user"
[[ -r /etc/os-release ]] || fail "os-release-missing"
# shellcheck disable=SC1091
source /etc/os-release
if [[ "${ID:-}" != arch && " ${ID_LIKE:-} " != *" arch "* ]]; then
  fail "unsupported-distribution-arch-required"
fi

command -v sudo >/dev/null 2>&1 || fail "missing-command-sudo"
sudo -v || fail "sudo-authentication"

printf '%s\n' 'Installing required Arch/CachyOS packages...'
sudo pacman -S --needed --noconfirm \
  base-devel meson ninja python curl xz pkgconf \
  glib2 glib2-devel libgusb libusb openssl fprintd \
  cairo gobject-introspection pam usbutils dbus >/dev/null || fail "dependency-install"

for cmd in curl tar meson ninja python3 pkg-config make cc sudo glib-mkenums \
           systemctl busctl journalctl install sha256sum readlink stat lsusb \
           fprintd-list fprintd-enroll fprintd-verify timeout; do
  command -v "$cmd" >/dev/null 2>&1 || fail "missing-command-$cmd"
done
for module in glib-2.0 gio-unix-2.0 gobject-2.0 gusb libusb-1.0 openssl; do
  pkg-config --exists "$module" || fail "missing-pkgconfig-$module"
done
lsusb -d 27c6:5125 | grep -Fq '27c6:5125' || fail "supported-device-not-connected"
printf '%s\n' 'GOODIX_BETA_DEPENDENCIES=PASS distribution:arch-compatible usb_device:27c6:5125 profile_check:pending'

install_psk

printf '%s\n' 'Building and testing the native driver...'
make -C "$ROOT" clean all test || fail "native-build-or-selftest"
printf '%s\n' 'GOODIX_BETA_NATIVE=PASS strict_warnings:1 selftests:6 chicagohs_probe:built'
probe_chicagohs_compatibility

DSC="$CACHE_DIR/libfprint_${LIBFPRINT_VERSION}-1.dsc"
TARBALL="$CACHE_DIR/libfprint_${LIBFPRINT_VERSION}.orig.tar.xz"
DSC_URL="$LIBFPRINT_BASE_URL/$(basename "$DSC")"
TARBALL_URL="$LIBFPRINT_BASE_URL/$(basename "$TARBALL")"

curl --fail --location --proto '=https' --tlsv1.2 --output "$DSC.tmp" "$DSC_URL" || fail "libfprint-dsc-download"
mv -f "$DSC.tmp" "$DSC"
EXPECTED_SHA="$(awk -v name="$(basename "$TARBALL")" '
  /^Checksums-Sha256:/ { active=1; next }
  active && /^[^[:space:]]/ { active=0 }
  active && $3 == name && length($1) == 64 && $1 ~ /^[0-9a-f]+$/ { print $1; exit }
' "$DSC")"
[[ "$EXPECTED_SHA" =~ ^[0-9a-f]{64}$ ]] || fail "libfprint-sha256-metadata"
if [[ ! -s "$TARBALL" ]] || [[ "$(sha256sum "$TARBALL" | awk '{print $1}')" != "$EXPECTED_SHA" ]]; then
  rm -f -- "$TARBALL.tmp"
  curl --fail --location --proto '=https' --tlsv1.2 --output "$TARBALL.tmp" "$TARBALL_URL" || fail "libfprint-download"
  mv -f "$TARBALL.tmp" "$TARBALL"
fi
printf '%s  %s\n' "$EXPECTED_SHA" "$TARBALL" | sha256sum -c - >/dev/null || fail "libfprint-sha256"

rm -rf "$SOURCE_DIR" "$BUILD_DIR" "$WORK_DIR/extract"
mkdir -p "$WORK_DIR/extract"
tar -xJf "$TARBALL" -C "$WORK_DIR/extract" || fail "libfprint-extract"
mapfile -t source_roots < <(find "$WORK_DIR/extract" -mindepth 1 -maxdepth 1 -type d -print)
[[ "${#source_roots[@]}" -eq 1 ]] || fail "libfprint-source-layout"
mv "${source_roots[0]}" "$SOURCE_DIR"
grep -Fq "version: '$LIBFPRINT_VERSION'" "$SOURCE_DIR/meson.build" || fail "libfprint-version"
python3 "$ROOT/scripts/apply-libfprint-overlay.py" "$SOURCE_DIR" "$ROOT" || fail "libfprint-overlay"
meson setup "$BUILD_DIR" "$SOURCE_DIR" \
  -Ddrivers=goodix5125 \
  -Ddoc=false \
  -Dgtk-examples=false \
  -Dintrospection=false \
  -Dudev_rules=disabled \
  -Dudev_hwdb=disabled \
  -Dinstalled-tests=false || fail "libfprint-meson"
ninja -C "$BUILD_DIR" libfprint/libfprint-2.so.2.0.0 || fail "libfprint-build"
BUILT_LIB="$BUILD_DIR/libfprint/libfprint-2.so.2.0.0"
[[ -s "$BUILT_LIB" ]] || fail "libfprint-output"
LIB_SHA="$(sha256sum "$BUILT_LIB" | awk '{print $1}')"
VERSION_ID="$LIBFPRINT_VERSION-${LIB_SHA:0:12}"
NEW_VERSION_DIR="$LIB_INSTALL_BASE/$VERSION_ID"
printf 'GOODIX_BETA_LIBFPRINT=PASS version:%s sha256:%s driver:goodix5125\n' "$LIBFPRINT_VERSION" "$LIB_SHA"

sudo -v || fail "sudo-refresh-before-system-change"
if sudo test -e "$DROPIN_PATH"; then
  if ! sudo grep -Fq "$DROPIN_MARKER" "$DROPIN_PATH" && ! sudo grep -Fq "$OLD_DROPIN_MARKER" "$DROPIN_PATH"; then
    fail "foreign-fprintd-dropin"
  fi
  sudo cat "$DROPIN_PATH" > "$WORK_DIR/dropin.previous"
  PREVIOUS_DROPIN_EXISTS=1
fi
PREVIOUS_CURRENT_TARGET="$(sudo readlink "$CURRENT_LINK" 2>/dev/null || true)"
SYSTEM_ROLLBACK_ARMED=1
sudo systemctl stop fprintd.service >/dev/null 2>&1 || true
sudo install -d -m 0755 "$LIB_INSTALL_BASE" "$NEW_VERSION_DIR/lib"
sudo install -m 0644 "$BUILT_LIB" "$NEW_VERSION_DIR/lib/libfprint-2.so.2.0.0"
sudo ln -sfn libfprint-2.so.2.0.0 "$NEW_VERSION_DIR/lib/libfprint-2.so.2"
sudo ln -sfn "$VERSION_ID" "$LIB_INSTALL_BASE/.current.new.$$"
sudo mv -Tf "$LIB_INSTALL_BASE/.current.new.$$" "$CURRENT_LINK"
cat > "$WORK_DIR/50-goodix5125.conf" <<DROPIN
$DROPIN_MARKER
[Service]
Environment="LD_LIBRARY_PATH=$CURRENT_LINK/lib"
Environment="GOODIX5125_PSK_PATH=$PSK_PATH"
DROPIN
sudo install -D -m 0644 "$WORK_DIR/50-goodix5125.conf" "$DROPIN_PATH"
sudo systemctl daemon-reload || fail "systemd-daemon-reload"
START_EPOCH="$(date +%s)"
sudo systemctl restart fprintd.service || fail "fprintd-start"
DEVICE_PATH="$(wait_default_device)" || fail "fprintd-device-discovery"
DAEMON_PID="$(bus_owner_pid || true)"
[[ "$DAEMON_PID" =~ ^[0-9]+$ ]] || fail "fprintd-daemon-pid"
LIB_REAL="$(sudo readlink -f "$CURRENT_LINK/lib/libfprint-2.so.2")"
sudo grep -Fq "$LIB_REAL" "/proc/$DAEMON_PID/maps" || fail "fprintd-custom-library-not-loaded"
sudo busctl --system call net.reactivated.Fprint "$DEVICE_PATH" \
  net.reactivated.Fprint.Device Claim s "$LOGIN_USER" >/dev/null || fail "driver-open"
sleep 0.4
JOURNAL="$(sudo journalctl -u fprintd.service --since "@$START_EPOCH" --no-pager -o cat 2>/dev/null || true)"
grep -Fq 'GOODIX5125_DRIVER_OPEN=PASS' <<<"$JOURNAL" || fail "driver-open-log"
grep -Fq 'GOODIX5125_DRIVER_CLOSE=PASS' <<<"$JOURNAL" || fail "driver-close-log"
printf 'GOODIX_BETA_SYSTEM_DRIVER=PASS device:%s daemon_pid:%s persistent:1\n' "$DEVICE_PATH" "$DAEMON_PID"

LIST_OUTPUT="$(sudo env LC_ALL=C fprintd-list "$LOGIN_USER" 2>&1 || true)"
printf '%s\n' "$LIST_OUTPUT"
FINGER="$(grep -Eo -- '(left|right)-(thumb|index-finger|middle-finger|ring-finger|little-finger)' <<<"$LIST_OUTPUT" | head -n 1 || true)"
if [[ -z "$FINGER" ]]; then
  "$ROOT/scripts/enroll.sh" || fail "finger-enrollment"
  LIST_OUTPUT="$(sudo env LC_ALL=C fprintd-list "$LOGIN_USER" 2>&1 || true)"
  FINGER="$(grep -Eo -- '(left|right)-(thumb|index-finger|middle-finger|ring-finger|little-finger)' <<<"$LIST_OUTPUT" | head -n 1 || true)"
fi
[[ -n "$FINGER" ]] || fail "enrolled-finger-not-found"
"$ROOT/scripts/verify.sh" "$FINGER" || fail "finger-verification"
printf 'GOODIX_BETA_FINGERPRINT=PASS user:%s finger:%s encrypted_template:1\n' "$LOGIN_USER" "$FINGER"

[[ -e /usr/lib/security/pam_fprintd.so ]] || fail "pam-fprintd-module-missing"
REAL_PAM_TARGET="$(readlink -f -- /usr/lib/security/pam_fprintd.so)" || fail "pam-fprintd-resolve"
[[ "$REAL_PAM_TARGET" == /usr/lib/security/* ]] || fail "pam-fprintd-untrusted-path"
[[ "$(stat -Lc '%u' "$REAL_PAM_TARGET")" == 0 ]] || fail "pam-fprintd-not-root-owned"
[[ $((8#$(stat -Lc '%a' "$REAL_PAM_TARGET") & 8#022)) -eq 0 ]] || fail "pam-fprintd-writable"
cc -std=c17 -O2 -fPIC -shared -Wall -Wextra -Wpedantic -Werror \
  "$PAM_SOURCE" -o "$PAM_BUILD" -ldl -pthread || fail "pam-wrapper-build"
python3 -m py_compile "$PAM_HELPER" || fail "pam-helper-syntax"
if sudo test -f "$PAM_INSTALL"; then
  sudo cat "$PAM_INSTALL" > "$WORK_DIR/pam-wrapper.previous"
  PREVIOUS_WRAPPER_EXISTS=1
fi
WRAPPER_ROLLBACK_ARMED=1
sudo install -d -o root -g root -m 0755 "$PAM_INSTALL_DIR"
sudo install -o root -g root -m 0644 "$PAM_BUILD" "$PAM_INSTALL"

sudo install -o root -g root -m 0644 /dev/null "$PAM_TEST_PATH"
sudo tee "$PAM_TEST_PATH" >/dev/null <<PAMEOF
#%PAM-1.0
auth      required        $PAM_INSTALL max-tries=3 timeout=30
account   required        pam_permit.so
PAMEOF
TEST_SERVICE_CREATED=1
printf '%s\n' ''
printf '%s\n' 'Fingerprint authentication preflight:'
printf '%s\n' '1. Keep the sensor empty until prompted.'
printf '%s\n' '2. Place any enrolled finger on the sensor.'
printf '%s\n' '3. Do not type a password and do not press Enter.'
sleep 3
PRE_OUTPUT="$WORK_DIR/pam-preflight-output"
set +e
timeout --foreground 40s python3 "$PAM_HELPER" --service "$PAM_TEST_SERVICE" --user "$LOGIN_USER" 2>&1 | tee "$PRE_OUTPUT"
PRE_RC=${PIPESTATUS[0]}
set -e
[[ "$PRE_RC" -eq 0 ]] || fail "pam-wrapper-preflight"
grep -Fqx 'Place your finger on the fingerprint reader' "$PRE_OUTPUT" || fail "generic-English-prompt"
cleanup_test_service

if sudo test -f "$PAM_TARGET"; then
  PAM_SOURCE_PATH="$PAM_TARGET"
  PAM_TARGET_EXISTED=1
elif sudo test -f "$PAM_VENDOR"; then
  PAM_SOURCE_PATH="$PAM_VENDOR"
  PAM_TARGET_EXISTED=0
else
  fail "sudo-pam-source-missing"
fi
sudo cat "$PAM_SOURCE_PATH" > "$CURRENT_PAM_BACKUP"
PAM_MODE="$(sudo stat -c '%a' "$PAM_SOURCE_PATH")"
PAM_UID="$(sudo stat -c '%u' "$PAM_SOURCE_PATH")"
PAM_GID="$(sudo stat -c '%g' "$PAM_SOURCE_PATH")"
[[ "$PAM_UID" -eq 0 && "$PAM_GID" -eq 0 ]] || fail "sudo-pam-ownership"

PAM_CANDIDATE="$WORK_DIR/sudo.candidate"
PAM_STRIPPED="$WORK_DIR/sudo.stripped"
python3 - "$CURRENT_PAM_BACKUP" "$PAM_CANDIDATE" "$PAM_STRIPPED" \
  "$BETA_BEGIN" "$BETA_END" "$OLD_BEGIN" "$OLD_END" "$PAM_LINE" <<'PY'
from pathlib import Path
import sys

source, candidate, stripped = map(Path, sys.argv[1:4])
beta_begin, beta_end, old_begin, old_end, pam_line = sys.argv[4:9]
pairs = {beta_begin: beta_end, old_begin: old_end}
lines = source.read_text(encoding="utf-8").splitlines(keepends=True)
out = []
active_end = None
for line in lines:
    marker = line.rstrip("\r\n")
    if active_end is not None:
        if marker == active_end:
            active_end = None
        continue
    if marker in pairs:
        active_end = pairs[marker]
        continue
    out.append(line)
if active_end is not None:
    raise SystemExit("unterminated managed PAM block")
for line in out:
    if "pam_fprintd.so" in line or "pam_goodix5125_fprintd.so" in line:
        raise SystemExit("unmanaged fingerprint PAM line already exists")
stripped.write_text("".join(out), encoding="utf-8")
insert_at = 1 if out and out[0].rstrip("\r\n") == "#%PAM-1.0" else 0
block = beta_begin + "\n" + pam_line + "\n" + beta_end + "\n"
out.insert(insert_at, block)
candidate.write_text("".join(out), encoding="utf-8")
PY

grep -Fq 'system-auth' "$PAM_CANDIDATE" || fail "password-fallback-missing"
grep -Fqx "$PAM_LINE" "$PAM_CANDIDATE" || fail "pam-candidate-line"
sudo install -d -o root -g root -m 0700 "$PAM_STATE_DIR"
if ! sudo test -f "$PAM_STATE_FILE"; then
  if sudo test -f "$OLD_PAM_STATE_FILE" && sudo test -f "$OLD_PAM_STATE_META"; then
    sudo cp -a -- "$OLD_PAM_STATE_FILE" "$PAM_STATE_FILE"
    sudo cp -a -- "$OLD_PAM_STATE_META" "$PAM_STATE_META"
  else
    sudo install -o root -g root -m 0600 "$PAM_STRIPPED" "$PAM_STATE_FILE"
    printf 'target_existed=%d\n' "$PAM_TARGET_EXISTED" | sudo tee "$PAM_STATE_META" >/dev/null
    sudo chown root:root "$PAM_STATE_META"
    sudo chmod 0600 "$PAM_STATE_META"
  fi
fi
sudo install -o root -g root -m "0$PAM_MODE" "$PAM_CANDIDATE" "$PAM_TARGET"
PAM_ROLLBACK_ARMED=1

GUARD_DIR="$(mktemp -d)"
chmod 0700 "$GUARD_DIR"
GUARD_DISARM="$GUARD_DIR/disarm"
GUARD_TRIGGER="$GUARD_DIR/restore-now"
sudo bash -s -- "$GUARD_DISARM" "$GUARD_TRIGGER" "$CURRENT_PAM_BACKUP" "$PAM_TARGET" <<'ROOT_GUARD' &
set -euo pipefail
disarm="$1"
trigger="$2"
backup="$3"
target="$4"
for _ in $(seq 1 180); do
  [[ -e "$disarm" ]] && exit 0
  [[ -e "$trigger" ]] && break
  sleep 0.5
done
cp -a -- "$backup" "$target"
ROOT_GUARD
GUARD_PID=$!

printf '%s\n' ''
printf '%s\n' 'Live sudo fingerprint test:'
printf '%s\n' '1. Keep the sensor empty until sudo prompts for a fingerprint.'
printf '%s\n' '2. Place any enrolled finger on the sensor.'
printf '%s\n' '3. If a scan is rejected, lift the finger completely and try again.'
printf '%s\n' '4. Three fingerprint scans are allowed before password fallback.'
printf '%s\n' '5. Do not type the password during this test.'
sleep 3
sudo -k
timeout --foreground 110s sudo -v || fail "live-sudo-fingerprint"
sudo -n true || fail "sudo-timestamp"
: > "$GUARD_DISARM"
wait "$GUARD_PID" >/dev/null 2>&1 || true
GUARD_PID=""
PAM_ROLLBACK_ARMED=0
WRAPPER_ROLLBACK_ARMED=0
SYSTEM_ROLLBACK_ARMED=0
trap - EXIT INT TERM
printf '%s\n' 'GOODIX_BETA_SUDO=PASS attempts:3 timeout:30 prompt:generic-English password_fallback:1'
printf 'GOODIX_BETA_INSTALL=PASS version:%s user:%s finger:%s hardware_family:ChicagoHS chip_id:0x2504 sensor_profile:0x0c system_driver:1 encrypted_template:1 enrollment_samples:12 sudo_fingerprint:1\n' "$BETA_VERSION" "$LOGIN_USER" "$FINGER"
printf 'GOODIX_BETA_LOG=%s\n' "$LOG_FILE"
