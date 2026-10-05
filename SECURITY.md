# Security Policy

## Supported versions

This repository is under active development. Security fixes are applied to the current `main` branch.

## Reporting a vulnerability

Please **do not open a public issue** for a vulnerability that could expose users, devices, credentials, connected systems or supply-chain dependencies.

Use GitHub's private vulnerability reporting feature if it is enabled for this repository. If private reporting is not available, contact the maintainer through the contact information on the GitHub profile and clearly mark the message as a security report.

Include:

- affected firmware commit
- hardware / board revision
- reproduction steps
- impact
- relevant logs
- whether physical access is required
- suggested mitigation, if known

## Scope

Examples of relevant reports:

- unsafe memory behavior reachable from malformed audio files
- malicious SD-card content causing crashes or corruption
- dependency or build-pipeline compromise
- USB/MIDI security issues once those features exist
- unsafe parsing of metadata or future network input

Ordinary playback bugs, UI defects and performance issues should use the standard issue tracker.

## Disclosure

Please allow reasonable time for investigation and a fix before public disclosure.
