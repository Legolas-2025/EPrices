# EPrices – Version History

## v1.4.1 — 2026-10-11

Build-optimization release. Two ESP-IDF advanced framework options were enabled
and the 16 pre-existing compiler warnings coming from the price lambdas were
eliminated in the source — verified warning-free in the final v1.4.1 build log.
No new sensors, no entity ID changes, no secrets changes, no logic changes and no
change to any published value. Drop-in replacement for v1.4.0.

**`eprices_nvs.h` requires no update when migrating from v1.4.0 to v1.4.1** —
the helper file is unchanged, the NVS namespace, keys and array layout are
untouched, and all stored price data carries over through the OTA.

### Binary-size reduction: `minimum_chip_revision: "3.1"`

The device reports `ESP32 Chip: ESP32 rev3.1`, so the build now declares a
minimum chip revision of v3.1. ESP-IDF then omits the errata-workaround code
paths needed only for revisions below v3.0, which shrinks the firmware image.

### IRAM reclaim: `sram1_as_iram: true`

The 40 KB SRAM1 region is mapped as instruction RAM instead of data RAM, giving
40 KB more IRAM for hot code. ESPHome confirmed the board's bootloader supports
this (`Bootloader supports SRAM1 as IRAM (+40KB)`), which also proves the
bootloader is ESP-IDF v5.1 or newer.

### Measured build results (v1.4.0 → v1.4.1)

| Region | v1.4.0 | v1.4.1 | Change |
|---|---|---|---|
| Flash (total image) | 1,028,291 B (56.0 %) | 1,026,067 B (55.9 %) | −2,224 B |
| IRAM region | 131,072 B | 172,032 B | **+40,960 B (+40 KiB)** |
| IRAM used / free | 77,247 / 53,825 B (58.93 %) | 76,771 / 95,261 B (44.63 %) | −476 / +41,436 B |
| DRAM region | 180,736 B | 180,736 B | unchanged |
| DRAM used | 55,000 B (30.43 %) | 54,984 B (30.42 %) | −16 B |

Confirmed in the generated `sdkconfig` (`CONFIG_ESP32_REV_MIN_3_1=y`,
`CONFIG_ESP_REV_MIN_FULL=301`, `CONFIG_ESP_SYSTEM_ESP32_SRAM1_REGION_AS_IRAM=y`)
and in the linker map (`iram0_0_seg len = 0x20000 + 0xA000`).

No heap was given up for it. `_sram1_iram_len` resolved to `0` (IRAM code ends at
`0x40092BE4`, below the `0x400A0000` SRAM1 boundary), so `_heap_end` stayed at
`0x40000000` and ESP-IDF reserved nothing from the heap — SRAM1 had previously
been reserved for the **bootloader**, not for the app heap. Runtime free heap
(`MALLOC_CAP_8BIT`) confirms it: **180,476 → 180,492 B** (pre-cleanup build) and
**180,504 B** (final build) at the start of `recompute_today`. OTA payload
1,026,067 B in a 1,792 KiB app partition (44 % free).

### Compiler-warning cleanup (`snprintf` + buffer sizing)

The v1.4.0 build produced 16 `-Wformat-truncation` / `-Wformat-overflow` warnings
(10 truncation, 6 overflow) from the `snprintf`/`sprintf` calls in the price
lambdas. All 12 `sprintf` calls were converted to bounded `snprintf`, and 22
`char[]` buffers were widened to GCC's worst-case output width: counter buffers
`8 → 12`, `YYYYMMDD` date buffers `9 → 34`, timestamp buffers `20 → 32`,
`strftime` buffers `20 → 24`, `HH:MM` buffers `tb 6 → 24` and `hb 6 → 16`.
`char m[40]` / `char m[50]` were already large enough and only had their call
converted. The sizes are worst-case sizes, not typical ones — GCC assumes an
unbounded `int` can print 11 characters — and must not be shrunk back. Compile-
time only: no behaviour, entity or NVS changes.

**Verified:** the final v1.4.1 build compiled with **zero compiler warnings**.

### New hardware constraints

- The image will not boot on — and cannot be OTA'd to — an ESP32 module older
  than revision v3.1: both the ESP-IDF bootloader and the OTA updater reject a
  chip below the image's declared minimum revision.
- `sram1_as_iram` needs an ESP-IDF v5.1+ bootloader, and OTA does not update the
  bootloader, so an older board would need a USB reflash.
- `minimum_chip_revision` is valid only for the original ESP32. Switching
  `board:` to ESP32-S3/C3/S2/C6 requires removing the whole `advanced:` block.

`compiler_optimization` was already `SIZE` (ESPHome default) and all existing
`sdkconfig_options` are unchanged.

### Changed location in `eprices.yaml`

- `esp32:` → `framework:` — new `advanced:` block with `minimum_chip_revision:
  "3.1"` and `sram1_as_iram: true`, plus inline safety notes, added above the
  existing `sdkconfig_options:`
- File header — new `v1.4.1 compiler-warning cleanup` note explaining the
  buffer-sizing rule
- 26 lambda statements — 22 `char[]` declarations widened, 12 `sprintf` calls
  converted to `snprintf`; no lambda logic changed

### Migration checklist (v1.4.0 → v1.4.1)

| Item | Action required |
|---|---|
| `eprices_nvs.h` | **None** — unchanged, no update needed |
| `secrets.yaml` | None |
| HA entities / automations | None |
| NVS-stored prices | Preserved |
| Flash method | Normal ESPHome OTA |
| Hardware | ESP32 rev ≥ v3.1 and ESP-IDF v5.1+ bootloader required |

## v1.4.0 — 2026-10-06

Reliability release expanding the API retry coverage window for both today
and tomorrow. No new sensors, no secrets changes, no entity ID changes.
Drop-in replacement for v1.3.1. No `eprices_nvs.h` changes required.

### Today auto-retry now covers the full 24-hour day

Previously, the today auto-retry burst (00:05, 00:15, 00:30 + hourly :30)
capped at 8 attempts, exhausting by ~05:30. After that,
`auto_today_retry_active` was set to `false` and every remaining hourly
trigger returned immediately — leaving 18 hours with zero retry coverage.
If the API was down overnight, no automatic recovery was possible until the
next midnight bridge or a manual button press.

**Fix:** when the 8-attempt burst is exhausted, the counter resets to 0 and
`auto_today_retry_active` stays `true`. The existing hourly `:30` trigger
continues firing every hour for the rest of the day (up to 26 total attempts
per day). On any success, `auto_today_retry_active` is set to `false` as
before, stopping all further retries.

### Tomorrow retry window extended to 23:15

The hourly `:55` retry trigger was gated to `hour ≤ 19`, cutting off at
19:55 despite the fetch window extending to 23:50. Four hours of the window
had no scheduled coverage.

**Fix:** the gate is widened to `hour ≤ 22` (14:55 through 22:55, nine
scheduled attempts) and the attempt cap in the 13:55 and hourly `:55` triggers
is raised from 8 to 12 — without the cap change the chain still stopped at
16:55. A new last-chance trigger at 23:15 provides a final attempt inside the
window.

### Tomorrow attempt counter split from fetch counter

`tomorrow_retry_count` was being incremented by both the `on_time` triggers and
`smart_tomorrow_price_update`, so every scheduled attempt counted twice and the
`>= 8` cap blocked the retry chain from 17:55 onward — the widened window could
never be reached. The double-increment was introduced in v1.2.0.

The counter now has one owner each for its two jobs:

- `tomorrow_retry_count` — incremented by the `on_time` triggers only; the
  budget for the 12 scheduled attempts
- `tomorrow_fetch_count` (new global) — incremented by
  `smart_tomorrow_price_update`; drives the `Tomorrow API Fetch Attempts`
  sensor

Worker self-heal and manual button presses therefore no longer consume the
scheduled attempt budget.

### Manual clear is no longer undone by the self-heal

`clear_tomorrow_prices` now also zeroes `tomorrow_last_update_attempt`, the
self-heal throttle reference. Previously, clearing tomorrow's data could be
silently undone within 30 minutes if the last attempt was older than that.

### Tomorrow worker self-heal re-arm

A new 30-minute self-heal check was added to the existing 10-second worker
loop. If no tomorrow data exists, the last attempt was more than 30 minutes
ago, and the fetch window is still open, the worker automatically re-arms
`need_tomorrow_update`. This catches API recovery at any time during the
window without relying on a scheduled trigger. The 30-minute gap is the
natural throttle — no hard attempt cap needed for this phase.

### Retry coverage comparison

| Fetch | v1.3.1 coverage | v1.4.0 coverage |
|---|---|---|
| Today | 00:05 → 05:30 (8 attempts) | 00:05 → 23:30 (up to 26 attempts) |
| Tomorrow | 13:25 → 16:55 (5 attempts) | 13:25 → 23:15 (12 scheduled) + 30-min self-heal until 23:50 |

The v1.3.1 tomorrow figure is corrected here: the gate ran to 19:55, but the
counter cap stopped real fetches at 16:55, so only five attempts occurred.

### Changed locations in `eprices.yaml`

- `after_today_fetch` script — failure branch: counter reset instead of
  deactivation
- Tomorrow hourly `:55` `on_time` trigger — gate widened from `hour ≤ 19`
  to `hour ≤ 22`
- 13:55 and hourly `:55` triggers — attempt cap raised from 8 to 12
- New `on_time` trigger at 23:15 for last-chance tomorrow attempt
- Tomorrow `seconds: /10` worker lambda — 30-min self-heal re-arm block
  added before `need_tomorrow_update` processing
- `globals:` — added `tomorrow_fetch_count`
- 13:25 trigger, `smart_tomorrow_price_update`, `Tomorrow API Fetch Attempts`
  sensor, `midnight_bridge_promotion` — split scheduled-attempt counting from
  total-fetch counting
- `boot_recovery_tomorrow_script` — publishes the counter's current value rather
  than forcing it to 0, so a fetch completed by a trigger during the first 50 s
  after a reboot is no longer erased from the sensor
- `clear_tomorrow_prices` script — also zeroes `tomorrow_last_update_attempt`
- `recompute_tomorrow` script — status anchor uses calendar-day increment
  instead of `+86400`

## v1.3.1 — 2026-10-04

Bug-fix release for hourly JSON output length. No new sensors, no secrets
changes, no entity ID changes. Drop-in replacement for v1.3.0 (no
`eprices_nvs.h` changes required).

### Hourly JSON now emits populated block count (23/24/25), no trailing null slot

v1.3.0 expanded hourly vectors to 25 slots to preserve the repeated hour on DST
fall-back day. The hourly JSON builders in `recompute_today` and
`recompute_tomorrow` still iterated all 25 allocated slots, which could emit a
trailing `null` on normal 24-hour days and break downstream HA template sensor
arithmetic.

Fixed by tracking the actual sequential hour-block count and publishing only
those populated blocks:
- normal day: 24 values
- DST spring-forward day: 23 values
- DST fall-back day: 25 values

## v1.3.0 — 2026-10-03

Complete DST hardening. No new sensors, no secrets changes, no entity ID
changes. Drop-in replacement for v1.2.4 (migrating to v1.3.0 requires no changes to eprices_nvs.h file).

### All three DST edge cases fixed

#### Bug 1 — `+86400` UTC seconds for tomorrow's date (moderate)

On spring-forward Saturday evening after ~23:00 CET, adding exactly 86 400
UTC seconds to compute "tomorrow's date" landed on Monday instead of Sunday
(25 local hours span the DST boundary). This caused the tomorrow fetch URL,
NVS expected-date check, and `tomorrow_date_str` to all contain the wrong
date, making the fetch return no data or the wrong day's data.

Fixed in all three locations (`nvs_load_tomorrow_script`,
`parse_energy_charts_tomorrow_script`, `smart_tomorrow_price_update`) by using
calendar-day increment (`tm_mday += 1` + `mktime()`) with a midday anchor
instead of UTC-second arithmetic.

#### Bug 2 — `+86400` in tomorrow sensor lookup anchor (minor)

The four "Tomorrow" live sensors used `now + 86400` as the binary-search anchor
into `price_timestamps_tomorrow`. On DST transition nights this was ~1 hour off
from "same local time tomorrow", selecting the wrong 15-minute price slot.

Fixed in all four sensor lambdas by computing the anchor via calendar-day
increment.

#### Bug 3 — Fall-back day 25-hour hourly average (minor)

On the DST fall-back day (25 local hours), the `hourly_avg_prices_kwh` vector
(24 slots, indexed by `tm_hour`) received two writes to slot `[2]` — the second
02:xx block overwrote the first. Hourly average, min/max, and JSON data for the
02:xx hour were wrong.

Fixed by expanding the hourly vectors to 25 slots and populating them by
sequential hour-block index rather than raw `tm_hour`. The block counter also
advances when `tm_isdst` changes — that is what keeps the two `02:xx` blocks on
a fall-back day separate, since a `tm_hour`-only comparison merges them and
yields 24 blocks instead of 25. The four hourly sensor lambdas use the same
`tm_isdst`-aware scan. Normal 24-hour days are completely unaffected: 23 blocks
on spring-forward days, 25 on fall-back days.

See `CHANGELOG.md` for full implementation details.

---

## v1.2.4 — 2026-09-22

Bug-fix release. No new sensors, no secrets changes, no entity ID changes.
Drop-in replacement for v1.2.3.

### Force buttons now always perform a real HTTP fetch

Fixed a long-standing behavioural bug where the "Force Today's Update" and
"Force Tomorrow's Update" buttons silently served NVS-cached prices instead
of fetching from the API. If valid prices for today's (or tomorrow's) date
were already in NVS, the underlying `smart_price_update` script would load
from cache and skip the HTTP call entirely — defeating the purpose of a force
button. The status message could even say "Updating..." while in fact no API
call was made.

Both button `on_press` handlers now clear the relevant NVS slot and zero the
in-memory entry count before running the update script, guaranteeing an HTTP
fetch on every manual press. A new `clear_today_slot()` wrapper was added to
`eprices_nvs.h` to mirror the existing `clear_tomorrow_slot()`. Boot recovery,
auto-retry, and the midnight bridge are completely unaffected.

### Today fetch URL broken by Energy-Charts API default behaviour change

Fixed a critical bug where the today HTTP fetch returned a stale cached dataset
approximately two months old instead of the current day's prices. The
Energy-Charts `/price` endpoint, when called without explicit date parameters,
no longer returns the current rolling day and instead returns a stale window.
The today URL now includes explicit `&start=YYYY-MM-DD&end=YYYY-MM-DD`
parameters. The tomorrow URL already had a `&start=` parameter and has been
updated to also include `&end=` for symmetry and robustness.

See `CHANGELOG.md` for full implementation details.

---

## v1.2.3 — 2026-08-04

Quietness patch for the Home Assistant activity log. No new sensors, no
secrets changes, no entity ID changes. Drop-in replacement for v1.2.2.

### Uptime display bucketed and change-detected

The `Uptime` text sensor previously published a new value every minute
(e.g. `3 h 22 min`, `12 d 4 h 17 min`) because the underlying internal
`uptime` sensor updates every 60 seconds and the lambda always called
`publish_state`. In the HA activity log this produced a noisy stream of
state changes redundant with the existing `Last Reboot` text sensor.

The lambda now formats uptime into coarse buckets and only publishes when
the bucket string changes — **one logbook entry per hour** for the first
day, then **one per day**, then **one per month**.

**Buckets:**

| Age | Display |
|---|---|
| 0 – 59 min | `< 1 hour` |
| 1 – 23 h | `> 1 hour` ... `> 23 hours` |
| 1 – 30 d | `> 1 day` ... `> 30 days` |
| 1 – 11 mo | `> 1 month` ... `> 11 months` |
| 12+ mo | `> 1 year` ... `> N year(s) N month(s)` |

**New global:** `last_published_uptime` (`std::string`) — tracks the last
published bucket so the lambda can skip publishing when the value hasn't
crossed a boundary.

See `CHANGELOG.md` for full implementation details.

---

## v1.2.2 — 2026-04-28

Stability patch addressing spontaneous reboots during tomorrow price fetch at ~13:55.
No new sensors, no secrets changes, no entity ID changes. Drop-in replacement for v1.2.1.

### ESP32 task watchdog timeout increase

Increased the ESP-IDF task watchdog timeout from the default (~15 seconds) to
40 seconds and disabled idle task watchdog checking on both CPU cores. This
prevents false-positive watchdog resets during heavy JSON parsing when full price
data arrives (~13:55 and subsequent retry attempts).

**Root cause:** The first API call at 13:25 typically returns no data
(Energy-Charts usually hasn't published tomorrow's prices yet), so no heavy
parsing occurs. By 13:55, complete data is available, triggering the full
parsing chain that exceeded the default watchdog timeout.

### JSON string building optimised

Replaced O(n²) `std::string +=` concatenation in `recompute_today` and
`recompute_tomorrow` with pre-allocated fixed-size `char` buffers via
`snprintf()`. Eliminates heap fragmentation and peak memory spikes during JSON
building for the 8 JSON text sensors.

### Periodic yield() calls

Added explicit `yield()` calls at strategic points during parsing and recompute
operations to prevent the task watchdog from triggering during CPU-intensive
operations.

### Heap monitoring logs

Added `ESP_LOGI` calls to log free heap at key points during parsing and
recompute operations using `heap_caps_get_free_size(MALLOC_CAP_8BIT)`.

See `CHANGELOG.md` for full implementation details.

---

## v1.2.1 — 2026-04-07

Stability and housekeeping patch. No new sensors, no secrets changes,
no entity ID changes. Drop-in replacement for v1.2.

### ESP32 main task stack size increase

Doubled the FreeRTOS main task stack from 8192 to 16384 bytes via
`CONFIG_ESP_MAIN_TASK_STACK_SIZE`. Eliminates the stack overflow scenario
most likely responsible for the spontaneous reboot observed in production
on 2026-04-06 during a simultaneous NVS load + HTTP fetch/parse cycle.

### Price vector heap pre-allocation at boot

Added `.reserve(96)` on all four price vectors (`price_timestamps_today`,
`price_values_today`, `price_timestamps_tomorrow`, `price_values_tomorrow`)
in the `on_boot` lambda. Prevents repeated heap reallocation during NVS load
and HTTP parse operations, reducing heap fragmentation and peak allocation
pressure during the boot sequence.

### Hourly JSON sensor cleared state unified

`Today JSON Hourly Prices EUR⁄kWh` and `Tomorrow JSON Hourly Prices EUR⁄kWh`
now publish `""` when cleared, matching the existing behaviour of all six
15-minute JSON sensors. Previously they published `"[]"`.

See `CHANGELOG.md` for full implementation details.

---

## v1.2 — 2026-04-06

### HTTP fetch stuck-flag watchdog

Fixed a production-observed reliability issue where a TCP-level stall during
an HTTP fetch could lock `is_updating_today` or `is_updating_tomorrow` at
`true` for several minutes, silently blocking all auto-retry triggers and
manual button presses for the duration.

A 120-second watchdog was added to both worker loops. If either `is_updating_*`
flag has been held for more than 120 seconds, the worker force-clears it and
sets the status message to `"Fetch timeout – will retry"`, allowing the next
scheduled retry or manual press to proceed immediately.

New globals: `is_updating_today_since`, `is_updating_tomorrow_since`.

### Tomorrow auto-retry attempt counter fix

`tomorrow_retry_count` was not being incremented in the 13:55 and 14:55–19:55
hourly retry triggers, causing the `Tomorrow API Fetch Attempts` diagnostic
sensor to undercount after the first scheduled attempt at 13:25.

### Status message improvements

- `tomorrow_update_status_message` initial value changed from
  `"Waiting for 13:20"` to `"No data yet"`
- `clear_tomorrow_prices` end message changed from
  `"Cleared – awaiting next 13:20 window"` to
  `"Cleared – awaiting fetch window"`

See `CHANGELOG.md` for full implementation details.

---

## v1.1 — 2026-04-06

### Negative price provider fee

Added separate provider fee support for negative spot prices via a new
`eprices_neg_prov_fee` secret key. Positive and negative market prices
now use independent fee multipliers, correctly modelling contracts where
the provider's fee structure differs between the two cases.

Price calculation:
- Positive: `(raw / 1000) × (1 + prov_fee) × (1 + vat_rate)`
- Negative: `(raw / 1000) × (1 - neg_prov_fee) × (1 + vat_rate)`

VAT is applied to both, consistent with net billing where VAT is calculated
on the monthly net sum (linear equivalence applies).

See `CHANGELOG.md` for full implementation details.

---

## v1.0 — 2026-04-05

First public release.

### What EPrices is

EPrices is an ESPHome firmware for ESP32 that fetches day-ahead electricity
spot prices from the public Energy-Charts API and exposes them as Home Assistant
sensors. No API token, no cloud subscription, no external automations required
for core functionality — just an ESP32, ESPHome, and your WiFi network.

Prices are fetched for today and tomorrow in 15-minute resolution, converted
from raw €/MWh to €/kWh with your provider fee and VAT applied, and stored
persistently in ESP32 NVS flash so they survive reboots without re-fetching.

### Core features

- 15-minute and hourly resolution price arrays exposed as JSON text sensors
- Current, next, average, highest and lowest price sensors for both today and tomorrow
- Tomorrow live sensors evaluate at `now + 86400s` — showing tomorrow at the same local time
- NVS persistence — prices survive reboots without re-fetching
- Midnight bridge — tomorrow's data automatically promoted to today at 00:00
- Auto-retry logic — up to 8 HTTP fetch attempts for both today and tomorrow
- DST-safe — UNIX timestamps and binary search throughout, no hour-slot arithmetic
- Staleness detection — `Today Current Price Status` shows `Stale` if stored date mismatches today
- Full diagnostic sensor suite — NVS status, fetch attempts, API fetch times, data loaded
  times, WiFi signal, human-readable uptime
- Supports any Energy-Charts bidding zone (SI, DE-LU, AT, FR, HR, HU and more)
- Provider fee and VAT rate configurable via `secrets.yaml` — no code changes needed

### Sensor highlights

- **Today/Tomorrow JSON Hourly Prices EUR⁄kWh** — 24-value JSON arrays
- **Today/Tomorrow JSON 15-Min Prices EUR⁄kWh (P1/P2/P3)** — three 32-value JSON arrays
  covering 00:00-07:45, 08:00-15:45 and 16:00-23:45
- **Today/Tomorrow Data Loaded Time** — stamped on both NVS load and HTTP fetch
- **Today/Tomorrow Last API Fetch Time** — stamped on HTTP fetch only; `Never` if NVS only
- **Today/Tomorrow API Fetch Attempts** — HTTP-only counter, resets at midnight
- **Uptime** — human-readable: `45 s` → `5 min` → `3 h 22 min` → `12 d 4 h` → `4 months 12 d`

### Files

| File | Purpose |
|---|---|
| `eprices.yaml` | Main ESPHome configuration |
| `eprices_nvs.h` | NVS helper — save/load price arrays to ESP32 flash |
| `secrets.yaml` | Local secrets — not committed to git |
| `CHANGELOG.md` | Full sensor and entity ID reference for v1.0 |
| `README.md` | Installation guide and full sensor reference |
| `ENTSO-E-PRICES-MIGRATION.md` | Optional — migration guide from the predecessor project |
