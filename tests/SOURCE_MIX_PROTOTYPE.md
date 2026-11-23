# SATIN / SPECTRAL ordered Source+Mix prototype

`MD_SOURCE_MIX_PIPELINE` and `MD_ENGINE_UNION` are enabled in this isolated Balanced candidate. Source dispatch requires the existing single-worker `MD_MIX_PIPELINE`. This is a host prototype with no device performance or target stack/memory qualification for the adaptive change.

CPU0 keeps admission, quality, MIDI, triggers, Creative controls, VINTAGE RNG, wavetable control slews and filter/mix parameter preparation. For SATIN/SPECTRAL only, it copies the source tag, table indices and derived oscillator parameters into the existing owned descriptor. The worker calls the unchanged `render_wavetable_tile`, then the existing ordered `mix_voice`. Its Voice and output slot stay exclusively owned through join. CPU0 can render the following source in the other existing ping-pong output slot.

The adaptive scheduler defers a SATIN/SPECTRAL source only with no outstanding mix, or when a read-only job-end hint is nonzero. Otherwise the exact original wavetable tile runs immediately on CPU0, in the other existing ping-pong slot, while mix(i-1) may still run on CPU1. All CPU0 control preparation stays in its original order. A first eligible SATIN/SPECTRAL voice can still put its source on CPU1 each quantum; pure SATIN/SPECTRAL scenes continue to produce positive source-job counts. CPU0 source executions now increment the same serial-source counter, so accepted+serial still counts every selected source once.

`md_mix_worker_ready_device()` executes the existing platform csync then reads the volatile end timestamp. Submission clears it and synchronizes before publishing every accepted job. CPU1 writes it after source/mix work and synchronizes. The previous accepted job was joined before this clear, so an old producer cannot publish after the new reset. A zero timestamp at clock wrap, or a stale zero, just selects CPU0. A stale positive, a misread, or a job that has finished DSP but not platform epilogue can only choose the old deferred placement: the unchanged bounded join still runs before descriptor reuse, bus access or next submission. A reported failed job is still retired/stopped, never replayed. The hint grants no ownership and is not a completion/fault check.

The native host backend conservatively returns a false hint. `MD_SOURCE_READY_HOST_TEST` deliberately forces true only in PLAT_HOST parity builds, including a worker deferred until join, to test stale-positive safety. It is rejected in device compilation and introduces no production diagnostic/fault override. The actual target hint frequency/usefulness and extra csync cost are unmeasured; it may not restore or improve earlier timing. Host parity does not establish device speed, callback headroom, stack high-water or SRAM size.

The 28-byte source snapshot has no PCM storage. It selects constant tables through bounded indices. No new PCM buffers, other source engines, RNG stream, mixing order, admission weights, dispatch threshold, timeout or stack allocation are introduced. The 12-byte source counters are written only by CPU0; `md_source_mix_stats()` reads accepted/completed/serial counts while the audio owner is locked and joined. Counters reset at `md_init()`. The serial count includes both immediate CPU0 source rendering and unaccepted/last-voice source jobs executed serially; it is not a hardware dispatch refusal count.

Float bus additions and the single stem-dither RNG retain track order. An unaccepted job can execute serially once. An accepted partial job follows the existing retire/confirmed-hold and callback/rack recovery policy; it is never replayed as an untouched source. Existing device bounds/checksum validation covers the new source fields as well.

This applies the public peers' ownership/dispatch approach to the smallest independently owned Mutable source kernel. It does not introduce a new chip-level claim or transplant another firmware's state or linker addresses.

Run the native ASan/UBSan audit with Python 3.11+:

```sh
python3 tests/run_source_mix_pipeline.py \
  --reference /absolute/path/to/normal1477/source \
  --objects /absolute/path/to/source-identical/host-object-donor \
  --output /absolute/local/report/path
```

The donor must already have sanitized vendor objects in `app/build/san`. The runner compares every vendored source/header byte before reuse. It compiles host probes including the real DSP source, with no target/DSP-object build. Optional `--atlas-probe` and `--dream-probe` accept the verified canonical probes and verify their known output digests under both hint variants. The runner also compares complete active-engine state through all24x24 model transitions against the pinned old non-union source.

Coverage: original/default-off/source-on, all 24 models, wrapped eight-channel stems, RNG/Voice/filter/Creative/VINTAGE traces, dense MIDI, fragmented tiles, fades, all four floating rounding modes, actual threads, jitter, deferred/synchronous execution and refusal. The device-entry host mock exercises the actual descriptor validator and source dispatch. A fault advances one source sample, then aborts, proving no replay under both hints; the conservative path is allowed to have rendered only the distinct next voice on CPU0 before the failed join. The backend contract checks FP environment handoff and accepted partial exceptions.

Before device adoption, the final combined target still needs linker/RAM-call checks, stack high-water, bounded wait/callback timing, physical-device audio/stem regression and fault recovery qualification. Host wall-clock timings are test duration, not FM-1 performance measurements.
