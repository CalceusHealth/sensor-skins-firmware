# Sleep Logic Matrix

This matrix describes the current `tim` branch sleep policy implemented in:

- `firmware/ble_app_firmware_v2_R7/main.c`
- `firmware/ble_app_firmware_v2_R7/configure_firmware.h`

## Key thresholds

- Low-battery forced sleep floor: `3250 mV`
- Low-battery wake floor: `3450 mV`
- Charging recovery floor: `3450 mV`
- Charging idle sleep timeout: `15 s`
- Normal idle sleep timeout: `180 s`

## Matrix

| Battery / state | Charging confirmed | BLE active / connected | Sensor activity | Result |
|---|---:|---:|---:|---|
| `VBAT < 3250` | No | No | No | Sleep immediately |
| `VBAT < 3250` | No | Yes | Any | Sleep immediately |
| `VBAT < 3250` | Yes | No | Any | Sleep immediately |
| `VBAT < 3250` | Yes | Yes | Any | Sleep immediately |
| `3250 <= VBAT < 3450` and low-battery sleep already latched | No | No | Any | Stay asleep |
| `3250 <= VBAT < 3450` and low-battery sleep already latched | Yes | Yes | Any | Stay asleep |
| `VBAT >= 3450` and low-battery sleep was latched | No | No | No | May wake, then normal logic resumes |
| `VBAT >= 3450` and low-battery sleep was latched | Yes | No | No | May wake, then normal logic resumes |
| `VBAT >= 3450` | Yes | No | No | Charging-idle timer runs, sleep after `15 s` |
| `VBAT >= 3450` | Yes | Yes | Any | Stay awake |
| `VBAT >= 3450` | No | Yes | Any | Stay awake |
| `VBAT >= 3450` | No | No | Yes | Stay awake |
| `VBAT >= 3450` | No | No | No | Idle timer runs, sleep after `180 s` |

## Priority order

1. Low-battery protection: below `3250 mV` wins over everything.
2. If already in low-battery sleep: stay asleep until `3450 mV`.
3. Charging recovery rule: if charging is confirmed and below `3450 mV`, stay in recovery sleep.
4. BLE activity keeps the device awake.
5. Sensor activity keeps the device awake.
6. Confirmed charging with no activity: sleep after `15 s`.
7. Not charging with no activity: sleep after `180 s`.

## Plain English

- If the battery drops below `3250 mV`, the device sleeps no matter what.
- That includes active BLE connection and active streaming.
- Once it has slept for low battery, it does not wake again until battery reaches `3450 mV`.
