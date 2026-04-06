# EPrices – Changelog

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
| Today JSON Hourly Prices EUR⁄kWh | `sensor.eprices_today_json_hourly_prices_eur_kwh` | JSON array, 24 values |
| Today JSON 15-Min Prices EUR⁄kWh (P1 00:00-07:45) | `sensor.eprices_today_json_15_min_prices_eur_kwh_p1_00_00_07_45` | JSON array, 32 values |
| Today JSON 15-Min Prices EUR⁄kWh (P2 08:00-15:45) | `sensor.eprices_today_json_15_min_prices_eur_kwh_p2_08_00_15_45` | JSON array, 32 values |
| Today JSON 15-Min Prices EUR⁄kWh (P3 16:00-23:45) | `sensor.eprices_today_json_15_min_prices_eur_kwh_p3_16_00_23_45` | JSON array, 32 values |
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
| Tomorrow JSON Hourly Prices EUR⁄kWh | `sensor.eprices_tomorrow_json_hourly_prices_eur_kwh` | JSON array, 24 values |
| Tomorrow JSON 15-Min Prices EUR⁄kWh (P1 00:00-07:45) | `sensor.eprices_tomorrow_json_15_min_prices_eur_kwh_p1_00_00_07_45` | JSON array, 32 values |
| Tomorrow JSON 15-Min Prices EUR⁄kWh (P2 08:00-15:45) | `sensor.eprices_tomorrow_json_15_min_prices_eur_kwh_p2_08_00_15_45` | JSON array, 32 values |
| Tomorrow JSON 15-Min Prices EUR⁄kWh (P3 16:00-23:45) | `sensor.eprices_tomorrow_json_15_min_prices_eur_kwh_p3_16_00_23_45` | JSON array, 32 values |
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
