# Calceus Serial Protocol v2

**Revision 2.0 — last updated 2026-05-05**

Applies to firmware `DEVICE_FW_VERSION = 0x00020024` (Sensor Skins firmware
`ble_app_firmware_v2_R7`).

This document supersedes *Calceus Serial Protocol v1* (2022-03-24). The packet
framing is unchanged; the changes are:

- New `STREAM=` field in the `QI` system-info response, identifying the device
  stream protocol capability (`ASCII_V1_TS`, `BINARY_V2`, or `UNKNOWN`).
- `QB` battery query now returns averaged `%`, `mV`, single-letter state, and
  a fresh raw ADC reading for diagnostics (was `mV` only).
- `reid_ble_packet_t` and `reid_ble_summary_packet_t` expanded to 19 FSRs,
  5 temperature sensors, and 6 capacitive pads (12 s/n channels).
- Continuous streaming (`SEND_EVERY_MEAS_OVER_BLE`) is now a build-time option
  with two on-the-wire formats: timestamped ASCII (`ASCII_V1_TS`) or framed
  binary (`BINARY_V2`).
- New commands: `CK` (bench keep-awake) and `CX` (explicit session active).
- New sleep / lifeline behaviour: while the device is in smart-idle sleep, BLE
  continues advertising at a slow 5 s interval ("lifeline") so the controller
  can reconnect without waiting for sensor activity.

## Serial Details

Communication is over the Nordic UART Service (BLE), with the application as
controller and the insole as peripheral. The insole is named either `REIDLHS`
or `REIDRHS`; individual insoles are identified by MAC address.

- **RX Characteristic** UUID `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
  Controller writes packets to this characteristic. ATT Write Request or ATT
  Write Command may be used.
- **TX Characteristic** UUID `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
  With notifications enabled, the device sends packets to the controller as
  notifications.

## Packet Format

```text
;(type)(subtype)[space][data…](line end)
```

- `;` — packet start. Decoders must discard all bytes until the start byte is
  seen.
- `(type)` — one of:
  - `Q` Query (controller → device)
  - `R` Response (device → controller)
  - `C` Command (controller → device)
  - `A` Acknowledge (device → controller)
  - `E` Error (either direction)
- `(subtype)` — single character identifying the specific query, command, or
  error class.
- `[space][data…]` — optional payload, prefixed by a space. Required where the
  query/command needs arguments or returns data.
- `(line end)` — `\r`, `\n`, or both. Devices send both. Decoders accept
  either. Repeated end characters are ignored until the next `;` start byte.

Streamed packets (when `SEND_EVERY_MEAS_OVER_BLE` is enabled) are pushed by the
device without a controller query and follow the format described in
[Streaming Protocols](#streaming-protocols).

## Error Types

| Error Type | Char | Example | Description |
|---|---|---|---|
| Bad Command | `C` | `;EC\r\n` | Unsupported command, or required argument missing or malformed. |
| Bad Query | `Q` | `;EQ\r\n` | Unsupported query, or required argument missing or malformed. |
| Protocol Error | `M` | `;EM\r\n` | Packet framing error (e.g. no end seen, payload overrun). |
| No Data | `N` | `;EN\r\n` | The requested record / data is not in memory. |
| Unknown Error | `U` | `;EU\r\n` | Catch-all (e.g. baud-rate mismatch on UART transports). |

## Query / Response Types

| Query | Char | Example | Description |
|---|---|---|---|
| System Info | `I` | `;QI\r\n` → `;RI UID=0123456789ABCDEF,VER=00020024,STREAM=ASCII_V1_TS\r\n` | Returns 64-bit Nordic device ID, firmware version, and the device's compiled stream protocol (`ASCII_V1_TS`, `BINARY_V2`, or `UNKNOWN`). |
| Battery Level | `B` | `;QB\r\n` → `;RB 87 4051 K RAW=8421\r\n` | Returns averaged charge `%`, averaged battery voltage in `mV`, single-letter state, and a fresh raw ADC reading. State characters: `K` ok, `C` charging, `D` charged, `L` low, `U` unknown. The raw value is taken from a fresh `VBAT` measurement after the response is requested. |
| Time | `T` | `;QT\r\n` → `;RT 1648097508\r\n` | Device time in seconds. Seconds-since-boot until `CT` is used to set an epoch. |
| All record indices | `A` | `;QA\r\n` → `;RA 35,102\r\n` | Low and high index of all records currently stored. |
| Pending record indices | `P` | `;QP\r\n` → `;RP 97,102\r\n` | Low and high index of records not yet marked as synced. |
| Record (single) | `R` | `;QR 97\r\n` → `;RR <binary blob>\r\n` | Returns the record at the given index as a `reid_ble_summary_packet_t` blob (see below). |
| Multiple records | `M` | `;QM 97,102\r\n` → `;RM <blob>\r\n` × N | Returns all records in `[low,high]` as separate `;RM` notifications. Maximum 101 records per request (`MSG_MAX_RECORDS_PER_REQUEST`). All records in the range are returned, including those already marked as synced. If the range contains no records, `;EN\r\n` is sent. |
| Last data | `L` | `;QL\r\n` → `;RL <binary blob>\r\n` | Returns the most recent measurement as a full `reid_ble_packet_t` blob (includes `vdd_mv`). |
| Fresh data | `F` | `;QF\r\n` → `;RF <binary blob>\r\n` | Returns the most recent measurement as a full `reid_ble_packet_t` blob (includes `vdd_mv`). The "fresh measurement on demand" behaviour from v1 is currently disabled in firmware; the response payload is the same as `QL`. |

### `reid_ble_packet_t` (live measurement blob — `RL` and `RF`)

```c
#pragma pack(push,1)
typedef struct reid_ble_packet_t {
    uint64_t time_ms;
    uint16_t vdd_mv;
    uint16_t fsr1;     // 19 FSR channels
    uint16_t fsr2;
    /* ... fsr3 .. fsr18 ... */
    uint16_t fsr19;
    int16_t  temp1;    // 5 temperature channels (LMT01 raw, signed)
    int16_t  temp2;
    int16_t  temp3;
    int16_t  temp4;
    int16_t  temp5;
    uint16_t cap1s;    // 6 capacitive pads, each with south (s) and north (n)
    uint16_t cap1n;
    uint16_t cap2s;
    uint16_t cap2n;
    uint16_t cap3s;
    uint16_t cap3n;
    uint16_t cap4s;
    uint16_t cap4n;
    uint16_t cap5s;
    uint16_t cap5n;
    uint16_t cap6s;
    uint16_t cap6n;
} reid_ble_packet_t;
#pragma pack(pop)
```

### `reid_ble_summary_packet_t` (stored record blob — `RR` and `RM`)

```c
#pragma pack(push,1)
typedef struct reid_ble_summary_packet_t {
    uint32_t time_s_start;
    uint32_t time_s_end;
    uint32_t index;
    uint16_t is_synced;
    uint16_t num_samples;
    uint16_t vdd_mv_min;
    uint16_t fsr1_max;     // 19 FSR maxima
    /* ... fsr2_max .. fsr18_max ... */
    uint16_t fsr19_max;
    int16_t  temp1_max;    // 5 temperature maxima
    int16_t  temp2_max;
    int16_t  temp3_max;
    int16_t  temp4_max;
    int16_t  temp5_max;
    int16_t  cap1_delta_min;   // 6 capacitive deltas (min/max)
    int16_t  cap1_delta_max;
    int16_t  cap2_delta_min;
    int16_t  cap2_delta_max;
    int16_t  cap3_delta_min;
    int16_t  cap3_delta_max;
    int16_t  cap4_delta_min;
    int16_t  cap4_delta_max;
    int16_t  cap5_delta_min;
    int16_t  cap5_delta_max;
    int16_t  cap6_delta_min;
    int16_t  cap6_delta_max;
} reid_ble_summary_packet_t;
#pragma pack(pop)
```

A new summary record is generated every `NEW_SUMMARY_EVERY_N` measurements
(currently 300 s of measurements at the configured `MAIN_LOOP_TIME_MS`).

## Command / Acknowledge Types

| Command | Char | Example | Description |
|---|---|---|---|
| Reboot | `R` | `;CR\r\n` → `;AR\r\n` | Reboots the device after a 250 ms grace period to flush the response. |
| Erase All Measurements | `E` | `;CE\r\n` → `;AE\r\n` | Erases all stored measurement records. **BLE drops:** the ack is sent, BLE disconnects ~250 ms later, the flash erase runs, then BLE comes back up. |
| Set time | `T` | `;CT 1643057462\r\n` → `;AT\r\n` | Sets the internal seconds counter. Use a Unix epoch, or `0` for "seconds since last sync". |
| Bench keep-awake | `K` | `;CK 1\r\n` → `;AK\r\n` | When set to `1`, suppresses the smart-idle sleep transitions so the device stays awake on the bench. `;CK 0\r\n` re-enables normal sleep behaviour. State is reset on reboot. |
| Session active | `X` | `;CX 1\r\n` → `;AX\r\n` | Marks an explicit measurement session as active (`1`) or inactive (`0`). While active, the device treats activity as ongoing regardless of FSR / cap deltas, preventing entry into smart-idle sleep. State is reset on reboot. |
| Mark a record as synced | `S` | `;CS 97\r\n` → `;AS\r\n` | Marks the given record as synced. **BLE drops:** ack, 250 ms wait, BLE disconnect, flash write, BLE resumes. |
| Mark multiple records as synced | `M` | `;CM 97,102\r\n` → `;AM\r\n` | Same as `CS` over a range. Maximum 101 records per request. **BLE drops** as for `CS`. |

## Streaming Protocols

When the firmware is built with `SEND_EVERY_MEAS_OVER_BLE` enabled, the device
pushes every fresh measurement to the controller without being queried. Two
wire formats are supported, selected at compile time. The `QI` response
advertises which one is in use.

A build with `SEND_EVERY_MEAS_OVER_BLE` disabled (`nostream`) only emits
packets in response to controller queries; everything else in this document
still applies.

> **Note on `vdd_mv` in stream mode.** Neither stream format carries the
> `vdd_mv` field that appears in the `RL` / `RF` blobs. The streamed row is
> sensor data only. If a streaming client needs battery voltage, it must poll
> `;QB\r\n` separately — that response carries averaged `mV`, charge `%`,
> state, and a fresh raw ADC reading. (The `RL` / `RF` query responses are
> the only on-the-wire path that includes `vdd_mv` per measurement.)

### `ASCII_V1_TS` — timestamped ASCII rows

One row per measurement, terminated by `\r\n`, comma-separated. The order is:

```text
time_ms,
fsr1*4, fsr2*4, … fsr19*4,
temp1, temp2, temp3, temp4, temp5,
cap1s*3, cap1n*3, cap2s*3, cap2n*3,
cap3s*3, cap3n*3, cap4s*3, cap4n*3,
cap5s*3, cap5n*3, cap6s*3, cap6n*3
\r\n
```

Total: 1 timestamp + 19 FSR + 5 temp + 12 cap = **37 fields**.

Notes:

- `time_ms` is the device's monotonic millisecond clock at the time of the
  measurement (the same value as `reid_ble_packet_t.time_ms`).
- FSR values are reported pre-scaled by `×4`, capacitive values pre-scaled by
  `×3`. Temperature values are signed and unscaled. The scaling is applied by
  the firmware to recover dynamic range lost in the on-device divisions; the
  controller should consume the scaled values directly.
- Streamed `ASCII_V1_TS` rows do not have the `;` start byte — they are CSV
  lines and are distinguishable from query/response packets by that fact.

### `BINARY_V2` — framed binary (compile-tested, app integration pending)

`BINARY_V2` packs up to `STREAM_BINARY_V2_MAX_ROWS` (currently 3) measurements
into a single BLE notification. A frame is `<header><row[0]>…<row[N-1]>`.

```c
#pragma pack(push,1)
typedef struct reid_ble_stream_frame_v2_header_t {
    uint16_t magic;          // REID_STREAM_BINARY_V2_MAGIC = 0x5353 ('S','S')
    uint8_t  version;        // REID_STREAM_BINARY_V2_VERSION = 2
    uint8_t  frame_type;     // REID_STREAM_BINARY_V2_FRAME_SENSOR_ROWS = 1
    uint8_t  flags;          // bitmask, see below
    uint8_t  row_count;      // number of rows packed in this frame
    uint16_t sequence;       // monotonically increasing per frame, wraps at 2^16
    uint64_t base_time_ms;   // device time_ms of the first row
} reid_ble_stream_frame_v2_header_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct reid_ble_stream_row_v2_t {
    uint16_t delta_time_ms;  // ms since base_time_ms
    uint16_t fsr[19];
    int16_t  temp[5];
    uint16_t cap[12];        // per pad: south (s) then north (n) — cap1s, cap1n, … cap6s, cap6n
} reid_ble_stream_row_v2_t;
#pragma pack(pop)
```

Header `flags` bitmask:

| Bit | Macro | Meaning |
|---|---|---|
| `0x01` | `REID_STREAM_BINARY_V2_FLAG_FSR_X4` | FSR values in this frame are pre-scaled by `×4`. |
| `0x02` | `REID_STREAM_BINARY_V2_FLAG_CAP_X3` | Cap values in this frame are pre-scaled by `×3`. |

Behaviour:

- Frames are emitted as soon as either the row buffer fills, or
  `STREAM_BINARY_V2_MAX_LATENCY_MS` (currently `2 × MAIN_LOOP_TIME_MS`)
  elapses since the first buffered row.
- The frame size at runtime is bounded by the negotiated ATT MTU. Rows that do
  not fit in the current MTU are deferred to the next frame.
- If the BLE link drops, any buffered rows are discarded.
- `magic`, `version`, `frame_type`, and `flags` allow the controller to
  validate the stream and detect a future protocol bump.
- The frame cadence tracks `MAIN_LOOP_TIME_MS` (currently 8 Hz); planned
  sample-rate steps to 10 Hz and 12–15 Hz are tracked in
  `artefacts/firmware_build_matrix.md`. The wire format does not change with
  sample rate — only the rate at which frames are emitted.

## Sleep & BLE Lifeline

Smart-idle sleep is enabled by default (`ENABLE_SLEEP_SMART_IDLE`). The device
enters sleep when:

- the FSRs and caps stay below their delta thresholds for `SLEEP_NUM_MEAS`
  consecutive measurements, **and**
- no BLE connection is active, **and**
- neither bench keep-awake (`CK 1`) nor session active (`CX 1`) has been set.

While sleeping:

- The main measurement loop pauses; the device wakes every
  `SLEEP_CHECK_EVERY_MS` (5 s) to take a sensor measurement and decide whether
  to wake fully.
- BLE continues advertising at the slow "lifeline" interval
  (`APP_ADV_INTERVAL_SLOW = 8000` units = 5 s between adverts), so the
  controller can connect at any time without first waking the device with
  sensor activity.
- A connection event immediately wakes the device.
- Battery protection: if `vbat` falls below `LOW_BATTERY_SLEEP_MIN_MV` the
  device latches into a recovery sleep until `vbat` rises above
  `LOW_BATTERY_WAKE_MIN_MV`.

A watchdog timer is armed during sleep states; if the firmware fails to
service it the device resets and resumes from a known-good state.

See `SLEEP_LOGIC_MATRIX.md` in the firmware repo for the full sleep state
machine (entry/exit conditions, charging interactions, recovery paths).

## Standard data download procedure

| Controller | Peripheral | Description |
|---|---|---|
| `;QT\r\n` | | Confirm device time. Wildly wrong values point to a battery brownout or a recent reset. |
| | `;RT 1648097508\r\n` | |
| `;QP\r\n` | | Get the pending data index range. |
| | `;RP 97,102\r\n` | |
| `;QM 97,102\r\n` | | Download all records in the range. |
| | `;RM <blob>\r\n` × N | One `;RM` notification per record. Re-download to confirm if any blob looks corrupted. |
| `;CM 97,102\r\n` | | Mark the range as synced once cloud upload has completed. |
| | `;AM\r\n` | BLE will disconnect briefly while the flash write runs. |
| `;QP\r\n` | | Verify there are no pending records left. |
| | `;RP 4294967295,0\r\n` | The all-`F` low index with `0` high index indicates an empty pending set. |

## Compatibility notes for v1 controllers

A controller written against v1 will continue to function for the unchanged
queries (`QT`, `QA`, `QP`, `QR`, `QM`, `QL`, `QF`) and unchanged commands
(`CR`, `CE`, `CT`, `CS`, `CM`), provided it accepts the new larger
`reid_ble_packet_t` and `reid_ble_summary_packet_t` payloads.

Breaking points for v1 controllers:

- `QI` response now includes a `STREAM=` field. Parsers that hard-tokenised
  on exactly `UID=`,`VER=` will need to handle the additional comma-separated
  `STREAM=` token.
- `QB` response is now `pct mv state RAW=raw` instead of just `mV`. Parsers
  that did `sscanf("%u")` on the response will read the percentage and miss
  the rest.
- `reid_ble_packet_t` and `reid_ble_summary_packet_t` are larger. Any client
  that hard-coded the v1 struct sizes will mis-decode v2 blobs. Use the
  struct definitions above as the source of truth.
- New commands (`CK`, `CX`) are additive and do not affect existing v1 flows.
- A v2 device that is built with `SEND_EVERY_MEAS_OVER_BLE` will start
  pushing stream rows on the TX characteristic as soon as notifications are
  enabled. v1 controllers that only expect query-response traffic on TX must
  either tolerate the unsolicited frames or talk to a `nostream` build of the
  firmware.
