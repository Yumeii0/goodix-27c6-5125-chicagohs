# Troubleshooting

## Start with status

```bash
./scripts/status.sh
```

The command checks the USB device, PSK permissions, installed custom library, systemd drop-in, `fprintd`, enrolled fingers, and `sudo` PAM integration without printing secret or biometric contents.

## Device is not found

Confirm that the exact USB ID is present:

```bash
lsusb -d 27c6:5125
```

The installer intentionally rejects other device IDs.

## `fprintd` cannot see the device

Restart the service and run status again:

```bash
sudo systemctl restart fprintd.service
./scripts/status.sh
```

Inspect recent service output:

```bash
sudo journalctl -u fprintd.service -b --no-pager
```

Do not post PSKs, enrollment files, or raw biometric data in bug reports.

## Fingerprint is rejected at an edge

The enrollment process uses twelve guided placements. Re-enroll the finger and deliberately follow the edge, lower, and rotation instructions. Keep the matcher threshold unchanged unless a controlled calibration test supports a change.

Three PAM attempts are available. After a rejection, lift the finger completely, leave the sensor empty briefly, and place it again with a more centered or previously enrolled edge position.

## `sudo` falls back to password

This is expected after three rejected or timed-out scans. Password fallback is intentionally retained.

Check that the managed PAM block exists:

```bash
./scripts/status.sh
```

## PSK is rejected

The PSK must contain exactly 64 hexadecimal characters. The installed file must be root-owned and mode `0600`.

Never paste a PSK into a public issue or command-line argument.

## Recover the stock system behavior

Keep enrollment and PSK but remove the custom driver and PAM integration:

```bash
./scripts/uninstall.sh
```

Remove everything specific to this driver, including enrollment and PSK:

```bash
./scripts/fresh-reset.sh
```

Have the PSK available before using fresh reset.

## ChicagoHS compatibility probe fails

The installer performs a direct hardware probe after the PSK is available and before changing the installed `libfprint` runtime.

Expected success markers:

```text
GOODIX_BETA_CHICAGOHS_PROBE=PASS family:ChicagoHS
GOODIX_BETA_HARDWARE_PROFILE=PASS family:ChicagoHS
```

A failure can mean:

- The device PSK is incorrect.
- Another process still owns the USB interfaces.
- The reader has USB ID `27c6:5125` but is not compatible with the tested ChicagoHS protocol.
- The reader uses Milan or another unsupported Goodix family.
- The chip ID, OTP layout, TLS identity, or capture geometry differs from the tested hardware.

Do not bypass the probe or edit the expected identifiers to force installation. Another Goodix family needs a separately validated driver implementation.
