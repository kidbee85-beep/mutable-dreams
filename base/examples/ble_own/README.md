# The template

The smallest complete fm1-base firmware. Copy this directory to start your own: it builds, installs, updates, runs
in the host simulator and in the browser, speaks to a screen reader, and publishes a site with an installer and a
manual.

| File | What |
| --- | --- |
| `app.h` | The app's settings (name, block size, the store's objects) and its half of the contract with the platform |
| `app.c` | The app: input, sound, screen, saving |
| `fm1.toml` | Build and site settings: name, package identity, USB names, tagline, source, licence |
| `words.tsv` | The screen reader's words (fm1-reader): the screen's text and how to say it |
| `tests/basic.txt` | Host simulator checks: keys, MIDI, settings across a reboot, the sound, the screen reader |
| `tests/screens.txt` | The manual's screenshots |
| `docs/MANUAL.md` | The manual (an example to rewrite) |
| `site/landing.md` | The site's front page |

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

## Make it your own

1. Copy `template/` to your firmware's repository, with fm1-base beside it as a git submodule.
2. In `app.h`, set `APP_NAME`, and the store's objects and their flash offsets (`APP_STORE_MAP`).
3. In `fm1.toml`, set the name, an `id_prefix` of your own (`FM-1_` and digits; each firmware has its own), the USB
   names, and the site's tagline, source and credits.
4. In `words.tsv`, set the `# url:` line to where your site publishes its words.
5. Write your app in `app.c`: `app_render` for the sound, `app_frame` for the screen and the controls, and the rest
   of the contract in `app.h`.
6. Rewrite `docs/MANUAL.md` and `site/landing.md`, and the tests.

fm1-base is MPL-2.0: changes to its files are published under MPL-2.0, and your own files may use any licence.
