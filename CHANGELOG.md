# Changelog

## 0.3.1-beta

- Reworked the README to identify the project as AI-assisted reverse engineering tested on a Huawei MateBook D15.
- Added a prominent warning that the implementation supports only the ChicagoHS family and not Milan or other Goodix families.
- Added a strict pre-install ChicagoHS compatibility probe for chip ID `0x2504`, profile `0x0c`, OTP/configuration behavior, TLS identity, and `64 x 80` geometry.
- Added the Windows PSK extraction helper under `windows/psk_extractor.ps1` with English prompts.
- Added the LGPL-2.1 license text and declared `LGPL-2.1-or-later`.
- Clarified that the twelve enrollment scans are not position-guided by the program; the user must vary placement manually.

## 0.3.0-beta

- Added GitHub-ready project documentation.
- Added `scripts/status.sh` for installed-state diagnostics.
- Added `scripts/selftest.sh` for source-tree validation.
- Renamed historical Phase self-test markers to stable beta marker names.
- Documented runtime paths, security model, fresh reset, uninstall, and source-directory deletion behavior.
- Kept the 12-position guided enrollment and three-attempt generic-English PAM flow.

## 0.2.0-beta

- Added guided 12-scan enrollment for center, edge, lower, and rotated placements.
- Added `fresh-reset.sh` for real clean-install testing.
- Retained the fixed matcher threshold while improving enrollment coverage.

## 0.1.1-beta

- Restored enrollment template serialization, deserialization, CRC integrity checking, and round-trip tests.
- Completed the first clean beta installation on the tested system.

## 0.1.0-beta

- Created the first clean source tree and unified installer.
