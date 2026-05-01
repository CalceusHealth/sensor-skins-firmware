# Binary Stream and Sample-Rate Plan

## Objective

Increase practical streaming sample rate while preserving row fidelity and making the transport more robust than the current ASCII-per-row design.

## Constraints from the current firmware

- Current loop interval is `125 ms` (`8 Hz`).
- Current stream is ASCII and now includes `time_ms` plus all sensor values.
- The ASCII row size is near the BLE payload ceiling at negotiated MTU `247`.
- Live rows are not buffered through a true disconnect.
- Current app and tooling are transitioning toward device-owned timestamps.

## FSR workstream

### Completed

- Added configurable `FSR_ADC_GAIN`.
- Current test build uses `NRF_SAADC_GAIN1_2`.
- High-force jumping appears less saturated than the previous gain setting.

### Remaining

1. Validate low-force behaviour with controlled standing and seated tests.
2. Keep `GAIN1_2` if low-force sensitivity remains acceptable.
3. Only consider `GAIN1_4` if high-force clipping still appears in running or impact sessions.

## Stream protocol workstream

### Phase 1: Binary protocol scaffolding

Goal:

- introduce a compact binary frame format without removing the existing ASCII path

Deliverables:

- compile-time switch between ASCII stream and binary stream
- binary frame header definition
- binary row payload definition
- sequence numbering
- device timestamp ownership retained

Success criteria:

- firmware builds cleanly
- one binary frame can be sent for one measurement row
- current ASCII path remains available

### Phase 2: Multi-row batching

Goal:

- pack multiple measurement rows into a single BLE notification

Deliverables:

- rows-per-frame calculation based on BLE MTU
- batch builder with row count and base timestamp
- compact per-row delta timestamp encoding

Success criteria:

- at negotiated MTU `247`, fit up to 3 rows in one frame
- notification rate drops while sample row rate stays the same

### Phase 3: Rate increase

Goal:

- raise sample rate only after transport overhead is reduced

Candidate steps:

1. `8 Hz` -> `10 Hz`
2. `10 Hz` -> `12 Hz`
3. `12 Hz` -> `15 Hz`
4. reassess beyond `15 Hz` after real transport measurements

Success criteria:

- no systematic row loss on stable links
- app/native receiver can decode full sessions reliably
- battery impact remains acceptable

### Phase 4: Row buffering

Goal:

- tolerate short BLE stalls or reconnect windows without immediate row loss

Deliverables:

- ring buffer for live rows
- decoupled sampling and transport
- explicit overflow accounting

Success criteria:

- short transport stalls do not lose rows
- any dropped rows become measurable and reportable

## Proposed binary frame

### Header

- `magic` `uint16`
- `version` `uint8`
- `frame_type` `uint8`
- `flags` `uint8`
- `row_count` `uint8`
- `sequence` `uint16`
- `base_time_ms` `uint64`

### Per-row payload

- `delta_time_ms` `uint16`
- `fsr[19]` `uint16`
- `temp[5]` `int16`
- `cap[12]` `uint16`

## Encoding assumptions

- `base_time_ms` is the UTC-synced firmware time base.
- `delta_time_ms` is relative to `base_time_ms`.
- FSR and capacitance values remain in the same scaled domain initially to reduce simultaneous app changes.
- Raw ADC transport can be added later if needed for diagnostics.

## Risks

- Binary stream mode depends on negotiated BLE MTU being large enough for the selected frame size.
- Raising sample rate before batching and buffering will still be fragile.
- FSR range changes may reduce low-force sensitivity if pushed too far.

## Immediate next step

Implement Phase 1 in firmware now:

- add binary stream protocol definitions
- add binary frame builder
- keep ASCII as the default path for compatibility
- prepare main loop for future batching
