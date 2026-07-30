# Goodix 27c6:5125 ChicagoHS Linux fingerprint driver

> [!IMPORTANT]
> This is an **AI-assisted reverse-engineering and interoperability project** developed and tested on a **Huawei MateBook D15**.
>
> This implementation is specifically for the **Goodix ChicagoHS sensor family/profile used by the tested device**. It is **not** a generic driver for every Goodix reader and it does not support Milan or other Goodix sensor families.
>
> The USB ID `27c6:5125` alone is not sufficient proof that another computer uses the same internal sensor family. The installer performs a strict ChicagoHS compatibility probe and aborts before installing the system driver when the expected hardware behavior is not detected.

Artificial intelligence was used to accelerate protocol analysis, implementation, testing, debugging, and documentation. Hardware validation and final engineering decisions were performed against real hardware.

The project provides experimental native Linux support for the tested Goodix USB fingerprint reader and integrates with:

- `libfprint`
- `fprintd`
- PAM
- systemd

It does not require Wine, a Windows virtual machine, a vendor DLL, or another proprietary runtime component after installation.

This project is not affiliated with, endorsed by, or supported by Goodix or Huawei.

## Tested hardware

Development and live testing were performed on:

```text
Laptop: Huawei MateBook D15
Fingerprint vendor: Goodix
USB ID: 27c6:5125
Sensor family: ChicagoHS
Expected chip ID: 0x2504
Expected internal profile: 0x0c
Capture geometry: 64 x 80 pixels
Raw frame size: 10240 bytes
```

Check the connected USB device with:

```bash
lsusb
```

The output must include:

```text
27c6:5125
```

This USB check is only the first filter. Another device can theoretically expose the same USB ID while using a different internal sensor family, firmware, protocol, geometry, TLS configuration, or fingerprint algorithm.

## ChicagoHS compatibility check

The installer does not rely only on the USB product ID.

After securely installing or reusing the device PSK, it temporarily stops `fprintd` and opens the sensor directly through the native implementation. Before making any system-driver changes, it verifies the behavior expected from the supported ChicagoHS configuration:

- USB ID `27c6:5125`
- Expected USB interfaces and endpoints
- Chip ID `0x2504`
- Expected internal profile `0x0c`
- ChicagoHS OTP layout and usable DAC calibration data
- ChicagoHS sensor initialization and configuration sequence
- Valid TLS-PSK sensor identity
- Expected `64 x 80` geometry
- Expected `10240`-byte raw frame size

No fingerprint image is captured during this compatibility probe.

A successful probe prints:

```text
GOODIX_BETA_CHICAGOHS_PROBE=PASS family:ChicagoHS
GOODIX_BETA_HARDWARE_PROFILE=PASS family:ChicagoHS
```

If the probe fails, installation stops before replacing the system `libfprint` configuration.

The device does not expose a standardized human-readable USB string that can be treated as a manufacturer-authenticated family certificate. The probe therefore verifies exact protocol and hardware compatibility with this ChicagoHS implementation rather than trusting the USB ID alone.

## Current status

The following functionality works on the tested Huawei MateBook D15:

- Native USB communication
- Encrypted TLS-PSK sensor sessions
- Strict ChicagoHS hardware compatibility probing
- Automatic finger-touch detection
- Automatic finger-lift detection
- Native image preprocessing
- Native fingerprint feature extraction
- Native enrollment
- Native fingerprint matching
- Persistent storage through `fprintd`
- AES-256-GCM encrypted native templates
- Twelve-scan varied enrollment
- Fingerprint verification through `fprintd`
- Optional fingerprint authentication for `sudo`
- Three fingerprint attempts before password fallback
- Generic English PAM prompt:

```text
Place your finger on the fingerprint reader
```

This remains beta software. It has not undergone production-scale biometric certification or independent security auditing.

## Unsupported hardware

Do not install this driver for:

- Goodix Milan sensors
- Other named Goodix sensor families
- Devices with another USB ID
- Devices that only share the Goodix vendor ID
- A `27c6:5125` device that fails the ChicagoHS compatibility probe

Adding support for another sensor family requires separate protocol analysis, hardware validation, capture handling, preprocessing, and calibration. Renaming a device or bypassing the compatibility probe does not make another family safe or compatible.

## Supported distributions

The automated installer currently targets:

- Arch Linux
- CachyOS
- Sufficiently compatible Arch-based distributions

The installer expects:

- `pacman`
- systemd
- PAM
- `fprintd`

Other Linux distributions may work after manual adaptation, but they are not currently supported by the automated installer.

## Requirements

You need:

- A supported Goodix ChicagoHS `27c6:5125` reader
- The correct 64-character hexadecimal PSK belonging to that sensor
- An Arch-compatible Linux installation
- Internet access during installation
- A normal user account with `sudo` access

Do not run the installer from an existing root shell.

## Obtaining the device PSK

The sensor uses a device-specific 32-byte PSK for its encrypted communication session. The Linux driver cannot communicate with the reader without the correct PSK.

The PSK is not included in this repository and must never be published.

### Recommended method

Extract the existing PSK from a working Windows installation on the same computer.

This method reads the PSK already used by the official Goodix Windows driver. It does not intentionally reset the sensor PSK and does not require deleting the existing Windows fingerprint enrollment.

### Windows preparation

1. Install or boot Windows on the computer containing the fingerprint reader.
2. Install the official Goodix fingerprint driver supplied by Windows Update or the laptop manufacturer.
3. Open Windows Settings.
4. Configure Windows Hello fingerprint authentication.
5. Enroll at least one finger.
6. Confirm that fingerprint authentication works in Windows.
7. Locate the following file:

```text
Goodix_Cache.bin
```

Its location can differ between Windows driver versions and laptop manufacturers.

To search for it, open PowerShell as Administrator and run:

```powershell
Get-ChildItem -Path C:\ -Filter Goodix_Cache.bin -File -Recurse -Force -ErrorAction SilentlyContinue
```

The search can take several minutes.

### Running the extraction script

The repository includes:

```text
windows/psk_extractor.ps1
```

Open PowerShell in the directory containing the script and run:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\psk_extractor.ps1
```

The execution-policy change applies only to the current PowerShell process.

The script asks for the full path of `Goodix_Cache.bin`.

Example:

```text
C:\path\to\Goodix_Cache.bin
```

When extraction succeeds, it creates:

```text
goodix_psk.txt
```

on the Windows desktop.

The file must contain exactly 64 hexadecimal characters representing the 32-byte PSK.

### Protecting the PSK

Treat the PSK as a secret.

Never:

- Upload it to GitHub
- Include it in a public issue
- Include it in a log
- Share it in screenshots
- Store it inside this repository
- Send it to another user
- Use a PSK extracted from another computer

Transfer the PSK to Linux through a private method.

During installation, provide only the 64 hexadecimal characters when requested.

After confirming that the Linux driver works:

1. Keep an encrypted offline copy for future installations.
2. Delete unnecessary plaintext copies.
3. Empty the Windows Recycle Bin.
4. Remove the PSK from clipboard history, temporary notes, and cloud-synchronized text files.

The extraction helper is intended for the tested Windows Goodix cache format. Other Goodix devices or substantially different driver versions can use another format.

## Installation

Extract or clone the complete repository and run:

```bash
./scripts/install.sh
```

The installer will:

1. Verify that USB device `27c6:5125` is connected.
2. Install the required Arch Linux packages.
3. Reuse an existing valid PSK or request one through hidden terminal input.
4. Validate that the PSK contains exactly 64 hexadecimal characters.
5. Store the PSK at:

```text
/etc/goodix-27c6-5125/psk.hex
```

6. Set its owner and permissions to:

```text
root:root
0600
```

7. Build the native implementation with strict compiler warnings.
8. Run the native offline self-tests.
9. Perform the strict ChicagoHS hardware compatibility probe.
10. Abort without installing the system driver if the probe fails.
11. Download and verify the pinned `libfprint 1.94.10` source.
12. Add the Goodix ChicagoHS driver to `libfprint`.
13. Build and install the custom runtime under:

```text
/opt/goodix-27c6-5125
```

14. Configure `fprintd.service` to load the custom `libfprint`.
15. Reuse an existing enrollment or start a new enrollment.
16. Verify the enrolled fingerprint.
17. Install the generic English PAM wrapper.
18. Configure three fingerprint attempts for `sudo`.
19. Preserve password authentication as a fallback.

The installer does not print the PSK value to its log.

## Twelve-scan enrollment

Enrollment asks you to place the selected finger on the sensor twelve times.

The program does **not** tell you which area of the finger to use for each scan. You must vary the placement yourself.

Across the twelve scans, use different areas and angles of the same finger. For example, include the center, sides, upper area, lower area, slightly rotated placements, partial edge placements, and the natural positions that commonly occur during daily use.

You may use any area in any order and repeat a position as many times as you prefer. However, using all twelve scans on exactly the same small central area can reduce recognition when only the side, top, or bottom of the finger touches the sensor later.

Use the same finger for all twelve scans.

After every accepted scan:

1. Remove the finger completely.
2. Leave the sensor empty briefly.
3. Place the same finger again using another position or angle.

A rejected scan does not advance enrollment. Remove the finger completely and try again.

## Common commands

### Check the installation

```bash
./scripts/status.sh
```

This checks:

- Supported USB device detection
- PSK file ownership and permissions
- Custom `libfprint` installation
- systemd configuration
- Active `fprintd` service
- D-Bus device discovery
- Existing fingerprint enrollment
- `sudo` PAM integration

It does not print the PSK or fingerprint-template contents.

### Run source self-tests

```bash
./scripts/selftest.sh
```

### Verify an enrolled finger

```bash
./scripts/verify.sh
```

### Enroll another finger

```bash
./scripts/enroll.sh
```

### Remove the custom driver

```bash
./scripts/uninstall.sh
```

This removes the custom runtime and managed PAM integration while retaining the installed PSK and encrypted fingerprint enrollment.

### Return to a fresh installation state

```bash
./scripts/fresh-reset.sh
```

This removes:

- The custom `libfprint`
- The systemd drop-in
- The managed `sudo` PAM configuration
- The Goodix fingerprint enrollment
- The installed device PSK
- Local build and download caches

Make sure you have another copy of the correct PSK before running a fresh reset.

The script does not remove unrelated system packages or personal files.

## Runtime locations

The installer uses the following locations:

```text
/opt/goodix-27c6-5125/
    Custom libfprint runtime and PAM wrapper

/etc/goodix-27c6-5125/psk.hex
    Device PSK stored as root:root with mode 0600

/etc/systemd/system/fprintd.service.d/
    Managed fprintd service configuration

/var/lib/fprint/
    fprintd fingerprint enrollment database

/var/lib/goodix-27c6-5125/beta/
    Saved PAM state used for safe recovery and uninstall

/etc/pam.d/sudo
    Managed sudo fingerprint authentication block
```

## Security model

### Sensor communication

Communication with the sensor uses the device PSK and an encrypted TLS session.

### Fingerprint data

The driver does not intentionally save the following to disk:

- Raw sensor frames
- Grayscale fingerprint images
- PNG or JPEG fingerprint images
- Plaintext native templates
- Plaintext native feature records

The native fingerprint template stored through `fprintd` is protected using:

- AES-256-GCM
- HKDF-SHA256
- Random salt
- Random nonce
- Authenticated GCM tag

The template-encryption key is derived from the device PSK.

### PSK storage

The PSK is stored at:

```text
/etc/goodix-27c6-5125/psk.hex
```

with:

```text
root:root
0600
```

Normal users should not be able to read it.

### Password fallback

Fingerprint authentication for `sudo` is an additional authentication method. Password authentication remains available.

Three fingerprint attempts are allowed before the authentication process continues to the normal password path.

### Security limitations

- A root-level attacker can read the installed PSK.
- An offline attacker who obtains both the PSK and encrypted fingerprint database may be able to decrypt the native template.
- The PSK is not currently protected by TPM sealing.
- Independent liveness or presentation-attack detection has not been validated.
- Fingerprint templates are permanent sensitive biometric data.
- A fingerprint cannot be replaced like a password.
- The current matcher threshold is experimental.
- The implementation has not undergone certified FAR or FRR testing.
- Fingerprint authentication does not replace full-disk encryption.
- Fingerprint authentication does not replace a strong account password.

See `SECURITY.md` for the detailed threat model.

## Repository contents

The repository contains the files required to build, test, install, maintain, and document the driver:

- Native USB communication
- TLS session implementation
- Goodix protocol handling
- ChicagoHS compatibility probe
- Sensor capture
- Finger-presence detection
- Image preprocessing
- Feature extraction
- Enrollment
- Fingerprint matching
- Template encryption
- Libfprint integration
- PAM integration
- Installation and recovery scripts
- Offline self-tests
- Windows PSK extraction helper
- Project documentation

The repository does not contain:

- Device PSKs
- User fingerprint templates
- Raw biometric captures
- Private installation logs
- Windows driver binaries
- Proprietary Goodix DLL files
- Wine runtime dependencies
- Historical development archives
- Old Phase packages
- Local build output
- Download caches

Some source comments include reverse-engineering function labels and algorithm names. These comments preserve technical traceability to the independently reimplemented behavior.

## Troubleshooting

Run:

```bash
./scripts/status.sh
```

Then inspect the newest log under:

```text
logs/
```

Installer and management scripts print final status markers in the following form:

```text
GOODIX_BETA_...=PASS
```

or:

```text
GOODIX_BETA_...=FAIL
```

A profile mismatch or failed compatibility probe ends with:

```text
GOODIX_BETA_CHICAGOHS_PROBE=FAIL
GOODIX_BETA_INSTALL=FAIL stage:chicagohs-compatibility-probe
```

Do not bypass this failure for Milan or another unsupported sensor family.

Additional recovery information is available in:

```text
docs/TROUBLESHOOTING.md
```

Before publishing a log, inspect it for private machine information.

Never publish:

- The device PSK
- Files from `/var/lib/fprint`
- Fingerprint-template contents
- Raw biometric data

## Development approach

This project is an AI-assisted reverse-engineering effort created to provide Linux interoperability for unsupported hardware.

Artificial intelligence was used as an engineering tool to accelerate:

- Reverse-engineering analysis
- Source-code implementation
- Test generation
- Log analysis
- Debugging
- Documentation

The implementation was validated through repeated testing on a real Huawei MateBook D15.

The project does not distribute or require:

- Decompiled vendor source code
- Goodix Windows binaries
- Proprietary Goodix DLL files
- Wine
- Windows runtime components

The Linux implementation was independently written based on observed device and software behavior.

Goodix and Huawei names may be trademarks of their respective owners.

## Project maturity

Current validation includes:

- Native USB communication
- Encrypted TLS-PSK sessions
- Strict ChicagoHS compatibility probing
- Offline protocol self-tests
- Native preprocessing and feature extraction
- Enrollment and matcher tests
- Libfprint integration
- Persistent encrypted templates
- Automatic finger-touch and finger-lift detection
- fprintd integration
- PAM integration
- `sudo` authentication
- Service-restart persistence
- Fresh-installation workflow
- Twelve-scan varied enrollment

This does not make the driver a certified biometric security product.

Testing has primarily been performed on one Huawei MateBook D15. Results on another computer, even with USB ID `27c6:5125`, are not guaranteed.

## License

The independently implemented project source is distributed under:

```text
LGPL-2.1-or-later
```

See the `LICENSE` file for the full GNU Lesser General Public License version 2.1 text. The “or later” option is declared by the project license identifier and source notices.

The license applies only to the independently implemented source code and project materials included in this repository. It does not grant rights to proprietary Goodix or Huawei software, firmware, drivers, DLL files, trademarks, or other third-party components.

Contributors must not submit:

- Decompiled proprietary source code
- Vendor binaries
- Proprietary DLL files
- Device PSKs
- Fingerprint templates
- Raw biometric captures
- Material they do not have permission to distribute

Third-party components retain their original licenses.
