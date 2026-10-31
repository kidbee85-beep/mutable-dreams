#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${JIELI_TOOLCHAIN:?Set JIELI_TOOLCHAIN}"
: "${AC79_SDK:?Set AC79_SDK}"
exec "${PYTHON:-python3}" base/tools/build.py app --release 1.4.0-dev
