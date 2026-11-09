# The template

The smallest complete fm1-base firmware. Copy this directory to start your own: it builds, installs, updates, runs
in the host simulator and in the browser, speaks to a screen reader, and publishes a site with an installer and a
manual.

| File | What |
| --- | --- |
| `app.h` | The app's settings (name, block size, the store's objects) and its half of the contract with the platform |
| `app.c` | The app: input, sound, screen, saving |
| `fm1.toml` | Build and site settings: name, package identity, USB names, tagline, source, licence, and firmware.json's |
| `words.tsv` | The screen reader's words (fm1-reader): the screen's text and how to say it |
| `tests/basic.txt` | Host simulator checks: keys, MIDI, settings across a reboot, the sound, the screen reader |
| `tests/screens.txt` | The manual's screenshots |
| `docs/MANUAL.md` | The manual (an example to rewrite) |
| `site/landing.md` | The site's front page |
| `.github/workflows/release.yml` | Signed releases built on GitHub (moved to the repository's root: Make it your own) |

## Build, test, install

From the fm1-base directory:

    tools/build.py template                     # template/build/template.fwsc (JieLi toolchain, AC79 SDK)
    tools/build.py template --host              # the host simulator, then:
    template/build/host/template_host template/tests/basic.txt template/build/host
    tools/build.py template --web               # the browser emulator: template/build/emu/
    python3 tools/fm1_install.py template/build/template.fwsc

For the toolchain, the SDK and the container the build runs in, see fm1-base's README.

## Release and site

    tools/build.py template --release 0.1       # template/build/template-0.1.fwsc, identity FM-1_11000100
    tools/publish_site.sh template 0.1          # builds the release, the emulator and the site; pushes gh-pages

The site has the front page, the installer, the emulator, the manual and the screen reader's words. To build it
without publishing, run `tools/make_site.py template template/build/template-0.1.fwsc 0.1 OUT_DIR`.

In a firmware's own repository, a release is built on GitHub from its tag by `.github/workflows/release.yml`, which
calls the shared FM-1 builder ([`zvenson/fm1-build` at `v1`](https://github.com/zvenson/fm1-build/tree/v1)). The
builder runs `./build.sh --release X.Y` and `tests/run_tests.sh` (a failing test stops the release) and signs build
provenance for `NAME-X.Y.fwsc` and `NAME-X.Y-app.bin`; the FM-1 Firmware Hub shows such packages as built from
source. It also builds `NAME-X.Y.rfm1` for host apps such as RemoteFM1, with Emscripten 6.0.10 from emsdk, and
releases and signs it with `firmware.json`. The workflow makes a draft release (or keeps the one you wrote, with its notes), checks the sums and the
signatures, then publishes it. A tag with a suffix (`v0.2-beta`) is a prerelease; a published release is never
rebuilt. For the site, download that package and give it to `publish_site.sh`, so the site has the attested one:

    gh release download v0.1 --pattern '*.fwsc' --dir app/build
    base/tools/publish_site.sh app 0.1 app/build/NAME-0.1.fwsc

## Make it your own

1. Copy `template/` to your firmware's repository, with fm1-base beside it as a git submodule.
2. In `app.h`, set `APP_NAME`, and the store's objects and their flash offsets (`APP_STORE_MAP`).
3. In `fm1.toml`, set the name, an `id_prefix` of your own (`FM-1_` and digits; each firmware has its own), the USB
   names, the site's tagline, source and credits, and your name, a description, tags, your install page and a
   screenshot. The build writes them to `firmware.json` at your repository's root, which firmware hubs read
   (fm1-base's docs/FIRMWARE-JSON.md); commit it. A release build refuses until your name, the description, the
   tags, the install page, the tagline, the source, the licence and the screenshot are set. The screenshot is a
   square PNG of at least 240 × 240 in your repository, its path relative to the repository's root; one of the
   manual's screenshots (`tests/screens.txt`) fits as it is.
4. In `words.tsv`, set the `# url:` line to where your site publishes its words.
5. Write your app in `app.c`: `app_render` for the sound, `app_frame` for the screen and the controls, and the rest
   of the contract in `app.h`.
6. Rewrite `docs/MANUAL.md` and `site/landing.md`, and the tests.
7. For signed releases, move `.github/workflows/release.yml` to the repository's root (`.github/workflows/`) and
   change the lines marked `# <- yours`: the name in the release title and the concurrency group, `pkg=` (fm1.toml's
   name in lower case), any other assets, and what the tests need. Then a pushed tag `vX.Y` builds, tests, attests
   and publishes the release.

fm1-base is MPL-2.0: changes to its files are published under MPL-2.0, and your own files may use any licence.
