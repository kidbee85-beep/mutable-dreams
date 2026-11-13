#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ "$#" -ne 1 ] || [ "$1" != "--fm1-test-candidate" ]; then
  echo "Private FM-1 2.1 development: use --fm1-test-candidate only to compile a local test package. This does not flash or publish. SMK-37 is not a supported build target." >&2
  exit 2
fi
: "${JIELI_TOOLCHAIN:?Set JIELI_TOOLCHAIN}"
: "${AC79_SDK:?Set AC79_SDK}"
"${PYTHON:-python3}" tools/check_palette_20.py
"${PYTHON:-python3}" tools/test_target_selection.py
exec "${PYTHON:-python3}" base/tools/build.py app --release 2.1.0-dev
