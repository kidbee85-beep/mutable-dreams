# Building Mutable Dreams 2.3

This source corresponds to the qualified application build CF133B2D, identity FM-1_710020300. Follow the toolchain/platform setup in BUILDING.md and the included base documentation. The application source and project format are unchanged by the publication-only documentation edits.

Build the versioned application with `python3 base/tools/build.py app --release 2.3` after setting JIELI_TOOLCHAIN, AC79_SDK and PYTHON as described in BUILDING-2.2.md. The --release setting supplies APP_VERSION and the package identity. Keep compiler, linker, platform revision, flags and cache layout consistent when comparing timing. A fresh local build is not automatically the qualified CF133B2D binary, even when its sources match. Measure audio callback headroom and test update/restore before distributing a different binary.

The published .fwsc SHA-256 is c70c4371a4287ecbf68b1bb6f53d8d519f968276417ec94a058a0051c6b1ca96. Release-Notes-2.3.md lists the measured tests, backup instructions and unresolved hardware qualification. The m-bootloader is a separate optional recovery project and is not part of this application package.

SOURCE-SHA256.json records the archive members. No device User Banks, credentials or private test recordings are included.
