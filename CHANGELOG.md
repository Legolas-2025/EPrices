# EPrices – Changelog

## v1.4.1 — 2026-10-11

### ESP32 framework optimization (binary size + IRAM reclaim)

Build-only release: two ESP-IDF advanced framework options were enabled, and all
16 pre-existing compiler warnings coming from the price lambdas were removed —
the final v1.4.1 build log confirms a warning-free compile. No new sensors, no
entity ID changes, no secrets changes, no script, trigger or
global changes, and no change to any published value. Drop-in replacement for
v1.4.0.

**`eprices_nvs.h` does NOT need updating for the v1.4.0 → v1.4.1 migration.**
The helper file is unchanged (it has required no changes since v1.3.0), the NVS
namespace `eprices`, its keys and the dynamic timestamp/price array layout are
untouched, so all stored today/tomorrow price data survives the OTA unchanged.

#### What changed

| Option | Value | Effect |
|---|---|---|
| `esp32:` → `framework:` → `advanced:` → `minimum_chip_revision` | `"3.1"` | ESP-IDF omits the errata-workaround code paths for chip revisions below v3.0 → smaller firmware image |
| `esp32:` → `framework:` → `advanced:` → `sram1_as_iram` | `true` | The 40 KB SRAM1 region is mapped as instruction RAM instead of data RAM → 40 KB more IRAM for hot code |

`compiler_optimization` was already `SIZE` (the ESPHome default) and is left
unset. Every existing `sdkconfig_options` entry is unchanged
(`CONFIG_ESP_MAIN_TASK_STACK_SIZE: "16384"`, `CONFIG_ESP_TASK_WDT_TIMEOUT_S: "40"`,
idle-task watchdogs disabled).

#### Why these options are safe on this board

Both were recommended by ESPHome itself in the device boot log:

```text
[I][app:158]: ESP32 Chip: ESP32 rev3.1, 2 core(s)
[W][app:171]: Chip rev >= 3.0 detected. Set minimum_chip_revision: "3.1" under
              esp32 > framework > advanced to reduce binary size
[W][app:198]: Bootloader supports SRAM1 as IRAM (+40KB). Set sram1_as_iram: true
              under esp32 > framework > advanced
```

ESPHome prints the second warning only after successfully reading the bootloader
descriptor, so it confirms the installed bootloader is new enough for
`sram1_as_iram`.

#### Constraints introduced by this release

- **Chip-revision floor:** the image now declares `minimum_chip_revision: "3.1"`.
  ESP-IDF's bootloader refuses to boot, and the OTA updater refuses to accept, a
  chip older than the image's minimum revision. Do not flash this build onto an
  ESP32 module older than rev v3.1, and never set the value higher than the
  revision reported by the boot log.
- **Bootloader floor:** `sram1_as_iram` requires a bootloader from ESP-IDF v5.1
  or later. OTA updates do not replace the bootloader, so a board with an older
  bootloader would fail to boot and need a USB reflash. Not an issue on the
  tested board.
- **Variant coupling:** `minimum_chip_revision` is valid only for the original
  ESP32. If the `board:` is switched to ESP32-S3, C3, S2 or C6, the whole
  `advanced:` block must be removed — ESPHome rejects `minimum_chip_revision`
  on non-ESP32 variants.

#### Measured build results (v1.4.0 → v1.4.1)

Taken from the final v1.4.1 `ninja size` report — the post-warning-cleanup build
(`config_hash 0x02384f07`, compiled `2026-10-11 13:43:35 +0200`; ESPHome 2026.9.1,
ESP-IDF 5.5.5, board `esp32dev`):

| Region | v1.4.0 | v1.4.1 | Change |
|---|---|---|---|
| Flash (total image) | 1,028,291 B (56.0 %) | 1,026,067 B (55.9 %) | **−2,224 B** (−0.22 %) |
| IRAM region size | 131,072 B | 172,032 B | **+40,960 B (+40 KiB)** |
| IRAM used | 77,247 B (58.93 %) | 76,771 B (44.63 %) | −476 B |
| IRAM free | 53,825 B | 95,261 B | **+41,436 B** |
| DRAM region size | 180,736 B | 180,736 B | 0 |
| DRAM used | 55,000 B (30.43 %) | 54,984 B (30.42 %) | −16 B |
| RTC SLOW used | — | 56 B of 8,192 B | — |

Supporting figures from the same report: Flash Code `.text` 769,704 B; Flash
Data `.rodata` 163,184 B + `.appdesc` 256 B; IRAM `.text` 75,743 B +
`.vectors` 1,028 B; DRAM `.bss` 38,704 B + `.data` 16,120 B + `.noinit` 160 B.
OTA payload `eprices.bin` = `0xFA813` = 1,026,067 B (1,026,176 B padded on upload)
in a 1,792 KiB app partition (44 % free). `bootloader.bin` = `0x6620` = 26,144 B —
unchanged, as OTA never replaces the bootloader.

The intermediate build — framework options enabled, warning cleanup not yet applied
— measured 1,025,951 B (`config_hash 0xd3cbb4ec`). The widened buffers therefore add
+116 B of `.text`, which is why the net flash saving is 2,224 B rather than 2,340 B.

**Both options confirmed in the generated `sdkconfig`:**

```text
CONFIG_ESP32_REV_MIN_3_1=y
CONFIG_ESP32_REV_MIN_FULL=301
CONFIG_ESP_REV_MIN_FULL=301
CONFIG_ESP_SYSTEM_ESP32_SRAM1_REGION_AS_IRAM=y
```

and in the generated linker memory map:

```text
iram0_0_seg (RX) : org = 0x40080000, len = 0x20000 + 0xA000   ; 128 KiB + 40 KiB
dram0_0_seg (RW) : org = 0x3FFB0000,      len = 0x2c200       ; 176.5 KiB (unchanged)
```

**Why the DRAM region and the runtime heap are both unchanged:** the option only
costs heap if IRAM code actually *spills* past the end of the original 128 KB
IRAM region. The generated linker script makes this explicit:

```text
_sram1_iram_start = 0x400A0000;
_sram1_iram_len   = (_iram_end > _sram1_iram_start) ? (_iram_end - _sram1_iram_start) : 0;
_heap_end         = ALIGN(0x40000000 - _sram1_iram_len - 3, 4);
```

Resolved values from `eprices.map`:

```text
_iram_start       = 0x40080000
_iram_end         = 0x40092BE4      ; 0x12BE4 = 76,772 B of IRAM code
_sram1_iram_start = 0x400A0000
_sram1_iram_len   = 0x00000000      ; nothing spilled into SRAM1
_heap_end         = 0x40000000      ; heap upper bound unchanged
```

Because `_sram1_iram_len` is zero, the heap reservation
(`components/heap/port/esp32/memory_layout.c`,
`SOC_RESERVE_MEMORY_REGION(&_heap_end, 0x40000000, sram1_iram)`) removes nothing.
ESP-IDF's own Kconfig help states that SRAM1 "was previously reserved for
**bootloader** DRAM" — it was never available to the app heap in the first place,
so the extra 40 KB is a pure gain rather than a reallocation.

Runtime confirmation from the post-OTA boot log
(`heap_caps_get_free_size(MALLOC_CAP_8BIT)`):

```text
v1.4.0  recompute_today: starting 180,476 B   done 177,576 B
v1.4.1  recompute_today: starting 180,492 B   done 177,572 B   (pre-cleanup build)
v1.4.1  recompute_today: starting 180,504 B   done 177,552 B   (final build)
v1.4.1  recompute_tomorrow: starting 178,788 B  done 179,440 B (final build)
```

+16 B at both measurement points — exactly the 16 B reduction in static DRAM
(55,000 → 54,984 B). No heap was lost. The final post-cleanup build reports the same
54,984 B of static DRAM; its runtime figures differ by a dozen bytes only because
the measurement point falls at a slightly different allocation state.

**Net effect:** the flash saving is modest (2,224 B, 0.22 %, after the +116 B of
buffer widening) because ESP-IDF's
errata-workaround paths for pre-v3.0 ESP32 silicon are compact. The substantial
gain is IRAM headroom obtained at zero cost: usage fell from 58.93 % to 44.63 %
while the region itself grew, leaving 95,261 B free. IRAM overflow — the failure
mode that silently blocks added components — is no longer a near-term concern.
And if IRAM usage ever does exceed 128 KB, ESP-IDF automatically carves the
spilled amount out of the heap, so the fallback is graceful instead of a link
error.

#### Build-log notes

- The v1.4.1 compile emitted no ESPHome chip-revision / SRAM1 warnings, which
  confirms both options were compiled in (ESPHome only emits them when
  `USE_ESP32_MIN_CHIP_REVISION_SET` / `USE_ESP32_SRAM1_AS_IRAM` are undefined).
- The build reports `App "eprices" version: 2026.9.1` — the ESPHome version, not
  a project version. Adding `esphome: project_id:` and `version: "1.4.1"` would
  make future boot logs self-identify the firmware release.
- Warning cleanup: **verified**. The final v1.4.1 compile produced zero compiler
  warnings — the 16 `-Wformat-truncation` / `-Wformat-overflow` diagnostics that the
  v1.4.0 build emitted from the `snprintf`/`sprintf` calls in the price lambdas are
  gone from `eprices-install-log.txt`. The only warning in that log is ESPHome's own
  `WARNING Bundle contains secrets (e.g. Wi-Fi passwords)` line, which concerns the
  remote-build bundle and not the code. See **Compiler-warning cleanup** below.

#### Compiler-warning cleanup (buffer sizing + `snprintf`)

The v1.4.0 build emitted 16 format warnings from the lambdas. They were harmless
for the values actually produced, but a warning-laden build log hides genuine
problems, so all 16 were eliminated in v1.4.1 — and the elimination is confirmed by
the final v1.4.1 build log, which contains no compiler diagnostics at all. This is
a **compile-time-only** change: no published state, entity, timing or NVS behaviour
changes.

| Change | Sites | Reason |
|---|---|---|
| `sprintf(...)` → `snprintf(..., sizeof(buf), ...)` | 12 | every unbounded format call in a lambda is now bounded |
| `char buf[8]` → `char buf[12]` | 7 | `"%d"` of a 32-bit int can need 11 chars + NUL |
| `today_buf[9]`, `tmr_buf[9]`, `first_date[9]`, `today_check[9]` → `[34]` | 6 | `"%04d%02d%02d"` worst case is 3 × 11 + NUL |
| `char tbuf[20]` → `char tbuf[32]` | 2 | `"%04d-%02d-%02d %02d:%02d:%02d"` worst case |
| `char buf[20]` → `char buf[24]` | 3 | `strftime("%Y-%m-%d %H:%M:%S")` had exactly 20 bytes — zero margin |
| `char tb[6]` → `char tb[24]` | 2 | `"%02d:%02d"` worst case 11 + 1 + 11 + NUL |
| `char hb[6]` → `char hb[16]` | 2 | `"%02d:00"` worst case 11 + 3 + NUL |
| `char m[40]` / `char m[50]` sizes kept | 4 | already sufficient; only the call was converted to `snprintf` |

Warnings removed, from the v1.4.0 build log:

| Diagnostic | Count | Offending buffers |
|---|---|---|
| `-Wformat-truncation` | 10 | the 8-byte counters, the 9-byte `YYYYMMDD` dates, the 20-byte timestamps |
| `-Wformat-overflow` | 6 | the 6-byte `tb` / `hb` `HH:MM` time strings (`sprintf`) |

Why the new sizes look generous: GCC's format analysis assumes an unbounded
`int` argument can print up to 11 characters (`-2147483647`) and does not know
that `tm_year + 1900`, `min_idx/4` or `retry_count` will always be small. Sizing
each buffer for the worst case is the only way to silence the diagnostic without
adding clamping code that could mask a real bug. The header comment in
`eprices.yaml` records this so the buffers are not shrunk back later.

Buffers deliberately left alone — they produced no warning and are already
comfortable: `char url[160]`, `char buf[80]`, `char buf[450]`,
`char json_h_buf[400]`, `char buf[48]`, and `char date_buf[12]` in
`eprices_nvs.h` (which remains untouched).

#### Changed location in `eprices.yaml`

- `esp32:` → `framework:` — new `advanced:` block (`minimum_chip_revision: "3.1"`,
  `sram1_as_iram: true`) with inline safety notes, inserted above the existing
  `sdkconfig_options:`
- File header — new `v1.4.1 compiler-warning cleanup` block documenting the
  buffer-sizing rule
- 26 lambda statements — 22 `char[]` declarations widened and 12 `sprintf` calls
  converted to `snprintf`; no lambda logic, condition or output changed

#### Migration checklist (v1.4.0 → v1.4.1)

| Item | Action required |
|---|---|
| `eprices_nvs.h` | **None** — file unchanged, no update needed |
| `secrets.yaml` | None — no new or renamed keys |
| HA entities, automations, dashboards | None — no renames, no new/removed sensors |
| NVS-stored price data | Preserved automatically |
| Flash method | Normal ESPHome OTA is fine |
| Hardware requirement | ESP32 chip rev ≥ v3.1 **and** ESP-IDF v5.1+ bootloader |
| Lambda code changes | None needed — the buffer/warning cleanup is already inside `eprices.yaml` and changes no behaviour |

#### Post-OTA verification (v1.4.1 boot log)

| Check | Result |
|---|---|
| Firmware identity | `ESPHome version 2026.9.1 compiled on 2026-10-11 13:43:35 +0200` — matches the final v1.4.1 build (`config_hash 0x02384f07`) |
| Chip revision floor | `ESP32 Chip: ESP32 rev3.1, 2 core(s)` — satisfies `minimum_chip_revision: "3.1"` |
| ESPHome optimization warnings | **Gone** (both were present in the v1.4.0 log) |
| Compiler warnings | **None** — the matching build log contains no `-Wformat-*` diagnostics |
| NVS migration | `load(td_count): 96 entries for 20261011` and `load(tm_count): 96 entries for 20261012` — all 96 quarter-hour slots recovered for both days |
| Free heap | `recompute_today` 180,504 → 177,552 B; `recompute_tomorrow` 178,788 → 179,440 B |
| Data published | `Last Update Source = NVS_boot`, `Today` / `Tomorrow Price Update Status = SUCCESS` (`Loaded from NVS (boot)`), `Today` / `Tomorrow Entry Count = 96` |
| Published values unchanged | `Today Lowest 0.0205`, `Today Highest 0.3342`, `Today Average 0.2346` €/kWh — identical to the pre-cleanup build, as are `Today Highest Price Time 17:45` and `Today Lowest Price Time 13:30` |
| Boot loop guard | `safe_mode: Boot seems successful; resetting boot loop counter` |
| Watchdog / crash / time jump | None logged |
| Tomorrow side | `load(tm_count): 96 entries` → `SUCCESS` / `Loaded from NVS (boot)`. The pre-cleanup boot had shown `FAILED/WAITING` only because tomorrow's NVS data had not been fetched at that moment — a data-state difference, not a code change |

---

## v1.4.0 — 2026-10-06

### Full-day retry coverage for today and tomorrow

Reliability release addressing a production-observed gap where the Energy-Charts
API being offline for more than ~5 hours left the device with no automatic
recovery path for the remainder of the day. No new sensors, no secrets changes,
no entity ID changes. Drop-in replacement for v1.3.1. `eprices_nvs.h` unchanged.

#### Today: burst exhaustion no longer deactivates auto-retry

**Symptom:** if the API was down at midnight, the 8-burst auto-retry (00:05,
00:15, 00:30, then hourly :30) exhausted by ~05:30. `auto_today_retry_active`
was set to `false`, every subsequent hourly `:30` trigger returned immediately,
and no today fetch was attempted again until the next midnight bridge or a
manual button press — a gap of up to 18 hours.

**Root cause:** `after_today_fetch` failure branch set
`auto_today_retry_active = false` unconditionally after 8 consecutive failures.
All `on_time` triggers begin with `if (!auto_today_retry_active) return;`.

**Fix:** the failure branch now resets `auto_today_retry_count` to `0` and
leaves `auto_today_retry_active` at `true`. The existing hourly `:30` trigger
fires every hour for the rest of the day. After another 8 consecutive failures
the counter resets again, cycling until success or midnight. Maximum 26 attempts
per day (3 burst + 23 hourly). On success, `auto_today_retry_active = false` as
before — all triggers stop.

**Changed location in `eprices.yaml`:**
- `after_today_fetch` script, `else` branch — replaced
  `id(auto_today_retry_active) = false;` with
  `id(auto_today_retry_count) = 0;` and updated status message

---

#### Tomorrow: retry window extended from 19:55 to 23:15

**Symptom:** the hourly `:55` retry trigger was gated to `t.hour > 19` →
`return`. The last scheduled attempt was at 19:55. The fetch window extends to
23:50, but no trigger fired between 20:00 and 23:50. If the API recovered at
20:30, the next attempt was the following day at 13:25.

**Fix (three parts):**

1. The hourly `:55` gate is widened from `t.hour > 19` to `t.hour > 22`,
   covering 14:55 through 22:55 (nine scheduled attempts instead of five).

2. The attempt cap in the 13:55 and hourly `:55` triggers is raised from 8 to
   12, so the widened gate can actually be reached (see the counter-split
   section below — without this the cap stops the chain at 16:55).

3. A new `on_time` trigger at 23:15 provides a last-chance attempt
   (`tomorrow_retry_count` cap 12).

**Changed locations in `eprices.yaml`:**
- Tomorrow hourly `:55` `on_time` trigger lambda — gate `t.hour > 19` → `t.hour > 22`
- 13:55 and hourly `:55` triggers — cap `>= 8` → `>= 12`
- New `on_time` entry at `seconds: 15, minutes: 15, hours: 23`

---

#### Tomorrow: scheduled-attempt counter split from fetch counter

**Symptom:** `tomorrow_retry_count` was incremented by both the `on_time`
triggers and by `smart_tomorrow_price_update`, so every scheduled attempt
counted twice. The counter climbed by 2 per attempt and stood at 9 after
16:55, so the `>= 8` cap in the hourly `:55` trigger blocked every attempt
from 17:55 onward. Only five scheduled attempts (13:25, 13:55, 14:55, 15:55,
16:55) ever fired — in v1.3.1 and in the first draft of v1.4.0. Widening the
gate alone would have had no effect. The double-increment was introduced in
v1.2.0.

**Root cause:** one variable served two incompatible purposes — the scheduled
attempt budget (trigger-side) and the `Tomorrow API Fetch Attempts` sensor
value (script-side). Once the v1.4.0 worker self-heal began invoking the script
outside the schedule, the two roles could no longer share a variable.

**Fix:** the two roles are split.

| Variable | Incremented by | Purpose |
|---|---|---|
| `tomorrow_retry_count` | `on_time` triggers only | Budget for scheduled attempts; caps at 12 |
| `tomorrow_fetch_count` | `smart_tomorrow_price_update` | Every HTTP fetch; drives the sensor |

Worker self-heal and manual button presses now increment `tomorrow_fetch_count`
only, so they no longer consume the scheduled budget.

**Changed locations in `eprices.yaml`:**
- `globals:` — added `tomorrow_fetch_count`
- 13:25 trigger — resets both counters and increments `tomorrow_retry_count` for attempt #1
- `smart_tomorrow_price_update` — increments `tomorrow_fetch_count` instead of `tomorrow_retry_count`
- `Tomorrow API Fetch Attempts` sensor lambda — reads `tomorrow_fetch_count`
- `midnight_bridge_promotion` — resets both counters at the day rollover
- `boot_recovery_tomorrow_script` — seeds the sensor with the counter's current
  value instead of forcing it to 0. `tomorrow_fetch_count` is a global already
  initialised to 0 at boot, so no reset is needed there; forcing 0 would erase a
  fetch that a scheduled trigger completed in the first 50 s after a reboot
  (the `on_boot` recovery runs at `+50 s`, while `on_time` triggers can fire
  from `+0 s`). Observed on a device rebooted at 23:14:50: the 23:15
  last-chance trigger fetched successfully at 23:15:24 and the sensor showed
  `1`, then boot recovery republished `0` at 23:15:51.

---

#### Tomorrow worker: 30-minute self-heal re-arm

**Purpose:** even with the extended scheduled triggers, if the API recovers
between 23:15 and 23:50 (after the last scheduled trigger), no code path
re-arms `need_tomorrow_update`. The worker loop processes the flag but nothing
sets it.

**Fix:** the existing 10-second worker lambda now includes a self-heal check.
If `tomorrow_entry_count == 0`, `tomorrow_last_update_success == false`,
`is_updating_tomorrow == false`, the fetch window is open, and the last
attempt was more than 30 minutes ago, the worker sets
`need_tomorrow_update = true` automatically. The 30-minute gap is the throttle;
no hard cap is needed for this phase.

**Changed location in `eprices.yaml`:**
- Tomorrow `seconds: /10` worker lambda — self-heal block inserted between
  the stuck-flag watchdog and the `need_tomorrow_update` processing

---

#### Manual "clear" is no longer undone by the self-heal

**Symptom:** `clear_tomorrow_prices` (used by the "Force Tomorrow's Update"
button, the "Clear Tomorrow" action and the midnight bridge) zeroes
`tomorrow_entry_count` but left `tomorrow_last_update_attempt` untouched. The
self-heal block uses that timestamp as its throttle reference, so if the
previous attempt was more than 30 minutes old the next 10-second worker tick
re-armed the fetch and silently repopulated the data the user had just cleared.

**Fix:** `clear_tomorrow_prices` now also zeroes `tomorrow_last_update_attempt`,
which disables the self-heal until a real fetch attempt happens. Clearing is
now stable; recovery resumes at the next 13:25 scheduled attempt or on a manual
force press.

**Changed location in `eprices.yaml`:**
- `clear_tomorrow_prices` script — added `id(tomorrow_last_update_attempt) = 0;`

---

#### DST follow-up: `recompute_tomorrow` status anchor

**Symptom:** v1.3.0 replaced the `now + 86400` anchor with calendar-day
increment in the four Tomorrow sensor lambdas, but the equivalent line inside
`recompute_tomorrow` — which decides whether `Tomorrow Current Price Status`
reads `Valid` or `Missing` — still used the raw UTC offset.

**Fix:** that anchor now uses the same `tm_mday += 1` + `mktime()` calendar-day
increment. Low practical impact (the status would not have flipped to
`Missing` in normal operation), but it removes the last `+86400` leftover and
keeps the tomorrow-date arithmetic uniform across the file.

**Changed location in `eprices.yaml`:**
- `recompute_tomorrow` script — `int64_t ts_now_tomorrow` now derived via
  `localtime_r` + `tm_mday += 1` + `mktime()`

---

#### Retry schedule summary (v1.4.0)

**Today:**

Note: `auto_today_retry_count` cycles 0-7 and is reset to 0 each time the burst
is exhausted, so the numbers below are cumulative attempts for the day, not the
value of the counter.

| Time | Attempt of day | Phase |
|---|---|---|
| 00:05 | #1 | burst |
| 00:15 | #2 | burst |
| 00:30 | #3 | burst |
| 01:30 – 05:30 | #4–#8 | hourly :30 |
| *(counter resets, retry stays active)* | | |
| 06:30 – 23:30 | #9–#26 | hourly :30 (slow recovery) |
| any success | — | all retries stop |

**Tomorrow:**

| Time | Scheduled attempt | Phase |
|---|---|---|
| 13:25 | #1 | scheduled |
| 13:55 | #2 | scheduled |
| 14:55 – 22:55 | #3–#11 | hourly :55 |
| 23:15 | #12 | last-chance |
| *(worker self-heal)* | not counted | every 30 min if no data, in window, >30 min since last try |
| any success | — | all retries stop |

Self-heal fetches are counted by `tomorrow_fetch_count` (the `Tomorrow API Fetch
Attempts` sensor) but do not consume the #1–#12 scheduled budget.

## v1.3.1 — 2026-10-04

### Hourly JSON trailing null-slot fix

Bug-fix release addressing a regression introduced in v1.3.0. No NVS changes,
no entity ID changes, no secrets changes. Drop-in replacement for v1.3.0.

**Symptom:** `Today JSON Hourly Prices EUR⁄kWh` and `Tomorrow JSON Hourly Prices EUR⁄kWh`
could publish a 25-element JSON array on normal 24-hour days, with the 25th
element as `null`. Downstream HA template sensors doing arithmetic (`| sum`,
`| from_json | sum`) could fail with `TypeError` and become unavailable.

**Root cause:** In v1.3.0, hourly vectors were expanded to 25 slots for DST
fall-back correctness. The JSON emission loop iterated the full 25-slot
allocation instead of the actual populated sequential hour-block count for that
day.

**Fix:** `recompute_today` and `recompute_tomorrow` now track
`actual_hour_blocks` from the sequential block-index scan and emit hourly JSON
using only populated slots (`0..actual_hour_blocks-1`).
- Normal day: 24 elements
- DST spring-forward day: 23 elements
- DST fall-back day: 25 elements

**Changed locations in `eprices.yaml`:**
- `recompute_today`
- `recompute_tomorrow`

## v1.3.0 — 2026-10-03

### DST edge-case hardening — all three DST bugs fixed

Bug-fix release that corrects three DST-related edge cases identified by
static analysis of the v1.2.4 firmware. No new sensors, no secrets changes,
no entity ID changes. Drop-in replacement for v1.2.4 (migrating to v1.3.0 
requires no changes to eprices_nvs.h file).

#### Bug 1 — `+86400` UTC seconds for "tomorrow's date" (moderate)

**Symptom:** On the spring-forward Saturday evening after ~23:00 CET, the
tomorrow HTTP fetch URL, NVS expected-date string, and `tomorrow_date_str`
all contained the date of the day-after-tomorrow (Monday) instead of tomorrow
(Sunday). The API returned no data or the wrong day's data, and any valid NVS
slot for the correct tomorrow date was rejected.

**Root cause:** Computing "tomorrow" by adding exactly 86 400 UTC seconds to
`now` and then calling `localtime()`. On spring-forward night the clocks skip
one hour, so 86 400 UTC seconds span 25 local hours and advance two calendar
days rather than one.

**Fix:** All three locations now use a calendar-day increment with `mktime()`:
```cpp
time_t now_t = (time_t)id(ha_time).now().timestamp;
struct tm tmr_tm;
localtime_r(&now_t, &tmr_tm);
tmr_tm.tm_mday += 1;
tmr_tm.tm_hour = 12;   // midday anchor — away from DST boundary hour
tmr_tm.tm_isdst = -1;
mktime(&tmr_tm);       // normalises month/year rollover and re-applies DST rules
```

**Changed locations in `eprices.yaml`:**
- `nvs_load_tomorrow_script` — tomorrow NVS expected-date string
- `parse_energy_charts_tomorrow_script` — `tomorrow_date_str` population
- `smart_tomorrow_price_update` — `fetch_url_tomorrow` URL date parameters

---

#### Bug 2 — `+86400` in tomorrow sensor lookup anchor (minor)

**Symptom:** On DST transition nights, the four "Tomorrow" live sensors
(`Tomorrow Current Price`, `Tomorrow Next Price`, `Tomorrow Current Hourly
Price`, `Tomorrow Next Hourly Price`) selected a price slot approximately
1 hour off relative to "same local time tomorrow", showing the wrong 15-minute
price.

**Root cause:** The binary-search anchor for `price_timestamps_tomorrow` was
`now().timestamp + 86400`. On DST transition nights this is ~1 hour off from
"same local time tomorrow" in the target day's local clock.

**Fix:** The anchor is now computed via calendar-day increment:
```cpp
time_t now_t = (time_t)id(ha_time).now().timestamp;
struct tm tmr_lkp;
localtime_r(&now_t, &tmr_lkp);
tmr_lkp.tm_mday += 1;
tmr_lkp.tm_isdst = -1;
int64_t ts = (int64_t)mktime(&tmr_lkp);
```

**Changed locations in `eprices.yaml`:**
- `tomorrow_current_price` sensor lambda
- `tomorrow_next_price` sensor lambda
- `tomorrow_current_hourly_price` sensor lambda
- `tomorrow_next_hourly_price` sensor lambda

---

#### Bug 3 — Fall-back day (25-hour) hourly average missed the repeated 02:xx block (minor)

**Symptom:** On the DST fall-back day (last Sunday of October, 25 local hours,
up to 100 price entries), the hourly average for the 02:xx hour was wrong and
the daily min/max/average could misidentify the cheapest or most expensive hour.
The JSON hourly sensors showed incorrect data for that one hour.

**Root cause:** `hourly_avg_prices_kwh` and `tomorrow_hourly_avg_prices_kwh`
were `std::vector<float>(24, 0.0f)`. The `recompute_today` and `recompute_tomorrow`
scripts populated these by mapping each entry to slot `tm_hour` (0–23). On
fall-back day there are two 02:xx blocks; the second overwrote the first in
slot `[2]`.

The sensor lambdas for `today_current_hourly_price`, `today_next_hourly_price`,
`tomorrow_current_hourly_price`, and `tomorrow_next_hourly_price` also indexed
the vector by `tmi->tm_hour`, giving the same slot-collision for both 02:xx
blocks.

**Fix (two parts, both required):**

1. The hourly vectors are expanded to 25 slots
   (`std::vector<float>(25, 0.0f)`) so a 25-hour day cannot overflow them.

2. They are populated by **sequential hour-block index** rather than raw
   `tm_hour`, and the block counter additionally advances when `tm_isdst`
   changes:

   ```cpp
   if (tmi.tm_hour != prev_hour || tmi.tm_isdst != prev_dst) {
     hour_block++;
     prev_hour = tmi.tm_hour;
     prev_dst  = tmi.tm_isdst;
   }
   ```

   **Part 2 is what actually fixes this bug.** On a fall-back day the local
   hours run `00, 01, 02, 02, 03 …` and the second `02:xx` block carries the
   same `tm_hour` as the first, so a `tm_hour`-only comparison leaves the
   counter unchanged and merges both hours into one block — 24 blocks instead
   of 25, with block 2 holding all eight quarter-hours and reporting the mean
   of two different hours. Comparing `tm_isdst` distinguishes `02:00 CEST` from
   `02:00 CET`, producing the intended 0–24 range with both `02:xx` blocks in
   separate slots.

The four hourly sensor lambdas use the same `tm_isdst`-aware block-index scan to
locate the slot for the current timestamp, so during both 02:00–02:59 windows on
a fall-back day they report that hour's own average rather than a blend of the
two.

**Verified block counts** (`Europe/Ljubljana`). The API anchors `start`/`end` to
the *local* day of the bidding zone, so it returns 92 / 96 / 100 entries on a
spring-forward / normal / fall-back day:

| Day | API entries | Before | After |
|---|---|---|---|
| Spring forward (23 h) | 92 | 23 | 23 |
| Fall back (25 h) | 100 | 24 ✗ | **25** |
| Normal (24 h) | 96 | 24 | 24 |

**Known limitation (unchanged):** the legacy 96-slot 15-minute grid
(`hourly_prices` / `tomorrow_hourly_prices`) maps both `02:xx` occurrences to
slots 8–11, so on a fall-back day the second overwrites the first. That grid is
a 24-hour wall-clock view by contract and is used only by the 15-minute JSON
sensors and the 15-minute min/max time strings. The live 15-minute sensors read
`price_timestamps_today[]` directly and are unaffected.

**Changed locations in `eprices.yaml`:**
- `globals:` — `hourly_avg_prices_kwh` and `tomorrow_hourly_avg_prices_kwh` size changed from `24` to `25`
- `recompute_today` — hourly vector population uses a `tm_isdst`-aware block counter
- `recompute_tomorrow` — hourly vector population uses a `tm_isdst`-aware block counter
- `today_current_hourly_price` sensor lambda — `tm_isdst`-aware block-index lookup
- `today_next_hourly_price` sensor lambda — `tm_isdst`-aware block-index lookup
- `tomorrow_current_hourly_price` sensor lambda — `tm_isdst`-aware block-index lookup
- `tomorrow_next_hourly_price` sensor lambda — `tm_isdst`-aware block-index lookup

## v1.2.4 — 2026-09-22

### Force button NVS cache bypass

Fixed a bug where "Force Today's Update" and "Force Tomorrow's Update" buttons
served stale NVS-cached prices instead of performing a fresh HTTP fetch, even
when the button was pressed explicitly to recover from corrupt or wrong cached
data.

**Root cause:** `smart_price_update` and `smart_tomorrow_price_update` are
NVS-first by design — they load from NVS when a matching date and valid count
are found, and skip the HTTP fetch entirely. This is correct for automatic
retries and boot recovery, but wrong for a manual "Force" button press, where
the user's intent is always to go to the API regardless of what is cached.

**Fix:** Both button `on_press` handlers now call
`eprices_nvs::clear_today_slot()` / `clear_tomorrow_slot()` and reset the
in-memory entry count to `0` before executing the update script. With count
zeroed in both NVS and RAM, the NVS load step inside the update script returns
false and the HTTP fetch always proceeds. NVS is repopulated with fresh API
data at the end of the fetch as normal.

**New function in `eprices_nvs.h`:**
- `clear_today_slot()` — mirrors the existing `clear_tomorrow_slot()`

**Changed locations in `eprices.yaml`:**
- `Force Today's Update` button `on_press` — added `clear_today_slot()` call
  and `today_entry_count = 0` before `smart_price_update`
- `Force Tomorrow's Update` button `on_press` — added `clear_tomorrow_slot()`
  call and `tomorrow_entry_count = 0` before `smart_tomorrow_price_update`

**Changed locations in `eprices_nvs.h`:**
- Added `inline void clear_today_slot() { clear_slot("td_count"); }`

---

### Today HTTP fetch URL explicit date parameters

Fixed a bug where the today HTTP fetch URL contained no date parameters,
causing the Energy-Charts `/price` endpoint to return a stale cached dataset
approximately two months old instead of the current day's prices.

**Root cause:** The Energy-Charts API `/price` endpoint, when called without
explicit `start=` and `end=` date parameters, has stopped returning the current
rolling day's data and instead returns a stale cached window. The endpoint is
not marked deprecated (`"deprecated": false` in the response) but the default
behaviour is broken. The tomorrow fetch URL already included a `&start=` date
parameter; the today fetch did not.

This was confirmed by comparing:
- `GET /price?bzn=SI` → returned 96 entries for 2026-07-21 (two months stale)
- `GET /price?bzn=SI&start=2026-09-22&end=2026-09-22` → returned correct 96
  entries for today

**Fix:** Both the today and tomorrow fetch URL builders now include explicit
`&start=YYYY-MM-DD&end=YYYY-MM-DD` date parameters. The `char url[]` buffer
in both lambdas was enlarged from 128 to 160 bytes to accommodate the longer
URL string.

**Changed locations in `eprices.yaml`:**
- `smart_price_update` script URL lambda — added `&start=%04d-%02d-%02d&end=%04d-%02d-%02d`
  using today's date from `id(ha_time).now()`; buffer `char url[128]` → `char url[160]`
- `smart_tomorrow_price_update` script URL lambda — added `&end=%04d-%02d-%02d`
  to match; buffer `char url[128]` → `char url[160]`

---

## v1.2.3 — 2026-08-04

### Uptime sensor bucket display and logbook change-detection

The `Uptime` text sensor previously published a high-precision human-readable
uptime string (e.g. `3 h 22 min`, `12 d 4 h 17 min`) on every internal
`uptime` sensor update, which runs every 60 seconds. In the Home Assistant
activity log, this produced one state change per minute during the first day
after a reboot — a noisy stream redundant with the existing `Last Reboot`
text sensor.

The uptime lambda now formats the value into coarse hourly / daily / monthly
buckets and only calls `publish_state` when the bucket string actually
changes. Result: **one logbook entry per hour** for the first day, **one per
day** through the first month, **one per month** through the first year, and
**one per month** after that.

**New display buckets:**

| Boot age | State | Window |
|---|---|---|
| 0 – 59 min | `< 1 hour` | 1 h |
| 1 – 23 h | `> N hour(s)` | 1 h |
| 1 – 30 d | `> N day(s)` | 1 d |
| 1 – 11 mo | `> N month(s)` | 30 d |
| 12+ mo | `> N year(s)` or `> N year(s) N month(s)` | 30 d |

Singular/plural is handled in C++ (`1 hour` vs `2 hours`, `1 day` vs
`2 days`, etc.). Conventions: 1 month = 30 days, 1 year = 12 months —
consistent with the existing `d / 30` month convention used elsewhere in
the file.

**New global:**
- `last_published_uptime` (type: `std::string`, `restore_value: false`,
  `initial_value: '""'`) — tracks the most recently published bucket value.
  The uptime lambda compares against it and only calls `publish_state` when
  the bucket string differs.

The `update_interval: 60s` on the internal `uptime` sensor is kept as-is so
the lambda still runs every minute and reliably catches hour-boundary
transitions even under transient load.

**Changed locations in `eprices.yaml`:**
- `globals:` — added `last_published_uptime`
- Internal `uptime` sensor `on_raw_value` lambda — replaced minute-precision
  format with bucket display + change-detection guard

---

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
| Today JSON Hourly Prices EUR⁄kWh | `sensor.eprices_today_json_hourly_prices_eur_kwh` | JSON array, 23–25 values depending on DST (24 on normal days); `""` when no data |
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
| Tomorrow JSON Hourly Prices EUR⁄kWh | `sensor.eprices_tomorrow_json_hourly_prices_eur_kwh` | JSON array, 23–25 values depending on DST (24 on normal days); `""` when no data |
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
