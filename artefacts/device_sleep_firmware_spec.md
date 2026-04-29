# `*_sleep_{side}` Firmware Spec

## Scope

This spec defines new low-power firmware variants intended to reduce unintended battery drain while preserving deliberate use cases.

Proposed package names:

- `nostream_sleep_lhs_v00020021_sensorskins.zip`
- `nostream_sleep_rhs_v00020021_sensorskins.zip`
- `stream_sleep_lhs_v00020021_sensorskins.zip`
- `stream_sleep_rhs_v00020021_sensorskins.zip`

The intent is to base these variants on the current `v00020020` code line, then change only the auto-sleep / wake policy.

Variant split:

- `nostream_sleep_{side}`:
  no continuous BLE streaming, smart sleep enabled
- `stream_sleep_{side}`:
  continuous BLE streaming enabled, smart sleep enabled

## Goals

The firmware must handle these use cases:

1. Charging:
   the device should go to sleep reliably, even if the charge puck intermittently drops in and out.
2. Idle storage / shipping / sitting in a shoe with no active use:
   the device should go to sleep even if some FSRs remain statically loaded.
3. Active use:
   the device should stay awake while the user is wearing it and using it.
4. Bench testing:
   the device should be able to stay awake on demand even if the sensors look idle.

## Variant Policy

The smart sleep logic in this spec must be identical between the `nostream_sleep` and `stream_sleep` families.

The only intended behavioral difference between the two families is:

- `stream_sleep` has `SEND_EVERY_MEAS_OVER_BLE`
- `nostream_sleep` does not

Everything else should stay aligned:

- same battery logic
- same charging confirmation logic
- same sleep / wake thresholds
- same shipping / storage behavior
- same bench keep-awake behavior
- same session intent behavior

## Non-Goals

- This spec does not require continuous BLE streaming in all variants.
- This spec does not change VBAT measurement scaling or the battery query format.
- This spec does not require a mobile app change before the firmware can function, but the best behavior will come from app-assisted session control.

## Why The Current Logic Is Not Sufficient

The current `v00020020` sleep gate is FSR-only:

- count how many of the 19 FSRs are below `FSR_SLEEP_THRESHOLD`
- if at least `FSR_SLEEP_NUM` are below threshold for long enough, sleep

This does not distinguish:

- worn but seated
- in shoe but not worn
- packed for shipping
- bench testing

Recent CSV inspection showed that a worn, seated device can spend nearly all of its time with only 3 active FSRs, which is close enough to the existing threshold to drift into sleep. Static FSR load also cannot distinguish shipping or storage from real use.

## Design Principle

The decision to stay awake should not be based on absolute FSR pressure alone.

The firmware should separate:

- intent: whether someone explicitly wants the unit awake
- charging: whether the unit should favor sleeping
- recent activity: whether sensors are changing like a real use case
- static load: which should not by itself keep the device awake forever

## Required Inputs

The sleep state machine will use these inputs:

1. Battery state:
   from `battery_current_state()`, with `battery_charging` (`'C'`) treated as charging and `battery_ok` (`'K'`) treated as not charging for the purposes of this state machine.
2. Battery voltage:
   from `battery_pack_voltage_mv()`, reported in millivolts as integer values such as `3631` for `3.631 V`.
   The `vbat_raw_adc` value is diagnostic only and must not be used for wake / sleep thresholds.
3. FSR activity:
   counts and deltas from `fsr1` to `fsr19`.
4. CAP activity:
   counts and deltas from the CAP channels already measured in the packet.
5. BLE session intent:
   whether there has been recent BLE control activity, and optionally whether an explicit session has been started.
6. Bench keep-awake:
   an explicit test mode flag that disables auto-sleep.

## Implemented In `v00020021`

The current `v00020021` `*_sleep_*` builds implement:

- charging confirmation from repeated `C` readings
- low-voltage charging recovery sleep at `3450 mV`
- smoothed `vbat` reporting in `;QB`
- BLE-intent keep-awake using:
  - active BLE connection
  - recent BLE packet activity
  - explicit `session_active`
  - explicit `bench_keepawake`
- idle detection from recent FSR/CAP delta activity
- separate `stream_sleep_*` and `nostream_sleep_*` artifact families

The current implementation does not yet use a CAP absolute worn-detection threshold. It uses recent CAP delta activity instead.

## State Model

The firmware should maintain a simple high-level state:

- `STATE_ACTIVE`
- `STATE_IDLE_PENDING_SLEEP`
- `STATE_CHARGING_PENDING_SLEEP`
- `STATE_SLEEPING`
- `STATE_BENCH_KEEPAWAKE`

These are runtime states, not compile-time modes.

## Runtime Flags

The firmware should track these rolling flags / counters:

- `bench_keepawake`
- `session_active`
- `recent_ble_activity_counter`
- `charging_history_window`
- `idle_counter`
- `sleep_recheck_counter`

## Charging Confirmation

Charging must be confirmed over multiple checks because the current recharge pucks can flicker.

### Proposed Rule

- sample battery state every `5 s`
- store the last `5` battery states
- define `charging_confirmed = true` if at least `3` of the last `5` states are `C`
- if there are `3` consecutive samples that are not `C`, clear `charging_confirmed`
- otherwise treat the device as not charging, which in practice will usually present as `K`

### Rationale

- one transient `C` should not force sleep
- repeated `C` means the device is almost certainly on the puck
- `D` should not be relied on for state decisions because it may rarely appear with the current puck and battery behavior
- repeated non-`C` samples should explicitly clear charging state so a flickering puck does not leave the firmware stuck in a charging mode

## Charge Recovery Threshold

Charging should bias the device toward recovery sleep when the battery is still too low for useful operation.

### Proposed Rule

- if `charging_confirmed == true` and `battery_pack_voltage_mv() < 3450`, force recovery sleep
- remain in recovery sleep until `battery_pack_voltage_mv() >= 3450`
- optional hysteresis may later be added, but the initial test build should use a single wake floor of `3450 mV`

### Rationale

- `vbat` is already the corrected firmware value and is the right source of truth for this threshold
- low-voltage units should stop wasting power on BLE activity while charging
- the app should treat devices below this threshold as intentionally asleep during recharge

## Battery Query Reporting

The UI-facing `;QB` battery voltage should return the recent smoothed cached `vbat` value, not a one-shot fresh ADC voltage.

### Proposed Rule

- report the main `vbat` field from the rolling cached `battery_pack_voltage_mv()` value
- keep `RAW=...` available as a diagnostic field
- keep the existing response shape so the mobile app does not need a protocol change

### Rationale

- a short rolling average is more stable across reconnects and radio load changes
- it is still recent enough for real multimeter comparison
- it avoids stamping noisy one-shot battery reads into long blocks of recorded sensor data

## Activity Detection

The firmware should compute both absolute counts and change-over-time signals.

### FSR Derived Signals

- `fsr_delta_score`:
  count of FSR channels whose absolute change vs the previous sample exceeds `FSR_DELTA_THRESHOLD`

### CAP Derived Signals

- `cap_delta_score`:
  count of CAP channels whose absolute change vs the previous sample exceeds `CAP_DELTA_THRESHOLD`

### Implemented Constants In `v00020021`

- `FSR_DELTA_THRESHOLD = 150`
- `CAP_DELTA_THRESHOLD = 80`
- `FSR_DELTA_WAKE_MIN = 2`
- `CAP_DELTA_WAKE_MIN = 2`

These values are implemented now. CAP absolute thresholds are still intentionally not hard-coded.

## Intent Detection

The firmware should keep the device awake when there is an explicit reason to do so.

### Session Intent

Two levels are acceptable:

1. Minimal implementation:
   treat any valid BLE command or connection activity within the last `SESSION_HOLD_MS` as recent intent.
2. Better implementation:
   add an explicit BLE command to enter and exit `session_active`.

### Bench Intent

Add a dedicated BLE command to toggle `bench_keepawake`.

Suggested behavior:

- `bench_keepawake = 1` disables auto-sleep until cleared
- this is primarily for bench testing and diagnostics

### Implemented Commands

The current firmware adds:

- command `CK <0|1>` for `bench_keepawake`
- command `CX <0|1>` for explicit `session_active`

## Sleep Decision Tree

Each main loop iteration should evaluate the following in order:

1. If `bench_keepawake == true`:
   stay awake
2. Else if `session_active == true`:
   stay awake
3. Else if recent BLE activity is within `SESSION_HOLD_MS`:
   stay awake
4. Else if `charging_confirmed == true`:
   enter charging-idle evaluation
5. Else:
   enter normal-idle evaluation

### Charging-Idle Evaluation

If charging is confirmed:

- if `battery_pack_voltage_mv() < 3450`, enter recovery sleep immediately
- if `fsr_delta_score` is low
- and `cap_delta_score` is low
- and this condition persists for `CHARGING_SLEEP_TIMEOUT_MS`
- then sleep

Recommended starting timeout:

- `CHARGING_SLEEP_TIMEOUT_MS = 15000`

Static FSR preload should not block sleep while charging.

### Normal-Idle Evaluation

If not charging:

- if `fsr_delta_score` is high, stay awake
- else if `cap_delta_score` is high, stay awake
- else if BLE intent is present, stay awake
- else accumulate idle time

Implemented constants:

- `IDLE_SLEEP_TIMEOUT_MS = 180000`
- `SESSION_HOLD_MS = 120000`

Interpretation:

- real movement keeps the device awake
- explicit BLE use keeps it awake
- static pressure alone does not
- no-session / no-motion devices eventually sleep, which covers shipping and storage

## Worn Detection

The current implementation does not attempt a dedicated absolute worn/not-worn classification.

Instead it uses:

- recent CAP delta activity
- recent FSR delta activity
- BLE/session intent

This is deliberate because static preload and seated postures make absolute pressure thresholds unreliable.

## Sleep Entry Behavior

Before sleeping:

- flush any pending summary data if required
- force BLE disconnect
- enter the existing low-power sleep loop

## Sleep Recheck Behavior

While sleeping:

- wake every `SLEEP_CHECK_EVERY_MS`
- refresh battery state history
- re-measure FSR/CAP
- exit sleep if any of the following become true:
  - `bench_keepawake == true`
  - `session_active == true`
  - recent BLE activity is present
  - `fsr_delta_score` or `cap_delta_score` indicates renewed activity

Charging should not by itself wake the unit.

## Shipping / Storage Handling

Shipping is treated as a special case of normal idle:

- static FSR load may be present
- battery state may be `K`
- there is no BLE interaction
- there is no real sensor motion

Therefore shipping should sleep under the normal idle path after the longer idle timeout.

This is the main reason to avoid using absolute FSR level as the only wake condition.

## Observability Limits

There is currently no reliable BLE-level way to distinguish:

- asleep by policy
- battery too low to boot
- battery fully dead
- brownout / unstable boot
- other non-advertising failure states

If a device is not advertising or not connecting, the authoritative field diagnostic remains:

- measure battery voltage on the PCB test points with a multimeter

This hardware/protocol limit is acceptable for now as long as the firmware sleep logic is predictable and minimizes unnecessary drain before the unit reaches a non-bootable state.

## Compile-Time Variant Controls

Add a dedicated compile-time variant flag, for example:

- `ENABLE_SLEEP_SMART_IDLE`

Optional companion flags:

- `ENABLE_BENCH_KEEPAWAKE_CMD`
- `ENABLE_SESSION_ACTIVE_CMD`

This allows the current `nostream` and `stream` variants to remain available while the new behavior is trialed in separate `nostream_sleep` and `stream_sleep` image families.

## Artifact Naming

The package name should expose both transport mode and sleep policy.

Recommended names:

- `nostream_sleep_lhs_v00020021_sensorskins.zip`
- `nostream_sleep_rhs_v00020021_sensorskins.zip`
- `stream_sleep_lhs_v00020021_sensorskins.zip`
- `stream_sleep_rhs_v00020021_sensorskins.zip`

## Implementation Notes

Recommended implementation order:

1. Add helper functions to compute:
   - `fsr_delta_score`
   - `cap_delta_score`
2. Add rolling charging history based on `battery_current_state()`
3. Add the new state machine in `main.c`
4. Add `bench_keepawake` command
5. Add optional explicit `session_active` command
6. Add packaging support for the `nostream_sleep_*` and `stream_sleep_*` artifact prefixes

## Test Matrix

The new variant should be tested in at least these scenarios:

1. On charge puck, stable charge
   expected: sleeps quickly
2. On charge puck, intermittent `C/K/C/K`
   expected: sleeps after repeated charging confirmation
3. In shoe, worn, standing
   expected: stays awake
4. In shoe, worn, seated, feet hanging
   expected: stays awake
5. In shoe, not worn
   expected: sleeps after idle timeout
6. Out of shoe, bench test with keep-awake enabled
   expected: stays awake
7. Shipping simulation with static preload
   expected: sleeps after idle timeout

## Open Data Needed

Before final threshold tuning, collect:

- one charging CSV with orthotic off-foot on the puck
- one off-foot / in-shoe idle CSV
- one shipping-style static preload CSV

Those captures are mainly needed to tune CAP absolute thresholds and delta thresholds safely.
