# Mutable Dreams 2.4 for the FM-1 — status

Working branch: `main` (green) · experiments: `wip/mi-modules`
Base: tag `v2.3`, commit `57c4352`

This document tracks the 2.4 work so that it can be reviewed while it is still in
progress. Nothing here is released; `main` only grows by commits whose build and
checks are green.

---

## 1. What is in `main`

| Commit | Change | Effect |
|---|---|---|
| `73a6e62` | **Grids** (Mutable Instruments' drum sequencer) as kit generator 4 | New kit generator "GRIDS": `grids_generate(k, 127, 127, 127, 40)` fills a kit in the same way the other generators do; re-rolls at `step % 32 == 0`; generator bound and validation extended to 4; UI name `GRIDS` |
| `487b157` | **A-06** FX state (`room`, `cedar`) into `.pool` + **A-07** denormal flush | A-06 was reverted in the next commit, see below |
| `3637c59` | **A-07** only | flush to `1e-18` on every recursive path (`echo_lp`, `feedback_lp`, `grain_noise_lp`, `grain_noise_smooth`, `fx_highpass`, `dcx`, `dcy`, `room_lp`); no more denormal stalls after a long tail |

### Build numbers

| Build | Image | RAM `.data+.bss` | Pool | Free flash |
|---|---|---|---|---|
| 2.3 baseline | 546 928 B | 89 676 / 98 304 B | 335 856 / 344 064 B | ~34 KB (slot 581 564 B) |
| + Grids | 549 760 B | 89 676 / 98 304 B | 335 856 / 344 064 B | ~31 KB |
| + A-07 (current `main`) | **550 000 B** | 89 676 / 98 304 B | 335 856 / 344 064 B | **~31 KB** |

`RAM code .ram_text` 23 196 / 32 768 B.

### Why A-06 is not in `main`

Moving `room[4][1201]` and `cedar_line[6][512]`/`cedar_state[6]` to `.pool` needs
~17 KB, but the pool only has 8 KB of reserve left — the link fails with
`region POOL overflowed by 9184 bytes`. A-06 comes back once the pool has room
(e.g. after the tape/kit reduction below). It is a RAM move, not a flash saving,
so it does not help with the sound-engine budget.

---

## 2. Work in progress: the additional MI modules (`wip/mi-modules`)

Branch `wip/mi-modules` (tip `0411318`) adds the Mutable Instruments modules
Clouds, Warps, Marbles, Tides and Grids from the SMK-37 port, each over its own
`stmlib` renamed to `mi_stmlib` so it never collides with the 2.3 engine's
`stmlib`:

- `app/third_party/mi/` — the module sources (`clouds`, `grids`, `marbles`,
  `tides2`, `warps`, `stmlib`)
- `app/mi/` — the C bridges (`brick_mi.h`, `brick_mi_*.cpp`)
- `app/compat/` — `cstdio`/`cstdlib`/`cstring` added, `algorithm`/`cmath`/`math.h`
  extended for what the modules need
- `app/fm1.toml` — `mi = [...]`, `mi_cflags = ["-DTEST", "-Dstmlib=mi_stmlib"]`,
  `include` extended by `third_party/mi`
- `base/tools/build.py` — an `mi` unit loop next to the `o2` loop (own objects in
  `app/build/mi`, C++11, `-fno-exceptions -fno-rtti`, own include path)

**Tides** is wired into the render path: `mi_tides_render()` replaces
`tidal_wave()` for the per-track tide, with the old LUT curve kept as the
fallback. The switch is `-DMD_MI_TIDES` — deliberately *not* `MD_TIDES`, which
is already an enum value (the tidal command).

### Blocker

The link fails on RAM, not on flash:

```
app/build/mutable-dreams.elf section `.bss' will not fit in region `RAM'
region `RAM' overflowed by 3040 bytes
```

Tides' state alone does not fit into the 8 628 B of `.data+.bss` reserve. Two
ways out, neither done yet:

1. give the module state a home outside `.bss` (pool/psram-style arena), or
2. free RAM first — see the levers below.

Warps compiles but is not wired to a parameter yet.

---

## 3. Space levers (measured, not yet pulled)

Ordered by risk. The first two are precision/quality trades with no audible
change in the intended range; the third is a real sound change.

| Lever | Saving | Risk |
|---|---|---|
| Elements `smp_sample_data` float → int16 (256 KB table) | ~11 KB flash | none in the used dynamic range |
| Elements `smp_noise_sample` int16 → int8 (82 KB table) | ~41 KB flash | none, it is noise |
| Satin tables as spectral pairs (new flag `MD_SATIN_AS_SPECTRAL`, as used on the SMK-37 port) | ~16 KB flash | audible, halves the resolution of the satin curve |
| Kit samples PCM → mu-law | ~80 KB flash | audible, but the kit is lo-fi by design |

Elements' `resources.cc.o` is 372 KB — about 70 % of the image — of which
`smp_sample_data` (256 KB) and `smp_noise_sample` (82 KB) are the bulk. That is
the single biggest lever and it is the one to take first.

---

## 4. Next steps

1. Free the 3 KB of `.bss` (or move the module state) so Tides links.
2. Wire Warps, then Marbles; Clouds only if the flash budget allows it
   (Clouds' buffers alone are 184 KB and need a RAM arena).
3. Pull the Elements levers, then re-try A-06.
4. Regression + headroom gate before anything is called 2.4.

## 5. Verification used for `main`

- `python3 base/tools/build.py app --release 2.4` (Blink mode, no Docker)
- link gate: slot, `RAM code`, `.data+.bss`, pool — all inside budget, see the
  table above
- the host DSP tests under `tests/` are **not** yet re-run for 2.4 — they are the
  next gate before anything on `main` is called a release

Review notes and measurements are welcome as comments on the tracking issue
rather than as direct commits to `main` while the MI work is still link-broken.
