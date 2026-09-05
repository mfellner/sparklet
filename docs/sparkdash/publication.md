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

## Completion dependency

Do not publish merely because these presentation assets are ready. Finish the existing v1 goal, including its bounded physical acceptance checks, with the explicit 24-hour soak exclusion. The unresolved candidate USB response failure and pending checks are recorded in the [validation report](../sparkdash-validation.md). A physical power-cycle and screen report are currently needed to resume that work.

## Publication sequence

1. Complete v1 and update the validation report and README status to the actual final result. Replace historical screenshots with newer verified photos if available; do not retouch their metrics to simulate verification.
2. Review the tracked tree and history for credentials, raw device data and unintentional files. Backup/log/release/local-artifact directory paths were absent from history at preparation time. Public photo copies have no EXIF/XMP/IPTC metadata; retain that property for replacement photos.
3. Review source/dependency attribution and the existing vendor license limitation in provenance. Do not add a blanket license badge or claim a license grant that is not established.
4. Commit the final reviewed source and presentation; verify a clean checkout and the final release bundle.
5. Recheck name availability. Create a new public repository under mfellner, add its remote and push the intended source history. Do not replace or force-push an existing repository if the name is taken.
6. Set the repository description and relevant topics such as `esp32`, `esp32-c6`, `lvgl`, `dgx-spark`, `dashboard` and `waveshare`.
7. Verify the published default branch, README cover/gallery, documentation links and source visibility. Attach only the reviewed firmware bundle/checksums if publishing a release; never upload full device snapshots or raw logs.
8. Return the actual repository URL and final validation status to the user.

The publication request is already authorized. The delay is the user's completion condition and unresolved hardware evidence, not an additional approval requirement.
