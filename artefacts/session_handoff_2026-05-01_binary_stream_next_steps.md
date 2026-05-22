# Session Handoff - 2026-05-01

## Purpose

Short handoff note for the next session focused on:

- binary BLE streaming
- higher sample rate
- preserving the battery / test-point conclusions from today

## Current firmware baseline

- Current active packaged firmware line is `v00020023`.
- Sleep-enabled stream variants are the current working baseline.
- Smart sleep, low-battery protection, BLE activity keep-awake, and charging recovery logic are already in place.
- FSR gain test change to `NRF_SAADC_GAIN1_2` is already in the current build line.

Related notes:

- [binary_stream_sample_rate_plan_2026-05-01.md](/home/salmoneller/Git/sensor-skins-firmware/artefacts/binary_stream_sample_rate_plan_2026-05-01.md)
- [firmware_update_summary_2026-05-01.md](/home/salmoneller/Git/sensor-skins-firmware/artefacts/firmware_update_summary_2026-05-01.md)
- [SLEEP_LOGIC_MATRIX.md](/home/salmoneller/Git/sensor-skins-firmware/SLEEP_LOGIC_MATRIX.md)

## Important hardware conclusion from today

The physical test points near the board corner map as:

- `TP1 = 0V`
- `TP2 = VSYS`
- `TP3 = VBAT`
- `TP4 = 3V3`

Practical consequence:

- The previously assumed "VBAT" measurement on physical `#2` was actually `VSYS`.
- `TP2 / VSYS` going to `0 V` off-puck is expected.
- `TP3 / VBAT` is the real battery rail and is the correct point to probe for battery voltage in sleep.
- The sleep firmware does not intentionally disconnect the real battery rail; it only gates the VBAT measurement path for ADC sampling.

## Battery / sleep interpretation to retain

- Forced low-battery sleep is expected below `3250 mV`.
- Wake from low-battery sleep is gated until about `3450 mV`.
- Charging alone should not fully wake the unit into normal operation; the device rechecks periodically while asleep.
- If a pad reads `0 V` off-puck, confirm whether it is `VSYS` or the switched sensing path before treating it as a battery issue.

## Streaming situation today

- Current live stream is still ASCII per row.
- Device timestamps are now embedded in the stream as `time_ms`.
- ASCII payload size is close to the BLE payload ceiling, which is the main reason sample rate cannot safely be pushed much higher yet.
- There is still no real transport-side row buffering through a disconnect.

## Recommended next implementation order

1. Implement Phase 1 binary stream scaffolding in firmware while keeping ASCII available behind a compile-time switch.
2. Validate one binary frame per measurement row first at the existing `8 Hz` loop.
3. Update the mobile side decoder only after the firmware frame format is stable.
4. Add multi-row batching once the single-row binary path is proven.
5. Increase sample rate gradually only after binary framing reduces BLE overhead.

## Suggested technical starting point

Start in the firmware stream / messaging path and introduce:

- binary frame header
- binary row payload
- sequence counter
- `frame_type`
- compile-time selection between ASCII and binary send paths

Keep these stable for the first pass:

- existing sensor scaling
- device-owned timestamps
- current main loop timing

Avoid mixing in at the same time:

- sample-rate increase
- buffering / replay
- further battery logic changes

## Candidate rate progression after binary validation

- `8 Hz` -> `10 Hz`
- `10 Hz` -> `12 Hz`
- `12 Hz` -> `15 Hz`

Only continue upward if:

- no systematic row loss appears
- the app decodes sessions reliably
- battery impact remains acceptable

## First checks for the next session

- inspect current ASCII stream packaging path in `messaging.c` and `main.c`
- identify where a binary frame can be built with minimal disruption
- confirm whether the current BLE notification path already assumes text termination or string formatting
- define an exact packed binary frame layout before touching the app

## Non-goals for the immediate next session

- do not revisit the TP mapping unless new measurements contradict it
- do not treat `TP2` as VBAT
- do not raise sample rate before binary framing is working
