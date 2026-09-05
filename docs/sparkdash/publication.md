# GitHub publication handoff

The user authorized publishing this project to a **new repository under mfellner after the project is finished**, with a distinctive name, cover image and README screenshots.

## Prepared presentation

- Project/repository name: **Sparklet** / `mfellner/sparklet`.
- Suggested description: `A tiny AMOLED companion for your DGX Spark cluster. Native ESP32-C6 dashboard powered by sparkDash.`
- Visibility: public, matching the requested publication.
- README: project introduction, generated cover, three real device photos, setup/build instructions and full documentation links.
- Assets and their provenance: [image notes](../assets/README.md).
- Existing firmware branding and setup SSID remain sparkDash/SparkDash for compatibility with the implemented user flow.

Published on 2026-09-05 at [mfellner/sparklet](https://github.com/mfellner/sparklet), with public visibility, main as the default branch, a cover illustration, three real-device photos and complete documentation. The [v1.0.0 release](https://github.com/mfellner/sparklet/releases/tag/v1.0.0) contains the verified ZIP and external checksum, built from source commit `07f10395f5173f8b37925464373e8f808d41ebe8`.

The bundle passed ZIP integrity, internal/external checksum and generated flash-reference checks. All three firmware binaries match the independently reproduced build. Normal firmware excludes QA controls and is installed on the device; the final 60-second check returned nine live samples, five nodes and no failures. No further user interaction is required.

## Release readiness

The bounded v1 acceptance checks are complete, including actual device HTTP failures, setup recovery, live data, memory and physical input timing. The 24-hour soak is excluded by request. See the [validation report](../sparkdash-validation.md) for evidence and limitations.

The public assets contain the generated cover and three historical device photographs. No setup credentials, flash backups, raw logs or local release directories are tracked. Reviewed history scans found no backup/log artifacts or high-confidence credential patterns; this is not a claim of exhaustive secret detection. Source attribution and the vendor license limitation remain recorded in firmware provenance.

Publication uses a new public repository, without replacing any existing repository. Release artifacts contain normal firmware, generated flashing arguments, pinned dependency/build metadata and checksums. Full flash backups remain private.
