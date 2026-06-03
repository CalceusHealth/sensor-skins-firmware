# Session handoff — 2026-06-03

## Battery reseed-on-recharge fix (`v00020026`) + branch model

### What prompted it
Devices drained below ~3.3 V were charged overnight. On reconnect the app showed
critically-low battery, then the level **climbed every few minutes** up to 100%.

### Diagnosis
Not an app bug. The mobile app displays the latest firmware voltage verbatim (no
averaging). The averaging is in `battery.c`:

- `battery_update()` kept a 10-deep moving average (`VBAT_AVERAGE_N 10`), replaced
  **one slot per update**, seeded only once at boot.
- Cadence: ~1 s when connected, ~5 s asleep (`main.c`); app re-polls every 30 s.
- After a recharge the buffer still held the pre-charge low samples and washed them
  out one slot at a time → the reported voltage (and derived %) ramped up slowly.

Note: the `;RB` response already sends a **fresh** raw ADC (`adc_read_vbat_raw_fresh`)
in `RAW=` alongside the **averaged** `mV`, which confirmed the lag.

### Fix
`battery.c` — reseed all buffer slots when a fresh reading jumps `>150 mV`
(`VBAT_JUMP_RESEED_MV`) above the running average (a recharge). Upward-only, so
genuine discharge stays smoothed. `DEVICE_FW_VERSION` `0x00020024 → 0x00020026`.

- Commit: `dfb7ac0` on `tim`.
- Packages: `artefacts/out/stream_sleep_{lhs,rhs}_v00020026_sensorskins.zip`
  - lhs sha256 `9ddf67d03c56ae3f2e210248b8017caaf1cc84da2716463422103f34eb8ddce3`
  - rhs sha256 `44eac1513a7632db8489881d1d1e5050b5acbb31b3f74ee37ce468da43a3d992`
- Built on the deployed `v00020024` baseline (IMU kept off — see below).

### Status: BUILT + PUSHED, NOT YET FLASHED/TESTED
**Next session:** flash both sides per `artefacts/howTo`, then repro:
drain a device < 3.3 V → charge → reconnect → confirm the level snaps to the real
value instead of climbing over minutes. `;QI` should report `VER=00020026`.

## Branch model (new this session)
- `main` — Alex's original baselines. Not built.
- `tim` — **stable line we build/flash for testing.** Keep experimental work off it.
- `experimental` — IMU bring-up + sample-rate / `BINARY_V2` POC work. Branched from `tim`.

The IMU commit `f6d7ca9` (`v00020025`) was **reverted off `tim`** (`6de6441`, safe
revert, no force-push) and now lives on `experimental`. This also un-blocked the
`tim` build.

### Open: IMU build blocker on `experimental`
`main.c` IMU logging uses a **7-arg `NRF_LOG_INFO`** ("IMU acc … temp"), which
expands to the undefined `LOG_INTERNAL_7` (the macro supports ≤6 varargs). Split it
into two `NRF_LOG_INFO` calls before building the `experimental` line.

## Housekeeping
- `artefacts/firmware_build_matrix.md` updated: branch model, `v00020026` rows, IMU
  moved to experimental w/ blocker noted, inventory updated.
- `artefacts/howTo` shows as modified in the working tree — predates this session,
  left untouched.
