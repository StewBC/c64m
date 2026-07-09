# Optimization notes

## Accepted

- `ca16212`: removed successful per-cycle error formatting from `c64_step_cycle`; hot-loop +26.9%; tests passed.
- `12ac7b7`: moved runtime completed-frame publish buffer off stack; fixed optimized-build bus error; tests passed.
- `b0a6bc9`: skipped VIC sprite composition when `$D015 == 0`; hot-loop +41.6%; tests passed.
- `e05c2dc`: cached VIC bank base from CIA2 port state; hot-loop +13.4%; tests passed.
- `72ad283`: skipped SID mixing/filter/sample output only when audio is explicitly disabled; SID still runs during normal audio playback and turbo multipliers; hot-loop +5.1%; tests passed.
- `8efc9f5`: gated CPU debug trace copies while preserving pending bus-event timing; hot-loop +10.4%; tests passed.
- Turbo display throttle (pixel output + frame-slot drop): when
  `active_turbo_multiplier >= 8` or breakpoint FAST mode, VIC keeps
  raster/BA/IRQ/sprite-DMA timing but skips ARGB fill, working-frame clear,
  and `working→completed` memcpy; runtime drops completed frames without
  multi-copy when the UI frame slot is still full, and rebuilds one
  geometric snapshot only when the slot is empty. Measured machine hot-loop
  (`profile_c64_hotloop`, 20M cycles, audio off): ~11.3 MHz with video on vs
  ~22.8 MHz with video off (~+100% / ~2.0×). Sprite collision latches only
  update while pixel output is enabled (accept under turbo). Tests passed.

## Rejected

- VIC background lazy color/base computation; measured speedup was within noise and was reverted.

## Guidance

- Preserve timing-visible side effects when optimizing hot paths.
- Preserve pending bus-event timing when reducing debug or trace overhead.
- Do not skip SID state advancement during normal playback or turbo unless the documented audio-disabled behavior applies.
- Turbo may skip host ARGB presentation work, but must not skip VIC timing,
  BA windows, raster IRQ, or CIA/SID state advancement.
- Record measured speedup, whether tests passed, and whether the change was accepted or reverted.
