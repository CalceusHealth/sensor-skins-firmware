# Battery Debug Summary - 2026-04-17

## Scope

This note captures the firmware and mobile-app work done to restore:

- OTA flashing on the RHS device
- continuous BLE streaming for the mobile app
- a live `;QB` battery query path
- battery voltage reporting that matches multimeter readings
- app-side handling of battery voltage, percentage, color, and raw ADC storage

It also records the significant test values reported during the session.

## Repos and Reference Artefacts

- Firmware repo: `/home/salmoneller/Git/sensor-skins-firmware`
- Mobile repo: `/home/salmoneller/Git/sensor-skins-mobile`
- Key context notes:
  - [firmware-context.md](/home/salmoneller/Git/sensor-skins-firmware/artefacts/firmware-context.md)
  - [carbon-circuits-1](/home/salmoneller/Git/sensor-skins-firmware/artefacts/carbon-circuits-1)
- Hardware references:
  - [Reid Orthotic v2 R4.pdf](/home/salmoneller/Git/sensor-skins-firmware/artefacts/Reid%20Orthotic%20v2%20R4.pdf)
  - [Routejade-FLPB301031-HPMW30-30.pdf](/home/salmoneller/Git/sensor-skins-firmware/artefacts/Routejade-FLPB301031-HPMW30-30.pdf)

## High-Level Outcome

- OTA flashing over BLE was confirmed working for the RHS device.
- The battery path was eventually made live again by moving away from stale cached values and using a direct/fresh `;QB` read path.
- The remaining error after that was scaling, and the empirical fix that matched hardware was:
  - `VDIV_VBAT_R1 = 21`
  - `VDIV_VBAT_R2 = 10`
- Continuous streaming was restored in the latest packaged firmware.
- The mobile app was updated so battery UI uses firmware voltage, not the old app-side correction factor and not the raw ADC value.
- The app now also stores `RAW=...` into CSV as `vbat_raw_adc`.

## Important Findings

- The original R7 build that was first flashed was protocol-only, not continuous streaming.
- A stale cached battery value is not acceptable for `;QB`; the correct product behavior is a live query on demand.
- Query-time battery reads were colliding with the sensor/SAADC usage in some versions.
- The direct/fresh VBAT path eventually produced valid nonzero raw ADC values.
- Once the direct path was working, the remaining issue was not timing but scaling.
- The schematic showed `R43 = 100k` and `R44 = 100k`, but the in-circuit behavior did not match a simple `1:1` divider in practice.
- Empirical calibration from live hardware testing indicated an effective scale ratio close to `3.1`, implemented as `21/10`.

## Hardware Notes

From the R4 schematic:

- `ADC_VBAT` maps to `P0.05 / AIN3`
- `VBAT_ON` maps to `P0.06`

From physical probing on the PCB:

- Test point `#1` was already known/confirmed by the user to be ground.
- Measured examples:
  - black on `#1`, red on `#2` gave about `3.41 V`
  - with recharge puck on the coil:
    - `#2` about `4.06 V`
    - `#3` about `4.17 V`

Interpretation:

- `#2` appears to be the relevant battery-related node for comparison against `;QB`
- `#3` appears to be a higher charging-related rail/node

## Firmware Timeline

### Early state

- OTA flash succeeded and the device came back as `REIDRHS`.
- `;QI` returned:
  - `;RI UID=805DEC82,VER=00020008`
- `;QB` returned:
  - `;RB 0 0 L`
- Other protocol sanity checks:
  - `;RT 479`
  - `;RP 4294967295,0`
  - `;RA 0,0`

Interpretation:

- firmware was alive
- streaming behavior had changed
- battery query path existed but returned invalid data

### Mobile app sanity check during earlier firmware

- App showed about `3.31 V`
- Multimeter showed about `3.53 V`

Interpretation:

- there was under-reporting
- at that stage it was not yet clear whether the issue was the app, firmware scaling, or query timing

### 0x00020009

Intent:

- restore streaming
- start integrating battery fix work

Outcome:

- flash succeeded
- user could connect
- streaming behavior and battery logic still needed work

### 0x0002000A

Intent:

- protocol-only / no-streaming test image to simplify battery debugging

Outcome:

- `;QB` initially returned nothing
- a follow-up fix made it respond again, but with:
  - `;RB 0 0 U`

Interpretation:

- command path was working
- VBAT measurement path was not

### 0x0002000B and 0x0002000C

Intent:

- try closer-to-old behavior and then a `VBAT_ON` polarity flip

Outcome:

- both still returned:
  - `;RB 0 0 U`

Interpretation:

- not a stale-image problem
- not fixed by `VBAT_ON` polarity change alone

### 0x0002000D

Intent:

- diagnostic `;QB` response to dump high/low enable-state raw values and charger pin states

Outcome:

- user reported all values were `0`

Interpretation:

- the measurement path still was not being exercised correctly in that context

### 0x0002000E

Intent:

- sample battery in the main loop after `measure_sensors()` and return cached value
- align with Alex's theory that battery was being sampled at the wrong time

Outcome:

- first real nonzero response:
  - `;RB 100 4479 K`
- multimeter:
  - `3.20 V`

Interpretation:

- timing mattered
- battery path was alive again
- scaling was badly wrong in the high direction

### 0x0002000F

Intent:

- keep timing fix
- revert divider math to `1:1`

Outcome:

- `;RB 36 3730 K`
- multimeter:
  - `3.10 V`

Interpretation:

- much closer than `4479 mV`
- still over-reading by about `0.63 V`

### 0x00020010

Intent:

- add `RAW=...` to the cached battery path

Outcome:

- `;RB 0 0 U RAW=0`

Interpretation:

- cached sample was still failing in that variant

### 0x00020011

Intent:

- move `battery_update()` before `measure_sensors()`

Outcome:

- `;RB 92 4064 K RAW=0`
- multimeter:
  - `3.36 V`

Interpretation:

- reported millivolts were stale/non-live
- `RAW=0` confirmed fresh acquisition had still failed

### 0x00020012

Intent:

- give battery measurement its own loop slot once per second

Outcome:

- user then supplied board probing context instead of a standalone versioned result
- at this stage the key conclusion remained that cache-first behavior was not good enough

### 0x00020013

Intent:

- make `;QB` force an isolated live sample

Outcome:

- `;RB 96 4106 K RAW=0`

Interpretation:

- still stale
- fresh sample still not succeeding

### 0x00020014

Intent:

- true direct-read `;QB` with explicit SAADC reset/re-init around VBAT

Outcome:

- charging:
  - `;RB 100 7010 C RAW=2969`

Interpretation:

- this was the turning point
- the direct raw sample was alive
- scaling math was clearly wrong because `7010 mV` is impossible for a single LiPo cell

### 0x00020015

Intent:

- use the same fresh raw sample for conversion
- divide by `ADC_AVG_SAMPLES`

Reported results:

- charging:
  - multimeter `4.11 V`
  - `;RB 0 2624 C RAW=2987`
- coil on:
  - multimeter `4.0 V`
  - `;RB 0 255 C RAW=2909`
  - note: `255` was captured verbatim from chat and may have been a typo for `255x mV`
- coil off:
  - multimeter `3.39 V`
  - `;RB 0 2228 K RAW=2536`
- later, after 15 minutes charging:
  - charging:
    - multimeter `4.18 V`
    - `;RB 0 2666 C RAW=3035`
  - not charging:
    - multimeter `3.81 V`
    - `;RB 0 2443 K RAW=2781`

Interpretation:

- direct/live reading was now working
- the remaining error was a consistent under-read by about `1.56x`
- this gave the empirical calibration target for the divider ratio

### 0x00020016

Intent:

- keep direct/fresh `;QB`
- apply empirical scaling with:
  - `VDIV_VBAT_R1 = 21`
  - `VDIV_VBAT_R2 = 10`

Reported results:

- not charging:
  - multimeter `3.61 V`
  - `;RB 10 3580 K RAW=2629`
- charging:
  - multimeter `4.12 V`
  - `;RB 71 4081 C RAW=2997`

Interpretation:

- battery voltage became accurate enough to call fixed
- error was only about `30-40 mV`

### 0x00020017

Intent:

- keep the working direct/fresh `;QB`
- keep the `21/10` scaling
- keep `RAW=...`
- turn continuous streaming back on for the mobile app

Built/package status:

- build completed
- DFU zip was produced at:
  - [artefacts/out/reid_ble_aginic_v2_r7_rhs_dfu.zip](/home/salmoneller/Git/sensor-skins-firmware/artefacts/out/reid_ble_aginic_v2_r7_rhs_dfu.zip)

This version was the handoff point for app/front-end testing.

## Mobile App Changes

The mobile app battery path was updated to align with the corrected firmware behavior.

### What changed

- The old app-side voltage correction factor is no longer used.
- The app now treats firmware `mv` as the source of truth for displayed battery voltage.
- The app now derives displayed percentage from the calibrated voltage, not from the firmware percentage and not from `RAW`.
- `RAW=...` is parsed and kept for diagnostics/storage only.
- CSV recording now includes:
  - `vbat`
  - `vbat_raw_adc`

### Files changed

- [src/utils/battery.ts](/home/salmoneller/Git/sensor-skins-mobile/src/utils/battery.ts)
- [src/services/BLEService.ts](/home/salmoneller/Git/sensor-skins-mobile/src/services/BLEService.ts)
- [src/hooks/useDeviceConnection.ts](/home/salmoneller/Git/sensor-skins-mobile/src/hooks/useDeviceConnection.ts)
- [src/services/RecordingService.ts](/home/salmoneller/Git/sensor-skins-mobile/src/services/RecordingService.ts)

### Intended UI behavior after the fix

- Voltage shown in the UI should be the calibrated firmware voltage
- Percentage shown in the UI should be interpolated from that voltage
- Color should be based on that interpolated percentage plus charging state
- `RAW` should not drive user-visible battery percentage or color
- `RAW` should be stored in CSV for fleet-wide later analysis

## CSV / Sample Rate Note

Sample file checked:

- `/home/salmoneller/Downloads/3af599c0-67f2-4b02-ac96-6abd76800ed6_right.csv`

Observed columns include:

- `timestamp`
- `vbat`
- `vbat_raw_adc`

Observed first rows showed:

- firmware column `2.0.6`
- `vbat = 3814`
- `vbat_raw_adc = 2800`

### Rate analysis

Naive all-row average:

- 400 rows
- average delta about `0.0692 s`
- average about `14.44 Hz`

But the timestamps are bursty, with many very short `1-3 ms` gaps. That makes the naive rate misleading.

When ignoring sub-`10 ms` gaps:

- average delta about `0.1072 s`
- average about `9.33 Hz`
- median delta about `0.121 s`
- median about `8.26 Hz`

When ignoring sub-`50 ms` gaps:

- average delta about `0.1205 s`
- average about `8.30 Hz`
- median delta about `0.125 s`
- median about `8.0 Hz`

Interpretation:

- the effective sensor cadence still looks much closer to `8 Hz`
- the `~15 Hz` impression appears to come from bursty timestamp delivery in the app/CSV, not a true doubling of the sampling loop

## Remaining Issues At Handoff

- The user reported there are still front-end issues to inspect later.
- The CSV sample suggested bursty timestamp delivery that should be reviewed before claiming any sample-rate increase.
- The CSV firmware column showed `2.0.6`, which should be checked against the intended reporting/version mapping if that matters to downstream tooling.

## Multi-Device Validation - 2026-04-18

Additional fleet testing was carried out on the no-stream battery-query build:

- version `0x00020018`
- live/direct `;QB`
- `RAW=...` enabled
- calibrated divider ratio `21/10`
- streaming disabled for cleaner manual measurements

### Test-point confirmation

The battery measurement test points were cross-checked against a separate electronics-only device by probing:

- directly on the battery terminals
- then on the PCB test points

This confirmed that the same PCB test points were being used correctly for the multimeter comparisons.

### Device batch results

Format below:

- charging: `QB(mV/state/raw)` vs multimeter
- not charging: `QB(mV/state/raw)` vs multimeter

#### `D6:A6:68:D0:93:31`

Charging:

- `97% 4231 C RAW=3106` vs `4.01 V`
- `91% 4187 C RAW=3074` vs `4.01 V`
- `9% 3836 C RAW=2816` vs `4.01 V`
- `98% 4239 C RAW=3113` vs `4.01 V`
- `90% 4176 C RAW=3066` vs `4.01 V`

Not charging:

- `9% 3841 K RAW=2820`

Interpretation:

- This device is inconsistent and remains the outlier.
- Most charging readings over-report against the `4.01 V` multimeter value by roughly `+166 mV` to `+229 mV`.
- One charging reading (`3836 mV`) is much lower than the others while still marked charging.
- This device should not be used to drive calibration decisions.
- This aligns with the suspicion that the first-batch battery/device may be degraded or otherwise problematic.

#### `DE:19:5E:5B:59:A4`

Charging:

- `2% 3277 C RAW=2406` vs `3.28 V`
- `7% 3672 C RAW=2696` vs `3.71 V`
- `7% 3678 C RAW=2701` vs `3.71 V`

Not charging:

- `4% 3329 K RAW=2445` vs `3.24 V`
- `2% 3209 K RAW=2357` vs `3.20 V`
- `17% 3640 K RAW=2673` vs `3.67 V`
- `18% 3650 K RAW=2681` vs `3.66 V`
- `17% 3637 K RAW=2670` vs `3.66 V`
- `11% 3588 K RAW=2634` vs `3.59 V`

Interpretation:

- This device looks broadly good.
- Most readings are close to multimeter, typically within a few tens of millivolts.
- The firmware percentage values are clearly less stable/less trustworthy than the millivolt field.

#### `E6:45:A0:2A:0B:80`

Charging:

- `10% 3852 C RAW=2892` vs `3.86 V`
- `10% 3852 C RAW=2829` vs `3.86 V`
- `9% 3841 C RAW=2820` vs `3.83 V`

Not charging:

- `77% 3920 K RAW=2878` vs `3.82 V`
- `63% 3833 K RAW=2814` vs `3.82 V`
- `63% 3833 K RAW=2815` vs `3.82 V`
- `64% 3838 K RAW=2819` vs `3.82 V`
- `62% 3825 K RAW=2808` vs `3.81 V`

Interpretation:

- This device looks broadly good on voltage.
- Not-charging readings are very repeatable.
- The `3920 mV` row is a mild high-side outlier against `3.82 V`, but the rest cluster closely.
- Percentage values again should be treated as provisional compared with the voltage field.

#### `F5:5F:1A:B0:2E:B5`

Charging:

- `9% 3819 C RAW=2805` vs `3.86 V`
- `9% 3847 C RAW=2824` vs `3.86 V`
- `9% 3838 C RAW=2819` vs `3.86 V`
- `13% 3868 C RAW=2840` vs `3.89 V`

Not charging:

- `67% 3852 K RAW=2829` vs `3.86 V`
- `65% 3844 K RAW=2822` vs `3.86 V`

Interpretation:

- This device looks very good.
- Voltage is tightly aligned with the multimeter on both charging and not-charging readings.

#### `EC:CA:AD:DD:62:DC`

Charging:

- `7% 3672 C RAW=2696` vs `3.64 V`
- `8% 3716 C RAW=2729` vs `3.65 V`
- `7% 3689 C RAW=2709` vs `3.66 V`
- `8% 3749 C RAW=2752` vs `3.72 V`

Not charging:

- `33% 3719 K RAW=2731` vs `3.69 V`
- `33% 3721 K RAW=2732` vs `3.69 V`

Interpretation:

- This device also looks broadly good.
- Voltage is close to multimeter and repeatable.

### Batch-level conclusion

- The newer-batch devices (`DE`, `E6`, `F5`, `EC`) broadly validate the live `;QB` voltage path and the `21/10` scaling.
- The first-batch device (`D6`) remains the clear outlier and likely reflects hardware/battery condition rather than a global calibration problem.
- Across these tests, the **millivolt value is the reliable field**.
- The **percentage field is not yet ready to be treated as authoritative**, especially across charging transitions and across devices.

## Todo For Tomorrow

- Improve battery UI behavior for different states:
  - charging vs not charging should not be presented as if they are directly comparable instantaneous battery states
  - review percentage behavior immediately after the recharge puck is removed
  - consider smoothing, hysteresis, or separate charging/discharging interpretation so the voltage drop after unplugging does not look like a bug
  - review the color/label logic so charging and resting states are visually clearer
- Flash and test multiple additional devices:
  - validate that the same `;QB` live-read path and `21/10` scaling behaves well across more units
  - compare `vbat` and `vbat_raw_adc` across devices using the same Routejade cell
  - collect multimeter vs `;QB` comparisons for each device
  - use the new CSV `vbat_raw_adc` field to assess battery and ADC variation across the fleet

## Recommended Next Checks

- Verify `;QI` and `;QB` on the latest streaming-enabled build
- Confirm the app battery widget uses voltage-derived percentage/color exactly as intended
- Inspect one fresh recorded CSV row and ensure:
  - `vbat` tracks the corrected firmware voltage
  - `vbat_raw_adc` is populated
- Review why timestamps are clustered/bursty in CSV before changing any expected sample-rate assumptions
