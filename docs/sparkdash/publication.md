# GitHub publication handoff

The user authorized publishing this project to a **new repository under mfellner after the project is finished**, with a distinctive name, cover image and README screenshots.

## Prepared presentation

- Project/repository name: **Sparklet** / `mfellner/sparklet`.
- Suggested description: `A tiny AMOLED companion for your DGX Spark cluster. Native ESP32-C6 dashboard powered by sparkDash.`
- Visibility: public, matching the requested publication.
- README: project introduction, generated cover, three real device photos, setup/build instructions and full documentation links.
- Assets and their provenance: [image notes](../assets/README.md).
- Existing firmware branding and setup SSID remain sparkDash/SparkDash for compatibility with the implemented user flow.

On 2026-09-05, GitHub CLI authentication selected mfellner and the repository lookup for `mfellner/sparklet` returned 404. Availability must be checked again at creation time. No new GitHub repository has been created and no push has been performed. The local repository currently has no remote.

## Release readiness

The bounded v1 acceptance checks are complete, including actual device HTTP failures, setup recovery, live data, memory and physical input timing. The 24-hour soak is excluded by request. See the [validation report](../sparkdash-validation.md) for evidence and limitations.

The public assets contain the generated cover and three historical device photographs. No setup credentials, flash backups, raw logs or local release directories are tracked. Reviewed history scans found no backup/log artifacts or high-confidence credential patterns; this is not a claim of exhaustive secret detection. Source attribution and the vendor license limitation remain recorded in firmware provenance.

Publication uses a new public repository, without replacing any existing repository. Release artifacts contain normal firmware, generated flashing arguments, pinned dependency/build metadata and checksums. Full flash backups remain private.
