# Session Handoff - 2026-05-14

## Purpose

Hand off the IMU bring-up work so it can be picked up tomorrow:

- LSM6DSM 6-axis IMU is populated on the Reid Orthotic v2 / CAL1020 PCB but has never been used by firmware.
- Industry / customers have asked for IMU data (or an external IMU) to validate FSR/CAP step counts.
- Bring-up source changes have been made on branch `tim`. Nothing has been built, flashed, or committed yet.

## Hardware finding (no change required)

Schematic `artefacts/Reid Orthotic v2 R4.pdf`, IO sheet, part U1:

- Part: **ST LSM6DSM** — 3-axis accel + 3-axis gyro + temp.
- Bus: I2C on the existing `MCU_SDA` / `MCU_SCL` with 2.1 kΩ pull-ups.
- Address: `SA0` low + `CS` high → 7-bit I2C address **`0x6A`** (`0b1101010`).
- Interrupts: `INT1` / `INT2` routed to the nRF52832 as `6DOF_INT1` / `6DOF_INT2`. Aux SPI pins (`OCS_Aux` / `SDO_Aux`) wired but unused.
- Engineer confirms hardware is in place. Enabling the IMU is therefore a **firmware-only** change.

Driver `firmware/ble_app_firmware_v2_R7/lsm6dsm.c` / `lsm6dsm.h` has existed since 2019 (also present in R5, R6, v2.07) but `lsm6dsm_init()` / `lsm6dsm_update()` were **never called**, and `i2c_init()` itself was never invoked anywhere.

## What changed in source (uncommitted, branch `tim`)

Bumped `DEVICE_FW_VERSION` to **`0x00020025`** (`configure_firmware.h`).

### `lsm6dsm.h` / `lsm6dsm.c`

- Added `LSM6DSM_ADDRESS_WHO_AM_I = 0x0F`, `LSM6DSM_WHO_AM_I_VALUE = 0x6A`.
- Added `uint8_t lsm6dsm_whoami(void)` — reads WHO_AM_I; returns `0x6A` when the part responds, `0` on I2C failure.

### `main.c`

- `main_init()` now calls `i2c_init();` then `lsm6dsm_init();` after `lmt01_init()`.
- Under `ENABLE_DEBUG`, logs `LSM6DSM WHO_AM_I = 0xXX (expect 0x6A)` over RTT at boot.
- Per-loop debug block also calls `lsm6dsm_update()` and logs `IMU acc %d,%d,%d gyro %d,%d,%d temp %d` alongside the existing FSR / TEMP / CAP logs.

### `messaging_defines.h` / `messaging.c`

- New query character `MSG_QUERY_IMU = 'D'` ("6DOF / IMU debug").
- Added `case MSG_QUERY_IMU:` next to `QF` / `QL`: reads WHO_AM_I, does a fresh `lsm6dsm_update()`, replies in ASCII:
  ```
  ;RD WHO=0x6A AX=12 AY=-34 AZ=2050 GX=1 GY=-2 GZ=0 T=128
  ```
- RX parser line for `QD` added alongside the other query parsers.
- `#include "lsm6dsm.h"` added at the top of `messaging.c`.

### Build matrix

`artefacts/firmware_build_matrix.md` "Next planned variants" table now lists the `v00020025` IMU bring-up build and a future "binary + IMU" row.

## Verification when you build / flash tomorrow

Build the first test as **nostream** so the query path is uncluttered:

```
./tools/build_app.sh <nrf5_sdk_root> lhs Release nostream
./tools/build_app.sh <nrf5_sdk_root> rhs Release nostream
./tools/package_dfu.sh ...
```

(`nostream` is just a sed-toggle of `SEND_EVERY_MEAS_OVER_BLE` in `tools/common.sh`. Query commands and RTT logging both still work in `nostream`.)

Expected behaviour after DFU:

1. **Boot RTT line** (Tag-Connect J5 + J-Link + SEGGER RTT viewer): `LSM6DSM WHO_AM_I = 0x6A (expect 0x6A)`.
2. **Per-loop RTT line** every 125 ms: `IMU acc <ax>,<ay>,<az> gyro <gx>,<gy>,<gz> temp <T>`.
3. **BLE query** `;QD` over the NUS serial protocol: response `;RD WHO=0x6A AX=.. AY=.. AZ=.. GX=.. GY=.. GZ=.. T=..`.

Raw-count sanity checks (driver config: accel ±16 g @ 208 Hz, gyro ±2000 dps @ 208 Hz, FIFO bypass):

- Flat on the bench: one accel axis ≈ ±2048, other two ≈ 0 (≈ 2048 LSB/g).
- Stationary: all gyro axes ≈ 0 (± a small bias). 70 mdps/LSB.
- Temp: `°C ≈ 25 + T/256`.

Failure modes to watch for:

- `WHO = 0x00` → I2C transaction failed. Most likely cause is the LHS / RHS pin-set mismatch (`gpio.h` defines different `PIN_SDA` / `PIN_SCL` for `REID_LHS` vs `REID_RHS`); confirm the build side matches the device. Less likely: part not fitted on that specific PCB.
- WHO correct but accel / gyro stay at `0` → check `i2c_write` ordering inside `lsm6dsm_init()` (CTRL1_XL register sequence) is being accepted by the part.

## Binary v2 protocol sketch (do not implement yet)

When the ASCII bring-up is confirmed working, the next step is to make IMU data part of the v2 binary stream so host tooling can log it next to FSR / CAP.

Proposed change in `measure.h`:

```c
typedef struct reid_ble_stream_row_v2_t {
    uint16_t delta_time_ms;
    uint16_t fsr[19];
    int16_t  temp[5];
    uint16_t cap[12];
    int16_t  imu_acc[3];   // +6 bytes (ax, ay, az; raw LSM6DSM counts, ±16 g)
    int16_t  imu_gyro[3];  // +6 bytes (gx, gy, gz; raw counts, ±2000 dps)
} reid_ble_stream_row_v2_t;       // 74 -> 86 bytes
```

Plus a flag bit `REID_STREAM_BINARY_V2_FLAG_IMU = 0x04` set in the frame header so older decoders can stay back-compatible.

Batching impact at MTU 247 (payload 244, header 16, rows budget 228):

- today (74 B / row): 3 rows / frame (222 B).
- with IMU (86 B / row): 2 rows / frame (172 B; three rows would be 258 B, over budget).

Trade-off: full-rate IMU costs one row of batching. Acceptable for the validation phase; revisit if Phase 3 sample-rate increase ever gets tight, in which case either decimate IMU to the first row of each frame, or send a separate `frame_type` for IMU-only frames.

Wiring in firmware: extend `stream_binary_v2_append_row()` in `main.c` (~ line 260) to also `lsm6dsm_update()` and copy 6 values into the row; OR the IMU flag into the header build (~ line 326). Host decoder (`serial-monitor` and the app) reads the flag and decodes 12 extra bytes per row.

Update `artefacts/binary_stream_sample_rate_plan_2026-05-01.md` "Per-row payload" section at the same time as the firmware change.

## Open items for tomorrow

1. Commit the bring-up source changes on `tim`. Suggested message: "Add LSM6DSM IMU bring-up: i2c_init/lsm6dsm_init in main_init, WHO_AM_I check, per-loop RTT log, new `;QD` query (v00020025)".
2. Build `nostream` `lhs` and `rhs` packages, record sha256 + git commit in the active table of `firmware_build_matrix.md`.
3. Flash one device, run `;QD` from a NUS terminal, confirm `WHO=0x6A` and that accel / gyro counts move when the puck is moved.
4. Decide whether to also build the matching `stream` variant before moving on to the binary v2 IMU extension.
5. Once confirmed working, schedule the `BINARY_V2 + IMU` change against `measure.h` plus host decoder updates.

## Related files / context

- Schematic: [Reid Orthotic v2 R4.pdf](/home/salmoneller/Git/sensor-skins-firmware/artefacts/Reid Orthotic v2 R4.pdf)
- Driver: `firmware/ble_app_firmware_v2_R7/lsm6dsm.c` / `lsm6dsm.h`
- Build matrix: [firmware_build_matrix.md](/home/salmoneller/Git/sensor-skins-firmware/artefacts/firmware_build_matrix.md)
- Binary plan: [binary_stream_sample_rate_plan_2026-05-01.md](/home/salmoneller/Git/sensor-skins-firmware/artefacts/binary_stream_sample_rate_plan_2026-05-01.md)
- Serial protocol: [Reid Calceus - Serial Protocol v2.md](/home/salmoneller/Git/sensor-skins-firmware/artefacts/Reid Calceus - Serial Protocol v2.md) — `;QD` is the new query added today.
- Memory: `memory/imu_lsm6dsm.md`.
