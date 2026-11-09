# TEMPLATE

TEMPLATE is the smallest complete firmware for the M-VAVE FM-1, and it's made to be copied. Every control shows
its name on the screen as you use it. The keys play a simple voice with velocity and pressure, your settings save,
MIDI goes in and out over USB and Bluetooth and comes in over TRS, and it works with a screen reader. Copy it
to start your own firmware on fm1-base.

![TEMPLATE while a key is held: C4 in the header with V 90 P 64, FX lit among the buttons, K3 at 3, the key filled](manual/img/screen-playing.png "TEMPLATE on the FM-1's screen")

This is version {{VERSION}}. You install it at your own risk, and the installer can put M-VAVE's own firmware back.

- [Try it in the browser](emu/). Play it right here, with sound. It runs the same code as the FM-1, so you don't
  need one.
- [Install](install/) it from Chrome or Edge, with the FM-1 connected by USB.
- The [Manual](manual/) explains the screen, the controls, saving and MIDI.
- [Download {{PKG}}](firmware/{{PKG}}) to use the command-line installer: `python3 tools/fm1_install.py {{PKG}}`.
- The [Source](https://github.com/charlesvestal/fm1-base) is under MPL-2.0. It's built on fm1-base and
  felucca-bsp, which come from Felucca by Leo Kuroshita, Hügelton Instruments.

M-VAVE and FM-1 are trademarks of their owners. TEMPLATE is not affiliated with M-VAVE.
