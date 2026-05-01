# Sensor Skins Firmware Update Summary

## Current firmware baseline

The `tim` firmware line has been updated from the earlier `v00020021` baseline through `v00020022` and the current FSR test build `v00020023`.

These changes have been focused on device resilience, safer low-battery behaviour, better timestamp fidelity, and improved force-sensor headroom.

## Firmware resilience and device protection changes

### Device protection and recovery

- Code protection is enabled in release firmware.
- Hard fault recovery handling is enabled.
- Error-tolerant handling is enabled to reduce full-device lockups from transient faults.

### Sleep and idle protection

- Sleep handling is enabled.
- Smart idle sleep is enabled so the device can protect itself when there is no meaningful BLE or sensor activity.
- The firmware now uses BLE activity, sensor deltas, charging state, and low-battery state to decide whether the device should remain awake or enter sleep.

### Low-battery and charging protection

- Low-battery sleep thresholds are enforced.
- Wake thresholds after charging are enforced separately.
- Charging recovery thresholds are enforced to avoid unstable behaviour around depleted battery voltage.
- Charging idle timeout and general idle timeout are both used to reduce unnecessary battery drain while preserving session behaviour.

These changes materially improve device protection against:

- deep battery drain
- unstable behaviour near low voltage
- unnecessary battery consumption when idle
- remaining awake indefinitely after charging or inactive sessions

## Device identity and timestamp changes

### Cross-platform device identity

- The firmware now uses the Nordic chip `DEVICEID[1:0]` as the device identifier.
- This provides a firmware-owned identifier that is stable across iOS and Android.
- This replaces reliance on platform-specific identifiers such as iOS CoreBluetooth peripheral UUIDs or BLE MAC-derived identifiers.

### Device-owned timestamps in the stream

- Live streamed sensor rows now include firmware `time_ms` as the first field in the ASCII stream.
- The mobile app can now record the time the device actually collected the sample, rather than the time the phone happened to receive or write it.

This improves analysis quality by removing:

- app-side receipt-time distortion
- reconnect burst timestamp clustering
- CSV timestamps that reflected write timing rather than collection timing

### Time sync model

- The firmware already supports `SETTIME`.
- The mobile app now needs to sync device time on connect and reconnect so `time_ms` represents UTC-derived epoch time.

## Battery reporting changes

### `QB` battery query improvement

- `QB` now takes a fresh raw ADC battery reading for the `RAW=` value instead of relying on the stale cached path.
- The averaged battery voltage path is retained for the UI so voltage and percentage remain visually stable.

This gives:

- smoothed battery voltage and percentage for the app UI
- a real fresh raw reading for logs and diagnostics

This addresses the earlier issue where:

- averaged battery voltage could remain stale
- `RAW=0` could be returned after ADC contention
- the logs were not good enough to evaluate battery correction behaviour

## FSR measurement headroom change

### Problem observed

During jumping and high-force intervals, multiple FSR channels were repeatedly pinning at the same ceiling, indicating likely acquisition saturation rather than a simple display-scale limit.

### Change made

- FSR SAADC gain has been reduced from `NRF_SAADC_GAIN1` to `NRF_SAADC_GAIN1_2`.
- This is now controlled through `FSR_ADC_GAIN` in firmware configuration.

### Effect

- High-force sessions show materially less repeated ceiling clipping.
- Peak values are more spread and natural rather than flattening at one repeated maximum.
- This should improve interpretation of running and impact events.

### Current status

- The initial anecdotal jump comparison looks better with the new gain.
- Further controlled testing is still needed to confirm the low-force tradeoff is acceptable.

## Current limitations still present

- Live streamed rows are not buffered through a true BLE disconnect.
- If the BLE link drops, the missing live rows are currently lost rather than replayed.
- The current streaming format is still ASCII and still consumes most of the BLE payload budget.
- This limits safe sample-rate increases.

## Proposed next phase

The next firmware phase is to move from ASCII row streaming to a compact binary stream protocol.

The goals are:

- reduce BLE transport overhead
- batch multiple rows per notification
- increase practical sample rate
- preserve device-owned timestamps
- prepare for later buffering and replay work if required

## Proposed sample-rate work

The recommended path is:

1. Introduce a binary stream protocol alongside the current ASCII path.
2. Validate the binary stream with the current sampling loop first.
3. Increase sample rate incrementally once transport overhead has been reduced.
4. Add batching and then a real row buffer so short BLE stalls do not immediately lose live rows.

## Summary

The firmware is now substantially more resilient than the older baseline:

- better battery protection
- better low-voltage behaviour
- better sleep and idle management
- firmware-owned stable device identity
- firmware-owned timestamps in streamed rows
- improved battery diagnostics
- improved FSR headroom under high force

The next major gain will come from replacing ASCII streaming with a batched binary transport so sample rate can increase without overloading the current BLE path.
