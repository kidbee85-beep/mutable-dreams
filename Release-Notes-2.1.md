# Mutable Dreams 2.1 · Itinera, long dreams for the FM-1

Mutable Dreams is a free, open-source six-track instrument for the M-VAVE FM-1, made by Kid Bunka. Version 2.1 adds **Itinera**: dreams that play on their own for half an hour up to four hours and move through many Biomes like one long piece.

## Itinera: press PLAY and let it travel

- **14 dreams**: Hortus Quietus, Via Siderum, Silva Nocturna, Mare Nebularum, Lumen Primum, Terra Pluvia, Aurora Polaris, Somnus Lenis, Murmur Arenae, Speculum Glaciei, Ignes Errantes, Fungi Lucentes, Terra Resonans and Aer Silens.
- Each dream runs **30, 60, 120 or 240 minutes**, with 8 to 32 scenes in four chapters. You also choose how fast it changes and how much space it has.
- **Smooth transitions.** The rhythm thins out before a scene ends, while the pads and bass hold. Key, tempo and effects glide into the next scene, and the new key stays close to the old one.
- **One arc from start to end.** Each scene's level is evened out toward the dream's average. The tone changes slowly over the whole dream, like morning, midday and evening. In "varied" dreams, some scenes pause for a bar or two to breathe.
- **A moving picture for every dream.** Smooth animation, soft lines and stars that drift to their new places. Each Biome family adds its own layer, such as mist, rain, ice glints, pollen or embers. Notes show up as small rings, and the picture sways with the music.

## Playing and recording

- **USB and TRS MIDI notes keep their timing within the audio block.** The timestamp is resolved to 0.25 ms during rendering. Incoming notes are scheduled one block later, about 5.8 ms at 44.1 kHz. External clock tempo is steadier and no longer overwrites the project's own tempo unless you choose to keep it with SEL on the MIDI page.
- **ECO is temporary.** When the load protection steps in, your project keeps its FULL setting for next time. The System page shows "ECO NOW, FULL KEPT FOR NEXT".
- **USB stems are dithered.** The six mono voices fade into soft noise instead of hard steps. The stereo mix is unchanged.
- **Faster Biome browsing.** Turning through Biomes quickly no longer rebuilds the sound for every click.

## Safety and reliability

- Bluetooth pairing keys never leave the device through the editor.
- Notes are released when USB goes to sleep or a Bluetooth device drops. Bluetooth pairing is saved only while stopped to avoid a write during playback.
- Restore checks the whole backup before it writes anything. An update is refused while a restore is still waiting for its restart.
- The screen reader's word list is now loaded from GitHub Pages.

## Install and recovery

Before updating, export your saved User Banks (`.mddb`) and your edited live Biome. Use the project installer. User Banks, Biomes and the project format are unchanged from 2.0 (`md_patch` v12), so your 2.0 banks keep working.

**Hardware coverage.** Build 2467CC38 was tested on an FM-1 via USB-MIDI in both directions with Mutable Dreams 2.0 and with M-VAVE stock V15. The saved banks and live Biome matched their backups byte for byte after the V15 recovery. Mare Nebularum completed a scene transition in a two-minute test with eight USB audio channels and screen mirroring, with zero measured dropouts. A physical four-hour run, a separate stock V16 round trip and validation on UC25HQ80 (B36014) remain open for this exact build. Back up your banks and live Biome before installing.

Mutable Dreams for the **SMK-37** is a separate adaptation in development.

Thank you to everyone who plays, records and sends feedback. The source tree and component notices are included at this tag.
