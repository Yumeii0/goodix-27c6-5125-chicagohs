# Release checklist

Before publishing a GitHub release:

1. Run `./scripts/selftest.sh`.
2. Complete one fresh reset and clean installation on the supported device.
3. Verify enrollment persistence across an `fprintd` restart.
4. Verify the enrolled finger is accepted.
5. Verify a different finger is rejected.
6. Verify all three PAM attempts and password fallback.
7. Run `./scripts/status.sh` and confirm all required checks pass.
8. Confirm the archive contains no PSK, enrollment, log, raw image, build output, or cache.
9. Review reverse-engineering provenance and choose a public-source license.
10. Create release notes from `CHANGELOG.md`.

- Confirm the installer performs the ChicagoHS compatibility probe before system-driver installation.
- Confirm `windows/psk_extractor.ps1` contains no PSK and uses English prompts.
- Confirm `LICENSE` is present and README declares LGPL-2.1-or-later.
