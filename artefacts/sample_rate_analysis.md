# Sample Rate Analysis — Reid Orthotic v2 R7 Firmware

## Summary

Today the device samples at **8Hz** (one full reading every 125ms). This rate
was set when the data pipeline used a verbose ASCII text protocol that
saturated the Bluetooth link well before the sensor electronics ran out of
headroom. With the binary streaming protocol now scaffolded in firmware
(`STREAM_PROTOCOL_BINARY_V2`), the bandwidth ceiling is no longer the
bottleneck.

Reading the firmware end-to-end, the realistic ceilings are:

| Build variant | Temperature | Practical sample rate | Notes |
|---|---|---|---|
| **High-rate, no temperature** | Disabled | **120–150Hz** | Cleanest cycle; every sample is the same length |
| **High-rate, occasional temperature** | Sampled every few minutes | **100–120Hz** with brief periodic gaps | Easiest path; minimal code change |
| **High-rate, async temperature** | Sampled regularly, no gaps | **120–150Hz** | Requires reworking how the temperature sensor is read |

The current 8Hz target on the binary line is conservative — there is
roughly **15× headroom** between what is shipping today and what the
hardware can sustain.

> **Confidence note.** The Hz figures above are **envelope estimates**
> derived from reading the code, not from measuring a running device. The
> qualitative story is solid (binary V2 removes the bandwidth ceiling;
> measurement loop is the new floor; temperature is the awkward part).
> The specific numbers carry meaningful uncertainty — the true floor
> could plausibly land anywhere in the **100–200Hz** range. See "Confidence
> breakdown" in the technical section for what's confirmed, what's
> estimated, and what's guessed. Treat any specific figure as planning
> guidance, not a contract.

### Why temperature is the awkward part

The temperature sensors (LMT01) work by emitting a stream of pulses that the
firmware has to count over a 60–90ms window. The chip cannot do anything else
during that window in the current implementation. At 8Hz this is invisible.
At 100Hz it becomes a noticeable gap in the data stream every time a
temperature is taken.

There are three honest ways to handle this:

1. **Drop temperature** if the application doesn't need it. The simplest path.
2. **Sample temperature rarely** — body temperature changes slowly, so reading
   it every couple of minutes is enough for most uses, and the brief gaps
   become so infrequent they're unimportant.
3. **Restructure the temperature read** so the firmware can keep sampling
   force and capacitance while the temperature counter runs in the background.
   This is real engineering work but it removes the trade-off entirely.

### Recommendation

Stage the work:

- **Step 1 — validate binary V2 at 10–15Hz** as already planned in the build
  matrix. This is the existing roadmap and proves out the binary decode on
  the app side.
- **Step 2 — push to 25Hz** with temperature on a long interval (Variant B).
  This matches the sample rate used by mainstream clinical wearables such as
  activPAL, and lands the firmware in line with the established evidence
  base for gait and activity monitoring. Well within budget and minimal
  risk.
- **Step 3 — push to 50Hz** for higher-fidelity clinical work. Still within
  Variant B's comfort zone; useful for finer movement analysis where 25Hz
  starts to alias.
- **Step 4 — 100Hz+ ceiling for sporting / athletic use cases**, where
  rapid foot loading and impact dynamics require finer temporal resolution.
  At this rate, decide between dropping temperature (Variant A) or doing the
  async refactor (Variant B+) to avoid periodic gaps in the stream.

---

## Technical Details

### Where the 8Hz number came from

`MAIN_LOOP_TIME_MS = 125` in `configure_firmware.h:35`. The historical limit
was ASCII protocol overhead — each row was hundreds of bytes of CSV text
serialised over BLE notifications, with `WAIT_FOR_TX_OF_EVERY_PACKET` blocking
loop progress until the radio acknowledged each packet.

### The measurement-cycle floor

Computed directly from `measure_sensors()` in `measure.c` and the SAADC
configuration in `adc.c`:

| Element | Source | Time |
|---|---|---|
| Settling delay (`MEAS_SETTLING = 10000` cycles) | `measure.c:28`, `system.c:22` | ~156µs at 64MHz |
| FSR ADC acquisition | `adc.c:38` (`NRF_SAADC_ACQTIME_10US`) | 12µs per sample (10µs acq + 2µs conv) |
| CAP ADC acquisition | `adc.c:80` (`NRF_SAADC_ACQTIME_3US`) | 5µs per sample |
| Samples averaged per FSR read | `adc.h:24` (`ADC_AVG_SAMPLES = 2`) | 3 conversions per call |
| Conversions per CAP read | `adc.c:452–503` | 12 per call (high/low pullup pattern) |

**Per-cycle budget when temperature is not read:**

| Stage | Operations | Estimated time |
|---|---|---|
| Setup + VDD + adc_init | one-shot | ~0.5–1ms |
| FSR sweep | 7 MUX groups × (1 settle + 6 bank reads) | ~3.2ms |
| CAP sweep | 7 MUX groups × (1 settle + ~2 cap reads) | ~2.1ms |
| Cleanup | adc_deinit | ~0.1ms |
| **Total** | | **~6ms** |

This places the measurement-side ceiling near **150Hz**, with no other
software changes.

### The temperature cost

`lmt01_get_temp()` in `lmt01.c:130–157` is a synchronous, blocking sequence:

```c
system_wait_for_ms(10);  // power-off settle
system_wait_for_ms(20);  // power-on stabilise
system_wait_for_ms(60);  // pulse count window
```

That is **90ms of blocking time per single temperature reading**. The function
rotates through one of five temperature sensors per call
(`measure.c:111–134`), so a full 5-sensor set requires five separate 90ms
blocking calls. The hardware counter (LPCOMP + TIMER1 + PPI in `lmt01_init`)
already runs the pulse-counting in the background — the loop is blocking only
because of the `system_wait_for_ms(60)` call waiting for the count window to
close.

At the current `MEASURE_TEMP_EVERY_N = 80` and 8Hz main rate, this stall is
invisible because it fires every 10 seconds and the loop has plenty of slack.
At 100Hz the same setting fires every 0.8 seconds and shows up as a 90ms gap
every ~80 samples.

### BLE bandwidth at high rates

Binary V2 is configured with `STREAM_BINARY_V2_MAX_ROWS = 3`
(`configure_firmware.h:19`), packing three measurement rows per BLE
notification. At 100Hz that is 33 notifications per second, comfortably
within the SoftDevice's throughput budget at any normal connection interval.

The `WAIT_FOR_TX_OF_EVERY_PACKET` flag still applies, but with three rows per
packet the wait fires every ~30ms rather than every measurement, so it
doesn't constrain the measurement loop at the rates discussed.

The phone-side connection interval matters: iOS often negotiates 30ms by
default, Android 7.5–15ms. At 100Hz / 33 notifications-per-second, anything
≤30ms is fine; anything looser would need an explicit connection-parameter
update request from the firmware.

### Variant comparison with concrete numbers

**Variant A — No temperature**

| Stage | Time |
|---|---|
| Per-cycle measurement | ~6ms (no variation) |
| Theoretical floor | ~150Hz |
| Recommended `MAIN_LOOP_TIME_MS` | 8 (125Hz) — leaves ~25% headroom |

Optional optimisation: dropping `ADC_AVG_SAMPLES` from 2 to 1 trims FSR cost
to ~2ms, lifting the floor toward 200Hz at the cost of more raw-reading
noise.

**Variant B — Temperature retained, blocking**

| Stage | Time |
|---|---|
| Steady-state cycle (no temp) | ~6ms |
| Cycle that includes temp | ~96ms (6ms + 90ms LMT01 read) |
| Tolerable `MEASURE_TEMP_EVERY_N` at 100Hz | 3000+ (full 5-sensor set every ~2.5 min) |
| Effect on data | Periodic 90ms gap every N samples |

This is the lowest-risk change: bump `MEASURE_TEMP_EVERY_N` from 80 to a
much larger value and the temp stall becomes rare enough to ignore. The
firmware change is one constant.

**Variant B+ — Temperature async**

Refactor `lmt01_get_temp` to use an existing SoftDevice timer (the LPCOMP +
TIMER1 + PPI infrastructure is already initialised) so the 60ms count window
runs in parallel with normal sampling. The function would split into:

1. `lmt01_start_read(pin)` — power on, schedule timer, return immediately
2. timer callback — capture count, store result, mark sensor as updated

Effort estimate: small refactor of one file (`lmt01.c`) plus state-machine
plumbing in `measure.c`. No hardware change.

### Confidence breakdown

The numbers in this document are not all the same kind of thing. Some are
read directly out of the code; some are computed from documented hardware
timings; some are educated guesses about function-call overhead that I have
not measured. This section lays it out honestly so the figures can be
weighted appropriately.

**Solid — confirmed by reading the code:**

| Claim | Evidence |
|---|---|
| Current rate 8Hz | `MAIN_LOOP_TIME_MS = 125` (configure_firmware.h:35) |
| 7 FSR MUX groups, 7 CAP MUX groups | measure.c:138–386, counted directly |
| `MEAS_SETTLING = 10000` cycles | measure.c:28 |
| `ADC_AVG_SAMPLES = 2` | adc.h:24 |
| FSR ACQ = 10µs, CAP ACQ = 3µs | adc.c:38, adc.c:80 |
| LMT01 read = 10+20+60 = 90ms blocking | lmt01.c:140–148, literal `system_wait_for_ms` calls |
| `STREAM_BINARY_V2_MAX_ROWS = 3` | configure_firmware.h:19 |
| `cap_delta` already computed internally | measure.c:455 |
| Binary protocol scaffolded but not active | `STREAM_PROTOCOL_ASCII_V1` defined; `BINARY_V2` commented |

**Reasonable but assumed:**

| Claim | Assumption | Risk if wrong |
|---|---|---|
| 64MHz CPU clock | nRF52832 default after HFCLK init | Cycle-to-time math scales linearly; floor would shift proportionally |
| `system_delay_cycles(10000)` = ~156µs | Busy loop completes one iteration per ~7 CPU cycles as the constant suggests | Could be 2–3× off if compiler optimisation alters the loop body |
| SAADC conversion = ACQ + 2µs | nRF52 datasheet 12-bit conversion figure | Low — datasheet-grounded |

**Estimates that are essentially guesses:**

| Claim | What it really is |
|---|---|
| "Channel init/uninit ≈ 5–10µs" | Guess. Could be 2µs, could be 30µs. Not profiled. |
| "Each `adc_read_bank` ≈ 50µs" | 36µs of conversions is solid (3 × 12µs); the rest is filler. |
| "Each `adc_read_cap` ≈ 85µs" | 60µs of conversions is solid (12 × 5µs); the +25µs init is guessed. |
| "Per FSR group ≈ 460µs", "Per CAP group ≈ 326µs" | Sum of one solid component plus several guessed ones. |
| **"Per-cycle total ≈ 6ms"** | **Composed of confirmed delays plus estimated function overhead. Could realistically be 4ms or 10ms.** |
| **"Floor ≈ 150Hz"** | **Direct consequence of the 6ms estimate. Could be ~100Hz or ~200Hz in reality.** |

### Honest summary of what's defensible

What can be said with confidence:

- The current 8Hz is bandwidth-limited by ASCII, not measurement-limited.
- Binary V2 removes that bandwidth limit.
- The measurement-loop floor lies somewhere in the **100–200Hz range**
  based on the code structure, with the central estimate around 150Hz.
- LMT01 at 90ms blocking is a real and important constraint that scales
  poorly with sample rate.
- Reducing FSR count or moving to CAP-delta encoding will help, in
  approximately the directions described.

What cannot be said without measurement:

- The exact 150Hz figure, or the exact rates for the variants.
- Whether 100Hz has "50% headroom" — could be 20%, could be 100%.
- Any specific per-function-call timing.

### Validation needed before committing to a target rate

To convert any of this from theoretical to actual takes one focused
experiment. Approximately half a day of work:

1. Add `system_time_ms()` calls at entry and exit of `measure_sensors()`
   and log the delta over ~1000 cycles in a debug build.
2. Verify the BLE connection interval the phone actually negotiates (the
   SoftDevice exposes this; alternatively read it from nRF Connect during
   a live session).
3. Build at the target rate and run a 10-minute soak test to look for
   loop overruns, dropped notifications, or unexpected sleep transitions.

Until those measurements are in hand, every Hz figure in this document
should be read as **planning guidance with a wide error bar**, not a
guarantee.
