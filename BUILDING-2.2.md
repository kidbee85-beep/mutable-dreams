# Building Mutable Dreams 2.2

The `v2.2` tag contains the complete browsable application, DSP, platform and presets. The older `BUILDING.md` supplies general toolchain and host-test background; its 2.1 package names and example release number are historical. Use the 2.2 commands below for this release.

## Qualified release package

- Device build: **23F2FA0E**, identity **FM-1_710020200**.
- Package SHA-256: `25df8be7b5d87806739e3481f0d8af371762e29051e4153cd86ce5bdae882b68`.
- Effective application/platform/preset sources are unchanged from development commit `a5e0e7f95b4d38365a271edbe3700ea61b2ea4b9`; the public tag adds release documentation and web packaging.
- The published device package was retained without recompilation. A new build needs its own hardware timing qualification; identical source does not guarantee identical cache placement or a bit-reproducible binary.

## Device build

Use JieLi `jieli-linux-toolchains-20260730.1`, the AC79NN SDK V1.2.1 (2023-12-13) and Python 3.11 or newer. The toolchain and SDK are external dependencies, not redistributed in this repository. The vendor helpers and hash checks are documented in `BUILDING.md`.

```sh
export JIELI_TOOLCHAIN=/absolute/path/to/jieli-linux-toolchains-20260730.1
export AC79_SDK=/absolute/path/to/AC79/sdk
export PYTHON=/absolute/path/to/python3
"$PYTHON" base/tools/build.py app --release 2.2
```

The private `tools/build_device.sh --fm1-test-candidate` wrapper produces a **2.2.0-dev** test package, despite its inherited 2.1 wording in the usage message. It does not create the stable release package. Building does not flash a device.

For Apple Silicon, preserve the qualified Blink interpreter setup (`-m -j`) and the guest-page patch described in `docs/development/BUILD-ENVIRONMENT-FIX.txt` and `tools/toolchain-patches/blink-macos-guest-pages.patch`. A different emulator/toolchain arrangement requires fresh qualification.

## Host and browser work

Use separate working trees for device and host/browser builds: their generated fonts and build files must not share `app/build/gen`. The host test commands in `BUILDING.md` apply to this source. Package the browser template with `tools/package_web.py`; do not open `web/index.html` directly. The published website includes the required presentation assets.

## Hardware and recovery scope

The exact release package passed direct V15 and published-2.1 updates and both return paths on Kid Bunka's FM-1. Returning through V15 requires a saved-bank backup, a separate unsaved-live export and physical SEL confirmation when restoring. All 22 saved objects and the live project matched after restoration. See `docs/Hardware-Rundweg-2.2.json` and `docs/Qualification-2.2.json`.

Exact-build UC25HQ80 hardware validation remains pending. This is an FM-1 release; it must not be flashed to an SMK-37 or another device.
