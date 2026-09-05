# Development guide

[Documentation index](README.md)

## Toolchain and checkout

The verified host is macOS with Python, uv, CMake and an ESP-IDF installation outside Git. The target is `esp32c6`, not ESP32-S3. Use ESP-IDF **v5.5.3**, exact revision `2c211b236707889e8400c4dc5644dd5c4ee071e0`.

For a new SDK installation, choose a new destination outside the repository:

```sh
mkdir -p "$HOME/esp"
git clone --branch v5.5.3 --recursive https://github.com/espressif/esp-idf.git "$HOME/esp/esp-idf-v5.5.3"
cd "$HOME/esp/esp-idf-v5.5.3"
git rev-parse HEAD
./install.sh esp32c6
. ./export.sh
```

Compare the printed revision with the pin above. The install command is the SDK's normal setup procedure; the existing verified installation is `/Users/max/esp/esp-idf-v5.5.3`. Do not clone over it. Activate `export.sh` in every new shell that runs IDF commands. Check `idf.py --version` before building. External SDK downloads/install prerequisites depend on the host; see the repository's [reference links](../references.md).

Return to the repository and build:

```sh
cd /Users/max/Developer/github/mfellner/esp32-playground
. /Users/max/esp/esp-idf-v5.5.3/export.sh
idf.py -C firmware/sparkdash build
```

For another clone location, replace the first path. The committed project defaults select the target; do not apply a different board's SDK configuration.

## Reproducible inputs

Track source, component manifests, `dependencies.lock`, `sdkconfig.defaults`, `partitions.csv` and provenance together. Managed components are resolved from the lock and kept outside tracked source. The original vendor revision is `294543798f1a44e2f2c4d2976522323f2beee11d` and the reference is `09_LVGL_V9_Test` under its IDF 5.5.3 examples.

`CONFIG_APP_REPRODUCIBLE_BUILD=y` removes time/date/path variability. Two separate build directories produced byte-identical app, bootloader and partition binaries for candidate 3376e53. That proves binary reproducibility for those inputs, not the candidate's runtime acceptance: its subsequent USB validation is unresolved.

Generated `sdkconfig` persists local choices and can override defaults. Inspect it before packaging; changing defaults alone does not necessarily replace an existing value. Dependency or SDK upgrades require their own review, rebuild and relevant device checks. Do not delete the committed lock merely to obtain newer components.

For a second build with the same exact configuration:

```sh
idf.py -C firmware/sparkdash -B firmware/sparkdash/build-repro -D SDKCONFIG="$PWD/firmware/sparkdash/sdkconfig" build
cmp firmware/sparkdash/build/sparkdash.bin firmware/sparkdash/build-repro/sparkdash.bin
cmp firmware/sparkdash/build/bootloader/bootloader.bin firmware/sparkdash/build-repro/bootloader/bootloader.bin
cmp firmware/sparkdash/build/partition_table/partition-table.bin firmware/sparkdash/build-repro/partition_table/partition-table.bin
```

No output and exit status zero from each `cmp` means equality. Archive timestamps/manifest creation time can differ; the ZIP itself is not claimed byte-reproducible.

## Normal versus validation builds

Normal configuration must have `CONFIG_SPARKDASH_TEST_COMMANDS` disabled. Validation firmware adds volatile USB test-server controls. Keep it in a separate ignored config/build directory:

```sh
cp firmware/sparkdash/sdkconfig firmware/sparkdash/sdkconfig.qa
idf.py -C firmware/sparkdash -B firmware/sparkdash/build-qa -D SDKCONFIG="$PWD/firmware/sparkdash/sdkconfig.qa" menuconfig
```

In the sparkDash validation menu enable test commands, save, then build with the same `-B` and `-D SDKCONFIG` arguments. Do not overwrite an existing QA config without reviewing it. All relative commands here assume the repository root.

Flash a QA build only for controlled tests, using its generated arguments:

```sh
idf.py -C firmware/sparkdash -B firmware/sparkdash/build-qa -D SDKCONFIG="$PWD/firmware/sparkdash/sdkconfig.qa" -p PORT flash
```

Discover and replace `PORT` first. The [hardware instructions](../interaction.md) and verified backup requirements apply. After testing, build/flash the normal project with test controls disabled. The release packager rejects test-enabled firmware.

## Making changes

- Put board-independent transformations in the shared core and test the real functions on the host.
- Preserve valid-zero versus missing semantics. A parser change needs malformed/limit and transactional-cache tests as well as normal examples.
- Avoid adding large automatic objects to task stacks. Check heap, largest allocation block and stack high-water marks on-device after memory-relevant changes.
- Update UI labels through the display-only punctuation formatter; do not alter IDs to solve missing fonts.
- Keep network work and NVS writes outside the LVGL lock. Cached selection must stay responsive during requests.
- Keep every application JSON parse on the controlled arena path.
- Add no persistent debug web service, credentials in logs, generic command shell, or DGX control endpoint as incidental debugging.
- Keep source commits reviewable at milestone boundaries. Record unexpected hardware behavior and the actual remedy, including unsuccessful attempts that affect interpretation.

`main/Kconfig.projbuild` owns the test-control option. `main/CMakeLists.txt` lists application translation units and required components. `components/board` is the only place that should need board-specific bus/panel configuration.

## Debugging and artifacts

Use [testing](testing.md) for runnable checks and [operations](operations.md) for serial output interpretation. Generated application outputs live under `firmware/sparkdash/build/`, including `.bin`, `.elf`, map files, project metadata and flash arguments. An ELF is useful for decoding program counters from the matching build; a different revision may resolve the same address incorrectly.

Ignored directories include `build/`, `build-qa/`, `build-repro/`, `managed_components/`, `logs/`, `backups/`, `.local/` and `releases/`. Treat ignored data as private rather than disposable: the factory backup and saved snapshots are recovery assets. Raw logs can reveal network identifiers; review excerpts before committing or sharing.

The vendor source's inspected revision has no top-level license grant. Provenance records that limitation and the adapted initialization facts. Managed dependencies retain their respective licenses. Do not describe the aggregate firmware as having a blanket permissive license that has not been established.
