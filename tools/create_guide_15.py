#!/usr/bin/env python3
"""Create the illustrated, private 1.5 review guide from checked project assets."""
from pathlib import Path
from reportlab.pdfgen import canvas
from reportlab.lib.colors import HexColor
from reportlab.lib.pagesizes import A4
from reportlab.lib.utils import simpleSplit

root = Path(__file__).resolve().parents[1]
out = root.parents[1] / "outputs/Mutable-Dreams-1.5-Private-Preview/Mutable-Dreams-1.5-Guide.pdf"
screen = root / "docs/img/screen-1.5.png"
W, H = A4
ivory = HexColor("#F3F0E7")
ink = HexColor("#15242B")
muted = HexColor("#536565")
line = HexColor("#CBD5CE")
mint = HexColor("#86BA9E")
gold = HexColor("#DDB671")
coral = HexColor("#CE8275")
blue = HexColor("#77A9C1")

c = canvas.Canvas(str(out), pagesize=A4)
c.setTitle("Mutable Dreams 1.5 - Private illustrated guide")
c.setAuthor("Kid Bunka")

def box(x, y, w, h, fill, radius=12, stroke=None):
    c.setFillColor(fill)
    c.setStrokeColor(stroke or fill)
    c.roundRect(x, y, w, h, radius, fill=1, stroke=bool(stroke))

def txt(x, y, s, size=11, color=ink, font="Helvetica", leading=None, width=None):
    c.setFont(font, size)
    c.setFillColor(color)
    lines = simpleSplit(s, font, size, width) if width else [s]
    for t in lines:
        c.drawString(x, y, t)
        y -= leading or size * 1.45
    return y

def page_bg(color=ivory):
    c.setFillColor(color)
    c.rect(0, 0, W, H, fill=1, stroke=0)

def head(section, dark=False):
    color = ivory if dark else ink
    c.setStrokeColor(mint if dark else line)
    c.line(42, H - 49, W - 42, H - 49)
    txt(42, H - 36, "KID BUNKA / MUTABLE DREAMS", 8.8, color, "Helvetica-Bold")
    txt(W - 143, H - 36, section, 8.5, mint if dark else muted)

def foot(n, dark=False):
    color = HexColor("#A9BBB4") if dark else muted
    c.setStrokeColor(HexColor("#405659") if dark else line)
    c.line(42, 45, W - 42, 45)
    txt(42, 28, "PRIVATE DEVELOPMENT PREVIEW  /  09 NOV 2026", 8, color)
    txt(W - 56, 28, f"0{n}", 8, color, "Helvetica-Bold")

page_bg()
head("FIELD GUIDE / 1.5")
txt(42, 730, "The atlas grows.", 40, ink, "Times-Italic")
txt(42, 698, "A quick look at the new worlds and how to explore them.", 12, muted)
box(42, 425, 511, 246, HexColor("#E0E8DE"), 18)
box(55, 437, 222, 222, ink, 12)
c.drawImage(str(screen), 66, 448, width=200, height=200)
txt(302, 624, "169", 45, ink, "Times-Italic")
txt(304, 603, "Biomes including Blank", 11, muted)
txt(302, 550, "364", 45, ink, "Times-Italic")
txt(304, 529, "tone starting points", 11, muted)
txt(302, 478, "12 NEW WORLDS", 9, HexColor("#A46059"), "Helvetica-Bold")
txt(302, 459, "Existing User Banks stay in place.", 9.5, muted)
txt(42, 389, "Four ways into the new palette", 18, ink, "Times-Italic")
worlds = [
    ("Obsidian Tide", "deep, slow water and low resonance", mint),
    ("Night Hearth", "warm percussion with velvet motion", gold),
    ("Neon Moss", "rhythmic growth and bright detail", blue),
    ("Felt Labyrinth", "soft-edged paths and granular air", coral),
]
for i, (name, desc, color) in enumerate(worlds):
    y = 327 - i * 62
    box(42, y, 511, 52, HexColor("#FDFBF5"), 11, line)
    box(52, y + 9, 6, 34, color, 3)
    txt(70, y + 31, name, 13.5, ink, "Times-Italic")
    txt(70, y + 13, desc, 9.7, muted)
foot(1)
c.showPage()

page_bg(ink)
head("WIRELESS MIDI", True)
txt(42, 730, "Signals become music.", 38, ivory, "Times-Italic")
txt(42, 700, "Bring another MIDI instrument, controller or computer to the FM-1.", 11.5, HexColor("#BECFC6"))
box(42, 480, 511, 189, HexColor("#22383B"), 17)
txt(62, 640, "ON THE FM-1", 9, mint, "Helvetica-Bold")
txt(62, 615, "GLO  >  WIRELESS MIDI", 21, ivory, "Times-Italic")
knobs = [
    ("K1  RADIO", "Switch BLE MIDI on after stopping playback."),
    ("K2  DEVICE", "Search for eight seconds, then choose a MIDI device."),
    ("K3  LINK", "Connect or release the selected device."),
    ("K4  MIDI THRU", "Forward received MIDI when needed."),
]
for i, (label, detail) in enumerate(knobs):
    y = 584 - i * 31
    txt(62, y, label, 9.4, gold if i % 2 else mint, "Helvetica-Bold")
    txt(195, y, detail, 9.4, ivory)

txt(42, 442, "A first connection", 20, ivory, "Times-Italic")
steps = [
    ("01", "Stop the sequence, open Wireless MIDI and turn K1 right."),
    ("02", "On a phone, tablet or computer, connect to the FM-1 BLE MIDI service."),
    ("03", "Set a track's MIDI channel on MIDI / PERFORMANCE; send notes or CC."),
    ("04", "For external clock, select BLE on MIDI / PERFORMANCE."),
]
for i, (num, body) in enumerate(steps):
    y = 397 - i * 60
    c.setFillColor(mint if i % 2 == 0 else gold)
    c.circle(57, y + 4, 16, fill=1, stroke=0)
    txt(47, y, num, 9.5, ink, "Helvetica-Bold")
    txt(85, y + 6, body, 10, ivory, width=442)

box(42, 88, 511, 77, HexColor("#31514B"), 13)
txt(60, 139, "TESTED ON A PHYSICAL FM-1", 9, mint, "Helvetica-Bold")
txt(60, 119, "Notes, CC74, Start, Clock and Stop arrived over BLE from a Mac.", 10, ivory)
txt(60, 101, "FM-1-to-external-device linking still needs a separate controller test.", 9.7, ivory)
foot(2, True)
c.showPage()

page_bg()
head("SAVE / UPDATE / RETURN")
txt(42, 730, "Keep your worlds.", 39, ink, "Times-Italic")
txt(42, 700, "Make a backup before every change of firmware.", 12, muted)
sections = [
    ("1. Save what is yours", "Save the current Biome on the FM-1. Download a complete .mddb User Bank backup and export the live patch as JSON if it contains unsaved changes. Browser-only saves are separate."),
    ("2. Install from the right page", "Use the matching Mutable Dreams installer, choose only the intended FM-1 and keep USB connected until writing has finished. Do not use M-UPGRADE for the custom package; it rejected earlier Mutable Dreams packages."),
    ("3. Verify the restart", "Check for identity FM-1_710010500. The revised updater restarted and returned USB MIDI automatically in the final physical test. If a device does not return after writing is complete, power it off, disconnect USB for five seconds, then verify again. Never disconnect during writing."),
    ("4. Return to stock if needed", "The private installer provides the verified original V15 path. Returning to V15 erases Mutable Dreams User Banks and preferences. Reinstall a compatible Mutable Dreams version before restoring .mddb; the FM-1 requires a physical SEL confirmation."),
]
y = 650
for title, body in sections:
    box(42, y - 107, 511, 111, HexColor("#FDFBF5"), 11, line)
    txt(58, y - 21, title, 17, ink, "Times-Italic")
    txt(58, y - 45, body, 9.65, muted, width=474, leading=14.5)
    y -= 126

box(42, 71, 511, 65, HexColor("#E4DFD3"), 12)
txt(58, 113, "RELEASE STATUS", 9, HexColor("#A46059"), "Helvetica-Bold")
txt(58, 93, "This illustrated guide describes a private 1.5 candidate.", 10.5, ink)
txt(58, 78, "A newer UC25HQ80 FM-1 and BLE central role still need physical tests.", 9.2, muted)
foot(3)
c.save()
print(out)
