# Build Mutable Dreams 1.4

Use the browsable repository source tree or the versioned source bundle. No Git submodule download is needed. The application, selected Mutable Instruments code, fm1-base platform and felucca-bsp are vendored with their original notices.

```sh
cd /path/to/Mutable-Dreams-1.4-Source
```

## Source map

| Directory | Contents |
| --- | --- |
| `app/` | Firmware UI, banks, live editor, diagnostics and application configuration |
| `app/dsp/` | Six-track sound engine, sequencing, shared effects and preset tables |
| `app/third_party/` | Selected Plaits, Rings, Elements and stmlib source, including notices |
| `base/` | Platform, host/browser support, FM-1 BSP, loader and build tools |
| `web/` | Browser instrument and live-editor sources; `index.html` is a template |
| `presets/` | Factory catalog and development patch data |
| `tools/` | Build/package tools, backup/editor utilities and test runners |
| `tests/` | Native DSP/application and browser protocol tests |
| `docs/release-1.2/` | Released build qualification and update/recovery evidence |
| `docs/release-1.3/` | Historical 1.3 release evidence |
| `docs/release-1.4/` | Exact-build qualification, update notes and recovery information |

## Native build and checks (no FM-1 required)

Use Python **3.11 or newer**, Pillow and Clang/Clang++. macOS needs Xcode Command Line Tools; Linux needs the matching C/C++ development libraries. The commands below assume a POSIX shell. On Windows use a suitable Linux/WSL environment; native Windows builds have not been qualified.

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install Pillow

# macOS only: use the SDK installed on your machine.
export SDKROOT="$(xcrun --show-sdk-path)"

python tools/verify_published_source.py
python tools/generate_lofi.py
python tools/generate_presets.py
python tools/build_local.py host --sanitize
python tools/test_transfer.py
python tools/build_local.py dsp --sanitize
app/build/san/dsp-tests
```

Skip the `SDKROOT` line on Linux. The host executable is `app/build/san/mutable-dreams-host`; it is a command-driven simulator, not a desktop GUI. `tools/test_transfer.py` exercises the editor and simulated persistent storage without connecting to hardware. DSP tests check rendering and parameters; they do not measure device CPU headroom.

If macOS reports `tapi error: malformed file` / `unknown architecture` while linking SDK libraries, your selected SDK and linker are incompatible. Select an installed compatible SDK through `SDKROOT`; the source-publication check used `MacOSX15.4.sdk`. Do not change firmware code to work around a host SDK mismatch.

Run `python tools/build_local.py kits --sanitize` and `app/build/san/kits-tests` for all five sample banks, the change guard, v7 migration and the four-track rack. Further targets are `quality` (audio quality), `next` (earlier feature regressions) and `evolution` (1.2 sequence/chord/voice regressions). For example:

```sh
python tools/build_local.py next --sanitize
app/build/san/next-tests
python tools/build_local.py motion --sanitize
app/build/san/motion-tests
```

The `motion` target covers v8-to-v9 migration, independent LFO routing, neutral depth, sequence traversal, recording and motion persistence. Browser tests use Node.js: `tests/web_engine.mjs`, `tests/web_storage.mjs`, `tests/device_editor.mjs` and `tests/range_controls.mjs`. The latter two take a packaged HTML filename as an argument. They do not substitute for a visible browser or physical controller test.

Generated build files stay in `app/build/`. The snapshot's `.gitignore` is preserved unchanged; keep your local `.venv/` out of commits, for example through your local `.git/info/exclude`.

## Browser instrument

Install the WASI SDK separately; **34.0** was used for the release. Use the same Python/Pillow environment as above and set `WASI_SDK` to the directory containing its `bin/clang`:

```sh
export WASI_SDK=/absolute/path/to/wasi-sdk
python tools/build_local.py web
python tools/package_web.py app/build/browser/Mutable-Dreams.html
python -m http.server 8000 --directory app/build/browser
```

Open `http://localhost:8000/Mutable-Dreams.html` in your browser. **Do not open or publish `web/index.html` directly:** the packager must embed WASM, the catalog, worklet and editor scripts. This is the source instrument build; separately hosted tutorial/installer/media files are not created by this command. The current public release website includes the documented browser packaging addendum.

The browser and firmware share application/DSP sources. Browser storage is separate from saved hardware banks. Web MIDI editing requires a compatible browser and explicit device connection.

## FM-1 device build

Additional external dependencies:

- JieLi Linux **pi32v2** toolchain; release setup used `jieli-linux-toolchains-20260730.1`.
- JieLi **AC79NN SDK V1.2.1** (2023-12-13), with the expected boot/configuration components. The builder checks their hashes.
- A Linux x86-64 execution environment, or the supported tool runner/container arrangement for another host.

The vendored download helpers document their upstream locations: [toolchain](base/tools/get_toolchain.sh) and [SDK](base/bsp/tools/get_sdk.sh). The toolchain helper fetches the vendor's current package, which is not necessarily the version used for this release. External SDK/toolchain binaries are not included here.

```sh
export JIELI_TOOLCHAIN=/absolute/path/to/jieli-linux-toolchains-20260730.1
export AC79_SDK=/absolute/path/to/AC79/sdk
export PYTHON=/absolute/path/to/python3
sh tools/build_device.sh
```

`JIELI_TOOLCHAIN` must contain `pi32v2/` and `common/`; `AC79_SDK` must directly contain `cpu/`. The builder creates the application, update loader and `.fwsc` package under `app/build/`. Building does not flash a device.

For Apple Silicon, read [the tested build environment repair](docs/development/BUILD-ENVIRONMENT-FIX.txt) and [the preserved Blink patch](tools/toolchain-patches/blink-macos-guest-pages.patch). The qualified setup used the corrected Blink guest-page handling and interpreter mode (`-m -j`); the accelerated path failed in that setup. Do not assume a different emulator/container is qualified merely because compilation starts.

## Version and provenance

The v1.4 source archive contains the full tree and a SHA-256 manifest. Run `python3 tools/verify_published_source.py` to check these files. The device/DSP sources are identical to the qualified build; release documentation and browser packaging were updated afterwards. Toolchain differences can change binary outputs, so file identity alone is not a bit-for-bit build reproducibility claim.

Published v1.0 through v1.3 tags/assets remain unchanged. Their automatic GitHub source downloads describe their historical repository commits. Use their named source ZIPs for the original frozen snapshots.

See the [1.4 qualification](docs/release-1.4/DEVICE-TEST-REPORT-1.4.txt), historical [1.2 qualification](docs/release-1.2/QUALIFICATION.txt), [licensing](LICENSING-GPL.txt), platform credits and component/font notices.
