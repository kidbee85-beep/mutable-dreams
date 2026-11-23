# 2.4 → 2.3: a project saved with the GRIDS kit generator

`md_validate` of 2.3 accepts `kit.generator` up to 3; 2.4 adds GRIDS as 4 (a saved patch carries the number, nothing else changes).

```
mutable-dreams-host-2.4 1-write-grids-2.4.txt OUT          # saves the project, dumps the flash: OUT/grids-saved-2.4.bin
FM1B_FLASH=OUT/grids-saved-2.4.bin mutable-dreams-host-2.4 2-read-grids.txt OUT   # passes
FM1B_FLASH=OUT/grids-saved-2.4.bin mutable-dreams-host-2.3 2-read-grids.txt OUT   # fails at kit_mode (expected)
```

Result (19 Nov 2026, host): 2.4 reads its own project (kit_mode 1). 2.3 boots with a valid patch but not that project (kit_mode 0): it is ignored, not erased. The same holds for a User Biome saved with GRIDS: 2.3 cannot load it.
For the release notes: going back to 2.3 does not lose the project, but a SAVE made on 2.3 replaces it; use the backup (`.mddb`) before going back.

## 2.4 → 2.3: a project saved with every new setting (20 Nov 2026, host)
`3-write-settings-2.4.txt` stars a Biome, sets NIGHT, WIDTH, AIR, CHORUS and TEMPO RANGE (all live in the live-preferences trailer, `reserved[0]` bits 3-4 and `reserved[1]`), saves, restarts and dumps the flash; `4-read-settings.txt` runs on that flash (with `expect air 2` first on 2.4). Result: 2.4 gets the settings back; 2.3 loads the project and keeps the starred Biome, it only ignores the settings. (The preferences block must stay `reserved <= 3`: 2.3 rejects more.)
