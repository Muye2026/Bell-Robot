#include "ota_update.h"

#include <string.h>

#include "app_state.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

namespace bell_robot {

namespace {
constexpr char kTag[] = "bell_robot";
// 分区表里 ota_0 / ota_1 均为 5.5MB，限制上传体积留出余量。
constexpr size_t kMaxOtaImageBytes = 5UL * 1024UL * 1024UL;
constexpr size_t kOtaRecvBufferBytes = 2048;
} // namespace

esp_err_t sendOtaPage(httpd_req_t *req) {
  static constexpr char html[] =
      "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Bell Robot Firmware</title><style>body{margin:0;background:#111;color:#eee;font-family:sans-serif}"
      "main{max-width:640px;margin:20px auto;padding:0 14px}button,a{font-size:18px;padding:10px 14px;margin:6px 6px 6px 0;display:inline-block}"
      "a{color:#8cc8ff}.settings{margin:16px 0;padding:12px;border:1px solid #333;background:#181818}"
      "label{display:block;margin:10px 0 4px}input{box-sizing:border-box;width:100%;padding:10px;background:#222;color:#eee;border:1px solid #555}"
      ".hint{color:#aaa;font-size:14px}#status{min-height:24px;color:#9fdb9f;margin-top:12px}</style></head><body><main>"
      "<h2>Bell Robot Firmware</h2>"
      "<p class='hint'>Upload a firmware .bin built from firmware-idf (up to 5MB). "
      "The device writes it to the inactive OTA partition and reboots when verified. "
      "Timer settings and stored samples are kept.</p>"
      "<div class='settings'>"
      "<label for='bin'>Firmware file (.bin)</label><input id='bin' type='file' accept='.bin'>"
      "<button onclick='uploadFirmware()'>Upload and update</button><span id='status'></span></div>"
      "<p><a href='/'>Back</a></p>"
      "<script>"
      "async function uploadFirmware(){const input=document.getElementById('bin');const status=document.getElementById('status');"
      "const f=input.files[0];if(!f){status.textContent='Choose a file first';return;}"
      "if(f.size>5*1024*1024){status.textContent='File too large (max 5MB)';return;}"
      "const buf=await f.arrayBuffer();status.textContent='Uploading '+f.name+' ('+f.size+' bytes)...';"
      "const r=await fetch('/ota',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:buf});"
      "const t=await r.text();status.textContent=r.ok?(t+' — rebooting...'):('Update failed: '+t);}"
      "</script></main></body></html>";
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t handleOtaUpload(httpd_req_t *req) {
  if (req->content_len == 0 || req->content_len > kMaxOtaImageBytes) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid firmware size");
    return ESP_FAIL;
  }

  const esp_partition_t *updatePartition = esp_ota_get_next_update_partition(nullptr);
  if (updatePartition == nullptr) {
    ESP_LOGE(kTag, "no OTA update partition found");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no OTA partition");
    return ESP_FAIL;
  }
  ESP_LOGI(kTag, "OTA start: writing to %s (%lu bytes image)",
           updatePartition->label,
           static_cast<unsigned long>(req->content_len));

  esp_ota_handle_t otaHandle = 0;
  esp_err_t err = esp_ota_begin(updatePartition, OTA_WITH_SEQUENTIAL_WRITES, &otaHandle);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "esp_ota_begin failed: %s", esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota begin failed");
    return ESP_FAIL;
  }

  uint8_t buffer[kOtaRecvBufferBytes] = {};
  size_t remaining = req->content_len;
  bool aborted = false;
  while (remaining > 0) {
    const size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    const int received = httpd_req_recv(req, reinterpret_cast<char *>(buffer), chunk);
    if (received <= 0) {
      ESP_LOGE(kTag, "OTA upload interrupted: received=%d", received);
      aborted = true;
      break;
    }
    const size_t receivedSize = static_cast<size_t>(received);
    if (esp_ota_write(otaHandle, buffer, receivedSize) != ESP_OK) {
      ESP_LOGE(kTag, "esp_ota_write failed");
      aborted = true;
      break;
    }
    remaining -= receivedSize;
  }

  if (aborted) {
    esp_ota_abort(otaHandle);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota write failed");
    return ESP_FAIL;
  }

  err = esp_ota_end(otaHandle);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "esp_ota_end failed: %s", esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota verify failed");
    return ESP_FAIL;
  }

  err = esp_ota_set_boot_partition(updatePartition);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "set boot partition failed");
    return ESP_FAIL;
  }

  ESP_LOGI(kTag, "OTA complete, rebooting in 1.5s");
  requestReboot(1500);
  return httpd_resp_send(req, "update ok", HTTPD_RESP_USE_STRLEN);
}

} // namespace bell_robot
