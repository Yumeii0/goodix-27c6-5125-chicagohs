# Security

## Intended use

This project provides convenience authentication for the tested Goodix 27c6:5125 ChicagoHS sensor implementation. Milan and other Goodix sensor families are outside the supported threat and compatibility model. It should be used together with a strong account password and, where appropriate, full-disk encryption.

## Stored secrets and biometric data

### Device PSK

The device PSK is stored at:

```text
/etc/goodix-27c6-5125/psk.hex
```

Expected ownership and permissions:

```text
root:root 0600
```

The installer reads the value without terminal echo and does not pass it as a command-line argument.

### Fingerprint enrollment

`fprintd` stores the serialized `FpPrint` under its system database, normally below `/var/lib/fprint/`.

The native template payload is encrypted using:

- AES-256-GCM
- HKDF-SHA256 key derivation from the device PSK
- Random salt
- Random nonce
- An authenticated GCM tag

The implementation does not intentionally persist raw frames, grayscale images, extracted feature arrays, or plaintext native templates.

## Threat model

The encryption protects a copied enrollment file when the attacker does not also possess the PSK.

It does not protect against an attacker who can read the full root filesystem and obtain both:

- `/var/lib/fprint/` enrollment data
- `/etc/goodix-27c6-5125/psk.hex`

Full-disk encryption helps against offline disk access. TPM sealing is not currently implemented.

## Authentication limitations

- The matcher and threshold are experimental.
- Production-scale false-accept and false-reject validation has not been performed.
- Presentation-attack detection and liveness resistance have not been independently validated.
- PAM fingerprint authentication is configured as a sufficient method with password fallback.
- Three fingerprint attempts increase usability but also provide three matching opportunities per PAM transaction.

## Reporting a vulnerability

Do not publish PSKs, enrollment files, raw biometric captures, or exploitable security details in a public issue.

After the GitHub repository is created, use a private GitHub security advisory for sensitive reports. Non-sensitive build or compatibility bugs can use the normal issue tracker.
