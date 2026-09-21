# EPrices – Version History

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
