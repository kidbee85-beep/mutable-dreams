# Build Mutable Dreams 2.4

The released hardware package is exact qualified build 8FBE5AF8. See RELEASE-BUILD-2.4.json and SOURCE-SHA256-2.4.json for its scope and unchanged app/platform core. The browser and host run the same application and DSP serially; device second-core timings are measured separately.

Use Python 3.11+, Pillow, Clang/Clang++ and Node.js. Install a compatible system C SDK. Do not regenerate the factory collection before checking the supplied source: its IDs and values are frozen for this release.

```sh
python3 tools/build_local.py host --sanitize
python3 tools/test_transfer.py
export WASI_SDK=/absolute/path/to/wasi-sdk-34.0
python3 tools/build_local.py web
python3 tools/package_web.py app/build/browser/Mutable-Dreams-2.4.html
node tests/web_engine.mjs
node tests/web_storage.mjs
node tests/release24_web.mjs
node tests/device_editor.mjs app/build/browser/Mutable-Dreams-2.4.html
```

The local builder sets APP_VERSION=2.4 for the host/browser label. Generation writes only to app/build and the versioned screen-reader words. The updater kit is separate. For the device toolchain, SDK acquisition and vendored platform, see the device section of BUILDING.md. Its earlier-version examples are historical; select version 2.4.0 for this application. A local rebuilt firmware is a new artifact and needs its own qualification; source or browser tests are not a hardware timing test.

Do not upload personal .mddb backups, Live.raw, UI.raw or device MIDI configurations as release evidence. Published qualification is the scoped non-personal summary in RELEASE-BUILD-2.4.json.
