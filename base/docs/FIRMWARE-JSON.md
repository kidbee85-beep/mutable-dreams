# firmware.json

`firmware.json` describes a firmware for the places that list and install FM-1 firmwares: The Firmware Hub,
RemoteFM1 and M-Flasher. It says what the firmware is called, who made it, its licence, what it is, where to install
it and what the hardware needs to have. It sits at the root of the firmware's repository, and a release can attach it
beside the package.

tools/build.py writes it on every build, from fm1.toml and the parts the build turns on, to `APP_DIR/build/firmware.json`
(`firmware-usb.json` from a `--no-ble` build). For an app in a firmware's repository (`APP_DIR` named `app`, as
`fm1-ctl/app`) it also writes the repository's `firmware.json`, only when its content has changed. fm1-base's own
template gets only `template/build/firmware.json`.

It has no version: it describes the firmware, not a release, and the release's package carries its own.

The format was shaped after Schwung's module catalog and RemoteFM1's metadata proposal
(Quixotic7/RemoteFM1FW, docs/REMOTEFM1-METADATA.md). Its JSON Schema is [firmware.schema.json](firmware.schema.json).

## An example

CTL-1's:

```json
{
  "schema": 1,
  "id": "ctl",
  "name": "CTL-1",
  "author": "Charles Vestal",
  "license": "MPL-2.0",
  "tagline": "A MIDI controller for the FM-1",
  "description": "CTL-1 turns the M-VAVE FM-1 into a MIDI controller. The keys play notes, with velocity and aftertouch read from the keys themselves, in any scale with its chords under the black keys; the knobs send controllers in four banks; an arpeggiator and a four-track Euclidean drum sequencer run on its clock. Everything goes out over USB and Bluetooth, MIDI from the TRS jack is passed along, and the FM-1 plays it all on its own General MIDI sounds too, so it works with nothing connected.",
  "tags": ["controller", "midi", "sequencer", "chords", "general-midi"],
  "install": "https://charlesvestal.github.io/fm1-ctl/install/",
  "site": "https://charlesvestal.github.io/fm1-ctl/",
  "source": "https://github.com/charlesvestal/fm1-ctl",
  "screenshot": "docs/img/screen-cc.png",
  "hardware": {
    "models": ["FM-1"],
    "flash": ["856014", "B36014"],
    "features": ["bluetooth-midi", "bluetooth-midi-devices", "usb-audio-in", "usb-audio-out", "velocity", "pressure", "screen-reader"]
  },
  "credits": "Built on fm1-base and felucca-bsp, from Felucca by Leo Kuroshita (Hügelton Instruments); the sounds are Sonivox EAS."
}
```

## Fields

| Field | Type | From (fm1.toml) | Meaning |
|---|---|---|---|
| `schema` | number | | The format's version: 1. |
| `id` | string | `name`, in lower case | The firmware's short name, the same as its package's file name (`ctl`). |
| `name` | string | `title`, else `usb_manufacturer`, else app.h's `APP_NAME` | The name people see (`CTL-1`). |
| `author` | string | `author`, else `authors` joined | Who made it. |
| `license` | string | `licence` | Its licence, an SPDX expression (`MPL-2.0`). |
| `tagline` | string | `tagline` | One line on what it is. |
| `description` | string | `description` | One paragraph, at most 600 characters. |
| `tags` | list of strings | `tags` | What it is, from the tag vocabulary. |
| `install` | URL | `install` | The page that installs it. |
| `site` | URL | `site` | Its site. |
| `source` | URL | `repo` | Its source code. |
| `screenshot` | path | `screenshot` | A picture of its screen for a hub's card: a PNG in its repository, relative to the repository's root (`docs/img/screen.png`). |
| `hardware.models` | list of strings | | The models it runs on. A firmware on fm1-base says `FM-1`. |
| `hardware.flash` | list of strings | device/fm1_flash_ok.h | The flash parts it can save to and update on, as their JEDEC IDs in upper-case hex. |
| `hardware.features` | list of strings | the build's parts | What the build has, from the feature vocabulary. |
| `credits` | string | `credits` | A sentence on what it is built from. |

`title` is for a firmware whose USB name is not the name people know it by (Radio's USB name is `fm1-radio`);
most leave it out.

URLs are `https://` addresses. The build writes the keys in this order, with a 2-space indent and each list of
strings on one line, in UTF-8 with a newline at the end.

## The screenshot

A hub shows the screenshot on the firmware's card. It fetches it from the firmware's repository at the release's
commit: `source`, the release's tag and the path (on GitHub,
`https://raw.githubusercontent.com/OWNER/REPO/vX.Y/docs/img/screen.png`).

In fm1.toml, `screenshot` is a path relative to the repository's root, which is the folder that holds `app/` (for
fm1-base's template, the template's folder). It uses `/` between folders, letters, digits, `.`, `_` and `-`, ends in
`.png`, and has no `..` and no leading `/`. The build refuses any other path. It also checks the file: it has to
exist, be a PNG, be square, be at least 240 × 240 and be at most 2 MiB. The build reads only the PNG's header, so it
needs nothing installed.

The FM-1's screen is 240 × 240, so a screenshot from the host simulator (its `shot FILE` command) or from screen
mirroring fits as it is. A hub should scale it up with nearest-neighbour (CSS `image-rendering: pixelated`) to keep
the pixels sharp.

## What a release needs

A release build (`tools/build.py APP_DIR --release X.Y`, as `./build.sh --release X.Y` and the release workflow run
it) refuses until fm1.toml has all of these, and names the ones missing:

`author` (or `authors`), `licence`, `tagline`, `description`, `tags`, `install`, `repo` and `screenshot`.

It also refuses a screenshot that fails the checks above, and says which check.

Any other build (a dev build, `--host`, `--web`, `--rfm1`) prints one warning line naming the missing keys, and one
for a screenshot that fails a check, and writes the file without them. `site` and `credits` are never required.

## Tags

A firmware's tags come from this list, and the build refuses any other with a message that lists them:

synth, drum-machine, groovebox, sequencer, sampler, controller, midi, chords, arranger, ambient, generative, fm, game,
utility, general-midi, experimental.

## Features

Each of these is listed only when the build has it:

| Feature | The build has it with |
|---|---|
| `bluetooth-midi` | `ble = true` (not with `--no-ble`): Bluetooth MIDI with a computer or a phone |
| `bluetooth-midi-devices` | `ble_central = true`: the FM-1 connects to Bluetooth MIDI devices too |
| `bluetooth-audio` | `bt_audio = true` on the own stack (`ble_stack = "own"`): sound out to Bluetooth speakers or headphones |
| `usb-audio-in` | `usb_audio_in` (on unless set false): the computer records the FM-1 over USB |
| `usb-audio-out` | `usb_audio_out` (on unless set false, with `usb_audio_in`): the computer plays through the FM-1 |
| `usb-midi-host` | `usb_host = true`: USB MIDI controllers and synths plugged into the FM-1 |
| `usb-disk` | `msc = true`: the FM-1 as a USB drive |
| `wifi` | `wifi = true` |
| `second-core` | `cpu2 = true` |
| `velocity` | `APP_TOUCH` in app.h (lib/touch): velocity from the keys |
| `pressure` | `APP_TOUCH` in app.h: pressure on held keys |
| `screen-reader` | a words file with its `# url:` line (`words`): the screen reader says the screen in words |
| `screen-mirror` | `mirror = true`: the screen and panel mirrored over USB |
| `backup` | `backup = true`: the saved work backed up and restored over USB |
| `resources` | `resources`: a resource region the installer writes after the firmware |

The vocabularies live in tools/firmware_json.py, and tests/firmware_json_test.py checks that this page and the schema
list the same ones.

## Rules

Readers ignore fields they do not know. A missing field means the firmware does not say: a firmware without `tags`
has no tags given, and one without `hardware.features` says nothing about its features. A hub may keep its own data
in the file under keys that start with `x-`, for example `x-remotefm1`, at the top level or in `hardware`; the format
will never use such a key. A firmware not built on fm1-base may write the file by hand, to the same schema.

## Changes

Schema 1, 2026-11-05. `screenshot` and fm1.toml's `title` added on 2026-11-05, before any release used the schema.
