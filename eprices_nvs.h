#pragma once
// EPrices NVS helper
// Namespace: "eprices"
// Keys: td_count, td_date, td_prices, td_ts
//       tm_count, tm_date, tm_prices, tm_ts

#include <vector>
#include <string>
#include <cstdint>
#include "esphome/core/log.h"
#include "nvs_flash.h"
#include "nvs.h"

namespace eprices_nvs {

static const char *TAG  = "eprices_nvs";
static const char *NS   = "eprices";

// ---- internal: ensure NVS is initialised once ----
inline bool ensure_init() {
  static bool done = false;
  if (done) return true;
  esp_err_t e = nvs_flash_init();
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    e = nvs_flash_init();
  }
  done = (e == ESP_OK);
  if (!done) ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(e));
  return done;
}

// ---- save ----
inline bool save(const char *count_key, const char *date_key,
                 const char *prices_key, const char *ts_key,
                 int count, const std::string &date,
                 const std::vector<float>   &prices,
                 const std::vector<int64_t> &timestamps) {
  if (!ensure_init()) return false;
  nvs_handle_t h;
  esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
  if (e != ESP_OK) { ESP_LOGW(TAG, "save open failed: %s", esp_err_to_name(e)); return false; }

  nvs_set_i32(h, count_key, (int32_t)count);
  nvs_set_blob(h, date_key, date.c_str(), date.size() + 1);
  if (count > 0) {
    nvs_set_blob(h, prices_key, prices.data(),     (size_t)count * sizeof(float));
    nvs_set_blob(h, ts_key,     timestamps.data(), (size_t)count * sizeof(int64_t));
  }
  nvs_commit(h);
  nvs_close(h);
  ESP_LOGI(TAG, "save(%s): %d entries for %s", count_key, count, date.c_str());
  return true;
}

// ---- load: returns false if missing, stale or corrupt ----
inline bool load(const char *count_key, const char *date_key,
                 const char *prices_key, const char *ts_key,
                 const std::string &expected_date,
                 std::vector<float>   &out_prices,
                 std::vector<int64_t> &out_timestamps,
                 int &out_count) {
  if (!ensure_init()) return false;
  nvs_handle_t h;
  esp_err_t e = nvs_open(NS, NVS_READONLY, &h);
  if (e != ESP_OK) { ESP_LOGW(TAG, "load open failed: %s", esp_err_to_name(e)); return false; }

  int32_t count = 0;
  e = nvs_get_i32(h, count_key, &count);
  if (e != ESP_OK || count <= 0 || count > 110) {
    ESP_LOGW(TAG, "load(%s): bad count %d", count_key, (int)count);
    nvs_close(h); return false;
  }

  char date_buf[12] = {};
  size_t date_len = sizeof(date_buf);
  nvs_get_blob(h, date_key, date_buf, &date_len);
  if (expected_date != std::string(date_buf)) {
    ESP_LOGW(TAG, "load(%s): stale %s != %s", count_key, date_buf, expected_date.c_str());
    nvs_close(h); return false;
  }

  std::vector<float>   pv((size_t)count);
  std::vector<int64_t> ts((size_t)count);
  size_t sp = (size_t)count * sizeof(float);
  size_t st = (size_t)count * sizeof(int64_t);
  nvs_get_blob(h, prices_key, pv.data(), &sp);
  nvs_get_blob(h, ts_key,     ts.data(), &st);
  nvs_close(h);

  out_prices     = pv;
  out_timestamps = ts;
  out_count      = (int)count;
  ESP_LOGI(TAG, "load(%s): %d entries for %s", count_key, (int)count, expected_date.c_str());
  return true;
}

// ---- clear one slot (set count=0) ----
inline void clear_slot(const char *count_key) {
  if (!ensure_init()) return;
  nvs_handle_t h;
  if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_i32(h, count_key, 0);
    nvs_commit(h);
    nvs_close(h);
  }
}

// ---- public wrappers ----
inline bool save_today(int count, const std::string &date,
                       const std::vector<float> &p, const std::vector<int64_t> &ts) {
  return save("td_count","td_date","td_prices","td_ts", count, date, p, ts);
}
inline bool save_tomorrow(int count, const std::string &date,
                          const std::vector<float> &p, const std::vector<int64_t> &ts) {
  return save("tm_count","tm_date","tm_prices","tm_ts", count, date, p, ts);
}
inline bool load_today(const std::string &expected_date,
                       std::vector<float> &p, std::vector<int64_t> &ts, int &cnt) {
  return load("td_count","td_date","td_prices","td_ts", expected_date, p, ts, cnt);
}
inline bool load_tomorrow(const std::string &expected_date,
                          std::vector<float> &p, std::vector<int64_t> &ts, int &cnt) {
  return load("tm_count","tm_date","tm_prices","tm_ts", expected_date, p, ts, cnt);
}
inline void clear_tomorrow_slot() { clear_slot("tm_count"); }
inline void clear_today_slot()    { clear_slot("td_count"); }

} // namespace eprices_nvs
