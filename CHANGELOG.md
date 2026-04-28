# EPrices – Changelog

## v1.2.2 — 2026-04-28

### ESP32 task watchdog timeout increase

Increased the ESP-IDF task watchdog timeout from the default (~15 seconds) to
40 seconds via `CONFIG_ESP_TASK_WDT_TIMEOUT_S`. Additionally disabled idle task
watchdog checking on both CPU cores via `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0`
and `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1`.

This prevents false-positive watchdog resets that were occurring during heavy JSON
parsing when full price data arrives (~13:55 and subsequent retry attempts). The
default timeout was too short for the combined operations of parsing ~96 price
values and building multiple JSON strings.

**Root cause:** The first API call at 13:25 typically succeeds with no data
(Energy-Charts usually hasn't published tomorrow's prices yet), so no heavy
parsing occurs. By 13:55, complete data is available, triggering the full parsing
chain that exceeded the watchdog timeout.

**Changed location in `eprices.yaml`:**
- `esp32: framework: sdkconfig_options:` — added
  - `CONFIG_ESP_TASK_WDT_TIMEOUT_S: "40"`
  - `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0: n`
  - `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1: n`

---

### JSON string building optimised — eliminated O(n²) heap fragmentation

Replaced the O(n²) string concatenation pattern in `recompute_today` and
`recompute_tomorrow` with pre-allocated fixed-size character buffers using
`snprintf()`. Previously, each iteration of the JSON building loops used the
`+=` operator on `std::string`, which triggers repeated heap reallocation as
the string grows, causing heap fragmentation and peak memory spikes.

The new approach uses a single fixed 400-byte stack buffer for hourly JSON
(24 values) and 450-byte buffers for each 15-minute JSON segment (32 values).
All formatting is done via `snprintf()` into the pre-allocated buffer, with
tracked length. This eliminates all heap allocations during JSON building.

**Before (problematic):**
```cpp
std::string json_h = "[";
for (int i = 0; i < 24; i++) {
    // ...
    json_h += pb;  // Each += may trigger reallocation!
}
```

**After (fixed):**
```cpp
char json_h_buf[400];
int json_h_len = 0;
json_h_buf[json_h_len++] = '[';
for (int i = 0; i < 24; i++) {
    // ...
    int written = snprintf(json_h_buf + json_h_len, sizeof(json_h_buf) - json_h_len, "%.4f", ha);
    json_h_len += written;
}
```

**Changed locations in `eprices.yaml`:**
- `recompute_today` script — hourly JSON and 15-min JSON building loops refactored
- `recompute_tomorrow` script — hourly JSON and 15-min JSON building loops refactored

---

### Periodic yield() calls prevent watchdog during heavy parsing

Added explicit `yield()` calls at strategic points during parsing and recompute
operations to prevent the task watchdog from triggering during CPU-intensive
operations:

- In `tokenise()` lambda: every 50 characters processed
- In parse loops: every 24 entries parsed
- Between `build32_fixed()` calls in recompute scripts

**Changed locations in `eprices.yaml`:**
- `parse_energy_charts_today_script` — yield in tokenise and parse loops
- `parse_energy_charts_tomorrow_script` — yield in tokenise and parse loops
- `recompute_today` — yield between JSON sensor publishes
- `recompute_tomorrow` — yield between JSON sensor publishes

---

### Heap monitoring logs for debugging

Added `ESP_LOGI` calls to log free heap at key points during parsing and
recompute operations:
- Heap before and after parsing (both today and tomorrow)
- Heap at start and end of recompute operations

These logs use `heap_caps_get_free_size(MALLOC_CAP_8BIT)` and appear at INFO
level, enabling future diagnosis of memory pressure issues.

**Changed locations in `eprices.yaml`:**
- `on_boot` lambda — added boot heap log
- `parse_energy_charts_today_script` — heap before/after parsing
- `parse_energy_charts_tomorrow_script` — heap before/after parsing
- `recompute_today` — heap at start/end
- `recompute_tomorrow` — heap at start/end

---

## v1.2.1 — 2026-04-07

### ESP32 main task stack size increase

Increased the FreeRTOS main application task stack from the ESP-IDF default
of 8192 bytes to 16384 bytes. This eliminates the stack overflow scenario
that most likely caused the spontaneous reboot observed in production on
2026-04-06, where the FreeRTOS idle task faulted following memory pressure
during a simultaneous NVS load and HTTP fetch/parse cycle.

The 8 KB cost is well within the ESP32's 320 KB available RAM.

**Changed location in `eprices.yaml`:**
- `esp32: framework: sdkconfig_options:` — added
  `CONFIG_ESP_MAIN_TASK_STACK_SIZE: "16384"`

---

### Price vector heap pre-allocation at boot

Pre-allocated capacity for all four price vectors at boot using `.reserve(96)`.
Previously, each vector started empty and grew incrementally on every NVS load
and HTTP fetch, triggering repeated heap reallocations and leaving the heap
fragmented before the first fetch cycle completed.

With capacity reserved upfront, no reallocation occurs during any subsequent
NVS load or HTTP parse operation. This reduces heap fragmentation and peak
allocation spikes, particularly during the boot sequence where NVS load and
the first HTTP fetch can overlap.

**Vectors pre-allocated:**
- `price_timestamps_today`
- `price_values_today`
- `price_timestamps_tomorrow`
- `price_values_tomorrow`

**Changed location in `eprices.yaml`:**
- `on_boot:` lambda — added `.reserve(96)` on all four price vectors

---

### Hourly JSON sensor cleared state unified with 15-min sensors

The two hourly JSON text sensors previously published `"[]"` when cleared
(at midnight bridge and on `clear_today_prices` / `clear_tomorrow_prices`).
The six 15-minute JSON sensors already published `""` in the same situation.
All eight JSON sensors now publish `""` when cleared, giving consistent
behaviour across the full sensor set.

**Affected sensors:**
- `Today JSON Hourly Prices EUR⁄kWh`
- `Tomorrow JSON Hourly Prices EUR⁄kWh`

**Changed locations in `eprices.yaml`:**
- `clear_today_prices` script — `"[]"` → `""`
- `clear_tomorrow_prices` script — `"[]"` → `""`
- `midnight_bridge_promotion` script — `"[]"` → `""` (where applicable)

---

## v1.2 — 2026-04-06

### HTTP fetch stuck-flag watchdog

Fixed a reliability issue where an HTTP fetch that stalled at the TCP level
(connection accepted by the server but data delivery suspended) could hold
`is_updating_today` or `is_updating_tomorrow` locked at `true` indefinitely.
While the flag was stuck, all subsequent auto-retry triggers and manual button
presses were silently blocked.

**Root cause:** The application-level 25 s HTTP timeout does not fire when the
TCP connection is established but the server stops sending data mid-transfer.
The socket never goes idle from the OS perspective, so the timeout never
triggers, and the fetch hangs until the TCP stack eventually resets the
connection — which can take several minutes.

**Fix:** Added a 120-second watchdog to both the today and tomorrow worker
loops (`seconds: /10`). If `is_updating_*` has been `true` for more than 120
seconds, the worker force-clears the flag and sets the status message to
`"Fetch timeout – will retry"`. The next scheduled retry or manual press then
proceeds normally.

**New globals:**
- `is_updating_today_since` (`time_t`) — timestamp when `is_updating_today` was last set to `true`
- `is_updating_tomorrow_since` (`time_t`) — timestamp when `is_updating_tomorrow` was last set to `true`

Both timestamps are set immediately alongside `is_updating_* = true` inside
`smart_price_update` and `smart_tomorrow_price_update`.

**Changed locations in `eprices.yaml`:**
- `globals:` — added `is_updating_today_since` and `is_updating_tomorrow_since`
- `smart_price_update` script — added `id(is_updating_today_since) = ...` alongside flag set
- `smart_tomorrow_price_update` script — added `id(is_updating_tomorrow_since) = ...` alongside flag set
- Today worker (`seconds: /10`) — watchdog block added before existing worker logic
- Tomorrow worker (`seconds: /10`) — watchdog block added before existing worker logic

---

### Tomorrow auto-retry attempt counter fix

Fixed a bug where `tomorrow_retry_count` was not incremented in the 13:55 and
14:55–19:55 hourly retry triggers. Those triggers called
`smart_tomorrow_price_update` directly without incrementing the counter first,
causing the `Tomorrow API Fetch Attempts` diagnostic sensor to undercount
attempts after the first one at 13:25.

**Changed locations in `eprices.yaml`:**
- 13:55 trigger — added `id(tomorrow_retry_count)++` before `.execute()`
- 14:55–19:55 hourly trigger — added `id(tomorrow_retry_count)++` before `.execute()`

---

### Status message improvements

Two status message strings that contained a hardcoded time reference have been
corrected. Both were misleading after a device reboot in the afternoon or a
manual `clear_tomorrow_prices` call outside the normal fetch window.

| Location | Old value | New value |
|---|---|---|
| `tomorrow_update_status_message` global `initial_value` | `"Waiting for 13:20"` | `"No data yet"` |
| `clear_tomorrow_prices` script end | `"Cleared – awaiting next 13:20 window"` | `"Cleared – awaiting fetch window"` |

---

## v1.1 — 2026-04-06

### Negative price provider fee support

Added a separate provider fee multiplier for negative spot prices, configurable
via `secrets.yaml`. This correctly models contracts where the provider fee
structure differs between positive and negative market prices.

**New secret key:**

```yaml
eprices_neg_prov_fee: "0.30"
```

**New global:** `neg_prov_fee` (type: `double`)

**Price calculation is now:**

| Market price | Formula |
|---|---|
| Positive (`raw_mwh >= 0`) | `(raw / 1000) × (1 + prov_fee) × (1 + vat_rate)` |
| Negative (`raw_mwh < 0`) | `(raw / 1000) × (1 - neg_prov_fee) × (1 + vat_rate)` |

**Key values for `eprices_neg_prov_fee`:**

| Value | Meaning |
|---|---|
| `"0.30"` | Provider keeps 30%, pays you 70% of negative price |
| `"0.00"` | Provider passes full negative price to you (no fee) |

**Switch point:** raw API price (`raw_mwh`) before any multiplier is applied.

**VAT:** applied symmetrically to both positive and negative prices, consistent
with net billing models where VAT is calculated on the monthly net sum
(mathematically equivalent due to VAT being a linear multiplier).

**Changed files:** `eprices.yaml`, `secrets.yaml`

**Changed locations in `eprices.yaml`:**
- `substitutions:` — added `neg_prov_fee_value`
- `globals:` — added `neg_prov_fee`
- `parse_energy_charts_today_script` — `MULT` split into `MULT_POS` / `MULT_NEG`
- `parse_energy_charts_tomorrow_script` — `MULT` split into `MULT_POS` / `MULT_NEG`

---

## v1.0 — 2026-04-05

Initial release of EPrices.

---

### Project & device renames

| Item | Value |
|---|---|
| ESPHome node name | `eprices` |
| Friendly name | `EPrices` |
| Helper file | `eprices_nvs.h` |
| C++ namespace | `eprices_nvs` |
| NVS partition namespace | `eprices` |
| Secret key prefix | `eprices_` |

---

### Secrets file keys

```yaml
wifi_ssid
wifi_password
eprices_fallback_ap_ssid
eprices_fallback_ap_password
eprices_api_encryption_key
eprices_timezone          # e.g. "Europe/Ljubljana"
eprices_country_bzn       # e.g. "SI"
eprices_prov_fee          # e.g. "0.12"  (provider fee as decimal multiplier)
eprices_vat_rate          # e.g. "0.22"  (VAT rate as decimal multiplier)
```

---

### Sensors

#### Numeric sensors (`sensor:`)

| Name | Entity ID | Notes |
|---|---|---|
| Today Current Price | `sensor.eprices_today_current_price` | |
| Today Next Price | `sensor.eprices_today_next_price` | |
| Today Average Price | `sensor.eprices_today_average_price` | |
| Today Highest Price | `sensor.eprices_today_highest_price` | |
| Today Lowest Price | `sensor.eprices_today_lowest_price` | |
| Today Current Hourly Price | `sensor.eprices_today_current_hourly_price` | |
| Today Next Hourly Price | `sensor.eprices_today_next_hourly_price` | |
| Today Highest Hourly Price | `sensor.eprices_today_highest_hourly_price` | |
| Today Lowest Hourly Price | `sensor.eprices_today_lowest_hourly_price` | |
| Today Current Max Hourly Price Percentage | `sensor.eprices_today_current_max_hourly_price_percentage` | |
| Tomorrow Current Price | `sensor.eprices_tomorrow_current_price` | Evaluates at now + 86400s |
| Tomorrow Next Price | `sensor.eprices_tomorrow_next_price` | Evaluates at now + 86400s |
| Tomorrow Average Price | `sensor.eprices_tomorrow_average_price` | |
| Tomorrow Highest Price | `sensor.eprices_tomorrow_highest_price` | |
| Tomorrow Lowest Price | `sensor.eprices_tomorrow_lowest_price` | |
| Tomorrow Current Hourly Price | `sensor.eprices_tomorrow_current_hourly_price` | Evaluates at now + 86400s |
| Tomorrow Next Hourly Price | `sensor.eprices_tomorrow_next_hourly_price` | Evaluates at now + 86400s |
| Tomorrow Highest Hourly Price | `sensor.eprices_tomorrow_highest_hourly_price` | |
| Tomorrow Lowest Hourly Price | `sensor.eprices_tomorrow_lowest_hourly_price` | |
| Tomorrow Current Max Hourly Price Percentage | `sensor.eprices_tomorrow_current_max_hourly_price_percentage` | Evaluates at now + 86400s |
| WiFi Signal | `sensor.eprices_wifi_signal` | dBm, diagnostic |
| Uptime | `sensor.eprices_uptime` | Human-readable string, diagnostic |

#### Text sensors (`text_sensor:`)

| Name | Entity ID | Notes |
|---|---|---|
| Today JSON Hourly Prices EUR⁄kWh | `sensor.eprices_today_json_hourly_prices_eur_kwh` | JSON array, 24 values; `""` when no data |
| Today JSON 15-Min Prices EUR⁄kWh (P1 00:00-07:45) | `sensor.eprices_today_json_15_min_prices_eur_kwh_p1_00_00_07_45` | JSON array, 32 values; `""` when no data |
| Today JSON 15-Min Prices EUR⁄kWh (P2 08:00-15:45) | `sensor.eprices_today_json_15_min_prices_eur_kwh_p2_08_00_15_45` | JSON array, 32 values; `""` when no data |
| Today JSON 15-Min Prices EUR⁄kWh (P3 16:00-23:45) | `sensor.eprices_today_json_15_min_prices_eur_kwh_p3_16_00_23_45` | JSON array, 32 values; `""` when no data |
| Today Highest Price Time | `sensor.eprices_today_highest_price_time` | HH:MM |
| Today Lowest Price Time | `sensor.eprices_today_lowest_price_time` | HH:MM |
| Today Highest Hourly Price Time | `sensor.eprices_today_highest_hourly_price_time` | HH:00 |
| Today Lowest Hourly Price Time | `sensor.eprices_today_lowest_hourly_price_time` | HH:00 |
| Today Current Price Status | `sensor.eprices_today_current_price_status` | `Valid` / `Missing` / `Stale` |
| Today Data Loaded Time | `sensor.eprices_today_data_loaded_time` | Stamped on NVS load and HTTP fetch |
| Today Last API Fetch Time | `sensor.eprices_today_last_api_fetch_time` | Stamped on HTTP fetch only; `Never` if NVS only; diagnostic |
| Today Price Update Status | `sensor.eprices_today_price_update_status` | `SUCCESS` / `FAILED/WAITING`; diagnostic |
| Today Price Update Status Message | `sensor.eprices_today_price_update_status_message` | Detailed status string; diagnostic |
| Today API Fetch Attempts | `sensor.eprices_today_api_fetch_attempts` | HTTP fetch count; resets at midnight; diagnostic |
| Today Entry Count | `sensor.eprices_today_entry_count` | Number of stored price points; diagnostic |
| Tomorrow JSON Hourly Prices EUR⁄kWh | `sensor.eprices_tomorrow_json_hourly_prices_eur_kwh` | JSON array, 24 values; `""` when no data |
| Tomorrow JSON 15-Min Prices EUR⁄kWh (P1 00:00-07:45) | `sensor.eprices_tomorrow_json_15_min_prices_eur_kwh_p1_00_00_07_45` | JSON array, 32 values; `""` when no data |
| Tomorrow JSON 15-Min Prices EUR⁄kWh (P2 08:00-15:45) | `sensor.eprices_tomorrow_json_15_min_prices_eur_kwh_p2_08_00_15_45` | JSON array, 32 values; `""` when no data |
| Tomorrow JSON 15-Min Prices EUR⁄kWh (P3 16:00-23:45) | `sensor.eprices_tomorrow_json_15_min_prices_eur_kwh_p3_16_00_23_45` | JSON array, 32 values; `""` when no data |
| Tomorrow Highest Price Time | `sensor.eprices_tomorrow_highest_price_time` | HH:MM |
| Tomorrow Lowest Price Time | `sensor.eprices_tomorrow_lowest_price_time` | HH:MM |
| Tomorrow Highest Hourly Price Time | `sensor.eprices_tomorrow_highest_hourly_price_time` | HH:00 |
| Tomorrow Lowest Hourly Price Time | `sensor.eprices_tomorrow_lowest_hourly_price_time` | HH:00 |
| Tomorrow Current Price Status | `sensor.eprices_tomorrow_current_price_status` | `Valid` / `Missing` / `Waiting...` |
| Tomorrow Data Loaded Time | `sensor.eprices_tomorrow_data_loaded_time` | Stamped on NVS load and HTTP fetch; `Outside fetch window` before 13:20 |
| Tomorrow Last API Fetch Time | `sensor.eprices_tomorrow_last_api_fetch_time` | Stamped on HTTP fetch only; `Never` if NVS only; diagnostic |
| Tomorrow Price Update Status | `sensor.eprices_tomorrow_price_update_status` | `SUCCESS` / `FAILED/WAITING`; diagnostic |
| Tomorrow Price Update Status Message | `sensor.eprices_tomorrow_price_update_status_message` | Detailed status string; diagnostic |
| Tomorrow API Fetch Attempts | `sensor.eprices_tomorrow_api_fetch_attempts` | HTTP fetch count; resets at 13:25; diagnostic |
| Tomorrow Entry Count | `sensor.eprices_tomorrow_entry_count` | Number of stored price points; diagnostic |
| Last Reboot | `sensor.eprices_last_reboot` | Boot timestamp; diagnostic |
| Last Update Source | `sensor.eprices_last_update_source` | `NVS_boot` / `HTTP_today` / `midnight_bridge` etc.; diagnostic |
| Today NVS Status | `sensor.eprices_today_nvs_status` | NVS load/store result; diagnostic |
| Tomorrow NVS Status | `sensor.eprices_tomorrow_nvs_status` | NVS load/store result; diagnostic |
| Today Data Date | `sensor.eprices_today_data_date` | Date of stored today data; diagnostic |
| Tomorrow Data Date | `sensor.eprices_tomorrow_data_date` | Date of stored tomorrow data; diagnostic |

#### Buttons

| Name | Entity ID |
|---|---|
| Force Today's Update | `button.eprices_force_today_s_update` |
| Force Tomorrow's Update | `button.eprices_force_tomorrow_s_update` |
| Reboot Device | `button.eprices_reboot_device` |

---

### Key behaviours

- **Price calculation:** `price_eur_kwh = (raw_eur_mwh / 1000) × (1 + prov_fee) × (1 + vat_rate)`
- **Negative prices:** formatted as `%.3f` (3 decimal places) to stay within the 255-character HA text sensor state limit; positive prices use `%.4f`
- **DST-safe:** all price indexing uses UNIX timestamps and binary search — no hour-slot arithmetic
- **NVS persistence:** prices survive reboots; stale data (date mismatch) is discarded and triggers a fresh HTTP fetch
- **Today Current Price Status** shows `Stale` if stored date does not match today's date
- **Tomorrow live sensors** evaluate at `now + 86400s` so they reflect tomorrow at the same local time
- **Tomorrow Data Loaded Time** shows `Outside fetch window` when device boots before 13:20
- **API fetch times** reset to `Never` at midnight bridge and on `clear_tomorrow_prices`
- **API fetch attempt counters** reset to `0` at midnight and publish `0` immediately on boot
- **Uptime** displayed as human-readable string: `45 s` / `5 min` / `3 h 22 min` / `12 d 4 h` / `4 months 12 d`
- **JSON sensors** publish `""` (empty string) when no data is available — all eight JSON sensors behave consistently
