#include "web_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "app_config.h"
#include "app_state.h"
#include "buzzer_player.h"
#include "display_ui.h"
#include "esp_camera.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "presence_detector.h"
#include "sample_store.h"
#include "sedentary_timer.h"
#include "wifi_net.h"

namespace bell_robot {

namespace {
constexpr char kTag[] = "bell_robot";
constexpr uint32_t kCameraStreamFrameDelayMs = 1;
constexpr uint32_t kCameraStreamLogFrames = 60;

httpd_handle_t httpServer = nullptr;

esp_err_t sendIndex(httpd_req_t *req) {
  static constexpr char html[] =
      "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Bell Robot Camera</title><style>body{margin:0;background:#111;color:#eee;font-family:sans-serif}"
      "main{max-width:760px;margin:20px auto;padding:0 14px}.preview{width:min(100%,360px);aspect-ratio:3/4;background:#222;overflow:hidden;display:flex;align-items:center;justify-content:center;margin:auto}"
      "#frame{width:133.333%;height:75%;display:block;transform:rotate(90deg)}"
      "button,a,input{font-size:18px}button,a{padding:10px 14px;margin:6px 6px 6px 0;display:inline-block}"
      "a{color:#8cc8ff}.settings{margin:16px 0;padding:12px;border:1px solid #333;background:#181818}"
      "label{display:block;margin:10px 0 4px}input{box-sizing:border-box;width:100%;padding:10px;background:#222;color:#eee;border:1px solid #555}"
      "#msg{min-height:24px;color:#9fdb9f}</style></head><body><main>"
      "<h2>Bell Robot Camera</h2><div class='preview'><img id='frame' src='/stream'></div>"
      "<p><button onclick='refreshFrame()'>Refresh</button><a href='/status'>Status</a><a href='/reset'>Reset</a><a href='/samples'>Samples</a></p>"
      "<form class='settings' onsubmit='saveSettings(event)'>"
      "<label for='sit'>倒计时（分钟）</label><input id='sit' name='sit_minutes' type='number' min='1' max='180' step='1' required>"
      "<label for='away'>离场容忍（分钟）</label><input id='away' name='away_minutes' type='number' min='1' max='5' step='1' required>"
      "<button type='submit'>保存设置</button><span id='msg'></span></form>"
      "<p><a href='/label?class=absent'>Save absent sample</a><a href='/label?class=seated'>Save seated sample</a></p>"
      "<script>function refreshFrame(){document.getElementById('frame').src='/stream?ts='+Date.now()}"
      "async function loadSettings(){let r=await fetch('/settings');let s=await r.json();document.getElementById('sit').value=s.sit_minutes;document.getElementById('away').value=s.away_minutes}"
      "async function saveSettings(e){e.preventDefault();let sit=document.getElementById('sit'),away=document.getElementById('away'),msg=document.getElementById('msg');msg.textContent='Saving...';"
      "let b='sit_minutes='+encodeURIComponent(sit.value)+'&away_minutes='+encodeURIComponent(away.value);"
      "let r=await fetch('/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b});"
      "msg.textContent=r.ok?'Saved':'Save failed'}"
      "loadSettings()</script></main></body></html>";
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t sendIndexCloud(httpd_req_t *req) {
  static constexpr char html[] =
      "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Bell Robot Camera</title><style>body{margin:0;background:#111;color:#eee;font-family:sans-serif}"
      "main{max-width:760px;margin:20px auto;padding:0 14px}.preview{width:min(100%,360px);aspect-ratio:3/4;background:#222;overflow:hidden;display:flex;align-items:center;justify-content:center;margin:auto}"
      "#frame{width:133.333%;height:75%;display:block;transform:rotate(90deg)}"
      "button,a,input{font-size:18px}button,a{padding:10px 14px;margin:6px 6px 6px 0;display:inline-block}"
      "a{color:#8cc8ff}.settings{margin:16px 0;padding:12px;border:1px solid #333;background:#181818}"
      "label{display:block;margin:10px 0 4px}input{box-sizing:border-box;width:100%;padding:10px;background:#222;color:#eee;border:1px solid #555}"
      ".hint{color:#aaa;font-size:14px}#msg,#cloudMsg{min-height:24px;color:#9fdb9f}</style></head><body><main>"
      "<h2>Bell Robot Camera</h2><div class='preview'><img id='frame' src='/stream'></div>"
      "<p><button onclick='refreshFrame()'>Refresh</button><a href='/status'>Status</a><a href='/reset'>Reset</a><a href='/samples'>Samples</a></p>"
      "<form class='settings' onsubmit='saveSettings(event)'>"
      "<label for='sit'>Timer minutes</label><input id='sit' name='sit_minutes' type='number' min='1' max='180' step='1' required>"
      "<label for='away'>Away tolerance minutes</label><input id='away' name='away_minutes' type='number' min='1' max='5' step='1' required>"
      "<button type='submit'>Save timer</button><span id='msg'></span></form>"
      "<form class='settings' onsubmit='saveCloud(event)'>"
      "<h3>Cloud remote access</h3><p class='hint'>Only Wi-Fi is required. Device ID and token are generated automatically.</p>"
      "<label for='ssid'>2.4G Wi-Fi SSID</label><input id='ssid' name='ssid' maxlength='32' required>"
      "<label for='pass'>Wi-Fi password</label><input id='pass' name='password' type='password' maxlength='64'>"
      "<label for='server'>Server URL</label><input id='server' name='server_url' placeholder='https://your-domain.example' maxlength='159' required>"
      "<p class='hint'>Device ID: <span id='device'>-</span></p>"
      "<button type='submit'>Save cloud</button><button type='button' onclick='forgetCloud()'>Forget cloud</button><span id='cloudMsg'></span></form>"
      "<p><a href='/label?class=absent'>Save absent sample</a><a href='/label?class=seated'>Save seated sample</a></p>"
      "<script>function refreshFrame(){document.getElementById('frame').src='/stream?ts='+Date.now()}"
      "async function loadSettings(){let r=await fetch('/settings');let s=await r.json();document.getElementById('sit').value=s.sit_minutes;document.getElementById('away').value=s.away_minutes}"
      "async function loadCloud(){let r=await fetch('/cloud');let s=await r.json();document.getElementById('ssid').value=s.ssid||'';document.getElementById('server').value=s.server_url||'';document.getElementById('device').textContent=s.device_id||'-'}"
      "async function saveSettings(e){e.preventDefault();let sit=document.getElementById('sit'),away=document.getElementById('away'),msg=document.getElementById('msg');msg.textContent='Saving...';"
      "let b='sit_minutes='+encodeURIComponent(sit.value)+'&away_minutes='+encodeURIComponent(away.value);"
      "let r=await fetch('/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b});"
      "msg.textContent=r.ok?'Saved':'Save failed'}"
      "async function saveCloud(e){e.preventDefault();let ids=['ssid','pass','server'],p=new URLSearchParams(),msg=document.getElementById('cloudMsg');ids.forEach(id=>p.append(document.getElementById(id).name,document.getElementById(id).value));msg.textContent='Saving...';let r=await fetch('/cloud',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()});let t=await r.text();msg.textContent=r.ok?'Saved, rebooting...':('Save failed: '+t)}"
      "async function forgetCloud(){let msg=document.getElementById('cloudMsg');msg.textContent='Clearing...';let r=await fetch('/cloud/forget',{method:'POST'});let t=await r.text();msg.textContent=r.ok?'Cleared, rebooting...':('Clear failed: '+t)}"
      "loadSettings();loadCloud()</script></main></body></html>";
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t sendSamplesPage(httpd_req_t *req) {
  static constexpr char html[] =
      "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Bell Robot Samples</title><style>body{margin:0;background:#111;color:#eee;font-family:sans-serif}"
      "main{max-width:980px;margin:20px auto;padding:0 14px}button,a{font-size:16px;padding:10px 14px;margin:6px 6px 6px 0;display:inline-block}"
      "a{color:#8cc8ff}.toolbar{margin-bottom:12px}.hint{color:#aaa}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:14px}"
      ".card{border:1px solid #333;background:#181818;padding:12px}.card img{width:100%;height:auto;background:#222;display:block}"
      ".meta{font-size:13px;line-height:1.5;color:#cfcfcf;word-break:break-word}.status{min-height:24px;color:#9fdb9f}</style></head><body><main>"
      "<h2>Bell Robot Samples</h2><p class='hint'>Second button and the debug capture endpoint both save JPEG + metadata into device storage.</p>"
      "<div class='toolbar'><a href='/'>Back</a><button onclick='captureSample()'>Capture Now</button><button onclick='clearSamples()'>Clear All</button><span id='status' class='status'></span></div>"
      "<div id='summary' class='hint'></div><div id='grid' class='grid'></div>"
      "<script>"
      "async function loadSamples(){const status=document.getElementById('status');const summary=document.getElementById('summary');const grid=document.getElementById('grid');status.textContent='Loading...';"
      "const r=await fetch('/samples/list');if(!r.ok){status.textContent='Load failed';return;}const data=await r.json();summary.textContent='Stored samples: '+data.count;grid.innerHTML='';"
      "if(!data.items||data.items.length===0){grid.innerHTML='<div class=\"hint\">No samples yet.</div>';status.textContent='';return;}"
      "for(const item of data.items){const card=document.createElement('div');card.className='card';"
      "card.innerHTML='<img alt=\"sample\" src=\"'+item.preview_url+'\">'+"
      "'<p><strong>'+item.id+'</strong></p>'+"
      "'<div class=\"meta\">state: '+item.state+'<br>present: '+item.present+'<br>model_prob: '+item.model_prob+'<br>wifi_mode: '+item.wifi_mode+'<br>boot_ms: '+item.boot_ms+'<br>jpeg_bytes: '+item.jpeg_bytes+'</div>'+"
      "'<p><a href=\"'+item.download_url+'\" download>Download JPG</a><a href=\"'+item.meta_url+'\" target=\"_blank\">Metadata</a></p>';"
      "grid.appendChild(card);}status.textContent='';}"
      "async function captureSample(){const status=document.getElementById('status');status.textContent='Capturing...';const r=await fetch('/samples/capture',{method:'POST'});"
      "if(!r.ok){status.textContent='Capture failed';return;}const data=await r.json();status.textContent='Saved '+(data.id||'sample');loadSamples();}"
      "async function clearSamples(){const status=document.getElementById('status');if(!confirm('Clear all stored samples?')){return;}status.textContent='Clearing...';"
      "const r=await fetch('/samples/clear',{method:'POST'});status.textContent=r.ok?'Cleared':'Clear failed';if(r.ok){loadSamples();}}"
      "loadSamples();</script></main></body></html>";
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t sendCapture(httpd_req_t *req) {
  CapturedCameraFrame capture = {};
  uint8_t *jpgBuffer = nullptr;
  size_t jpgLength = 0;
  bool converted = false;
  if (!captureFrameAsJpeg(&capture, &jpgBuffer, &jpgLength, &converted)) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera capture failed");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  const esp_err_t err = httpd_resp_send(req, reinterpret_cast<const char *>(jpgBuffer), jpgLength);
  releaseCapturedJpeg(&capture, jpgBuffer, converted);
  return err;
}

esp_err_t sendStream(httpd_req_t *req) {
  static constexpr char boundary[] = "bellrobot";
  char frameHeader[96] = {};

  ESP_LOGI(kTag, "camera stream connected");
  httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=bellrobot");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Connection", "close");

  uint32_t logFrameCount = 0;
  uint32_t logStartMs = millis32();
  while (true) {
    CapturedCameraFrame capture = {};
    if (!lockCamera(&capture)) {
      ESP_LOGW(kTag, "camera stream lock timeout");
      return ESP_FAIL;
    }
    if (!setCameraFrameSize(kCameraFrameSize)) {
      releaseCameraFrame(&capture);
      return ESP_FAIL;
    }

    camera_fb_t *frame = esp_camera_fb_get();
    if (frame == nullptr) {
      ESP_LOGW(kTag, "camera stream capture failed");
      releaseCameraFrame(&capture);
      return ESP_FAIL;
    }
    capture.rawFrame = frame;

    if (frame->format != PIXFORMAT_JPEG) {
      ESP_LOGW(kTag, "camera stream expected JPEG frame, got format=%d", static_cast<int>(frame->format));
      releaseCameraFrame(&capture);
      return ESP_FAIL;
    }
    const size_t frameLength = frame->len;
    const uint16_t frameWidth = frame->width;
    const uint16_t frameHeight = frame->height;

    const int headerLength = snprintf(frameHeader,
                                      sizeof(frameHeader),
                                      "\r\n--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                                      boundary,
                                      static_cast<unsigned>(frameLength));
    esp_err_t err = headerLength > 0 && static_cast<size_t>(headerLength) < sizeof(frameHeader)
                        ? httpd_resp_send_chunk(req, frameHeader, headerLength)
                        : ESP_FAIL;
    if (err == ESP_OK) {
      err = httpd_resp_send_chunk(req, reinterpret_cast<const char *>(frame->buf), frameLength);
    }

    releaseCameraFrame(&capture);
    if (err != ESP_OK) {
      ESP_LOGI(kTag, "camera stream disconnected: %s", esp_err_to_name(err));
      return ESP_OK;
    }

    logFrameCount++;
    if (logFrameCount >= kCameraStreamLogFrames) {
      const uint32_t nowMs = millis32();
      const uint32_t elapsedMs = nowMs - logStartMs;
      const float fps = elapsedMs == 0 ? 0.0f : (static_cast<float>(logFrameCount) * 1000.0f) / elapsedMs;
      ESP_LOGI(kTag,
               "camera stream fps=%.1f jpeg=%u bytes frame=%ux%u",
               static_cast<double>(fps),
               static_cast<unsigned>(frameLength),
               static_cast<unsigned>(frameWidth),
               static_cast<unsigned>(frameHeight));
      logFrameCount = 0;
      logStartMs = nowMs;
    }

    vTaskDelay(pdMS_TO_TICKS(kCameraStreamFrameDelayMs));
  }
}

esp_err_t sendSettings(httpd_req_t *req) {
  char payload[96] = {};
  snprintf(payload,
           sizeof(payload),
           "{\"sit_minutes\":%lu,\"away_minutes\":%lu}",
           static_cast<unsigned long>(minutesFromMs(sedentaryTimer.sitTargetMs())),
           static_cast<unsigned long>(minutesFromMs(sedentaryTimer.awayResetMs())));
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
}

bool parseUnsignedStrict(const char *value, uint32_t *out) {
  if (value == nullptr || *value == '\0' || out == nullptr) {
    return false;
  }
  char *end = nullptr;
  const unsigned long parsed = strtoul(value, &end, 10);
  if (end == value || *end != '\0' || parsed > UINT32_MAX) {
    return false;
  }
  *out = static_cast<uint32_t>(parsed);
  return true;
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

void formUrlDecodeInPlace(char *value) {
  if (value == nullptr) {
    return;
  }
  char *read = value;
  char *write = value;
  while (*read != '\0') {
    if (*read == '+') {
      *write++ = ' ';
      read++;
      continue;
    }
    if (*read == '%' && read[1] != '\0' && read[2] != '\0') {
      const int hi = hexValue(read[1]);
      const int lo = hexValue(read[2]);
      if (hi >= 0 && lo >= 0) {
        *write++ = static_cast<char>((hi << 4) | lo);
        read += 3;
        continue;
      }
    }
    *write++ = *read++;
  }
  *write = '\0';
}

void trimInPlace(char *value) {
  if (value == nullptr) {
    return;
  }
  char *start = value;
  while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
    start++;
  }
  char *end = start + strlen(start);
  while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
    end--;
  }
  const size_t length = static_cast<size_t>(end - start);
  memmove(value, start, length);
  value[length] = '\0';
}

bool readFormValue(const char *body, const char *key, char *out, size_t outSize) {
  if (body == nullptr || key == nullptr || out == nullptr || outSize == 0) {
    return false;
  }
  out[0] = '\0';
  const size_t keyLength = strlen(key);
  const char *cursor = body;
  while (*cursor != '\0') {
    const char *pairEnd = strchr(cursor, '&');
    const size_t pairLength = pairEnd == nullptr ? strlen(cursor) : static_cast<size_t>(pairEnd - cursor);
    const char *equals = static_cast<const char *>(memchr(cursor, '=', pairLength));
    if (equals != nullptr && static_cast<size_t>(equals - cursor) == keyLength &&
        strncmp(cursor, key, keyLength) == 0) {
      const char *valueStart = equals + 1;
      const size_t encodedLength = pairLength - keyLength - 1;
      const size_t copyLength = std::min(encodedLength, outSize - 1);
      memcpy(out, valueStart, copyLength);
      out[copyLength] = '\0';
      formUrlDecodeInPlace(out);
      trimInPlace(out);
      return true;
    }
    if (pairEnd == nullptr) {
      break;
    }
    cursor = pairEnd + 1;
  }
  return false;
}

esp_err_t readPostBody(httpd_req_t *req, char *buffer, size_t bufferSize) {
  if (req->content_len == 0 || req->content_len >= bufferSize) {
    return ESP_FAIL;
  }

  size_t received = 0;
  while (received < req->content_len) {
    const int read = httpd_req_recv(req,
                                    buffer + received,
                                    req->content_len - received);
    if (read <= 0) {
      return ESP_FAIL;
    }
    received += read;
  }
  buffer[received] = '\0';
  return ESP_OK;
}

esp_err_t handleSettingsPost(httpd_req_t *req) {
  char body[96] = {};
  if (readPostBody(req, body, sizeof(body)) != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid settings body");
    return ESP_FAIL;
  }

  char sitValue[16] = {};
  char awayValue[16] = {};
  if (httpd_query_key_value(body, "sit_minutes", sitValue, sizeof(sitValue)) != ESP_OK ||
      httpd_query_key_value(body, "away_minutes", awayValue, sizeof(awayValue)) != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing timer settings");
    return ESP_FAIL;
  }

  uint32_t sitMinutes = 0;
  uint32_t awayMinutes = 0;
  if (!parseUnsignedStrict(sitValue, &sitMinutes) ||
      !parseUnsignedStrict(awayValue, &awayMinutes) ||
      !validTimerMinutes(sitMinutes, awayMinutes)) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "settings out of range");
    return ESP_FAIL;
  }

  const esp_err_t err = saveTimerSettings(sitMinutes, awayMinutes);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "save settings failed: %s", esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save settings failed");
    return ESP_FAIL;
  }
  return sendSettings(req);
}

esp_err_t sendCloudSettings(httpd_req_t *req) {
  char payload[384] = {};
  snprintf(payload,
           sizeof(payload),
           "{\"ssid\":\"%s\",\"server_url\":\"%s\",\"device_id\":\"%s\","
           "\"token_set\":%s,\"configured\":%s,\"wifi_mode\":\"%s\"}",
           cloudSettings.ssid,
           cloudSettings.serverUrl,
           cloudSettings.deviceId,
           cloudSettings.token[0] == '\0' ? "false" : "true",
           cloudSettingsComplete() ? "true" : "false",
           wifiModeIsSta() ? "sta" : "ap");
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
}

esp_err_t handleCloudPost(httpd_req_t *req) {
  char body[640] = {};
  if (readPostBody(req, body, sizeof(body)) != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid cloud body");
    return ESP_FAIL;
  }

  CloudSettings next = cloudSettings;
  char value[180] = {};
  if (readFormValue(body, "ssid", value, sizeof(value))) {
    safeCopy(next.ssid, sizeof(next.ssid), value);
  }
  if (readFormValue(body, "password", value, sizeof(value))) {
    safeCopy(next.password, sizeof(next.password), value);
  }
  if (readFormValue(body, "server_url", value, sizeof(value))) {
    safeCopy(next.serverUrl, sizeof(next.serverUrl), value);
  }

  const esp_err_t err = saveCloudSettings(next);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "save cloud settings failed: %s", esp_err_to_name(err));
    if (next.ssid[0] == '\0') {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ssid");
    } else if (next.serverUrl[0] == '\0') {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing server url");
    } else if (!validServerUrl(next.serverUrl)) {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "server url must start with http:// or https://");
    } else if (next.deviceId[0] == '\0') {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing device id");
    } else if (next.token[0] == '\0') {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing device token");
    } else {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid cloud settings");
    }
    return ESP_FAIL;
  }
  requestReboot(2000);
  return sendCloudSettings(req);
}

esp_err_t handleCloudForget(httpd_req_t *req) {
  const esp_err_t err = forgetCloudSettings();
  if (err != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "forget cloud failed");
    return ESP_FAIL;
  }
  requestReboot(2000);
  return httpd_resp_send(req, "forget ok", HTTPD_RESP_USE_STRLEN);
}

esp_err_t sendStatus(httpd_req_t *req) {
  char payload[1024] = {};
  buildStatusPayload(payload, sizeof(payload));
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
}

esp_err_t handleReset(httpd_req_t *req) {
  resetTimer();
  presenceDetector.recalibrate();
  return httpd_resp_send(req, "reset ok", HTTPD_RESP_USE_STRLEN);
}

const char *queryClass(httpd_req_t *req, char *buffer, size_t bufferSize) {
  if (httpd_req_get_url_query_str(req, buffer, bufferSize) != ESP_OK) {
    return nullptr;
  }
  static char classValue[16] = {};
  if (httpd_query_key_value(buffer, "class", classValue, sizeof(classValue)) != ESP_OK) {
    return nullptr;
  }
  if (strcmp(classValue, "empty") == 0) {
    strcpy(classValue, "absent");
  } else if (strcmp(classValue, "occupied") == 0) {
    strcpy(classValue, "seated");
  }
  if (strcmp(classValue, "absent") != 0 && strcmp(classValue, "seated") != 0) {
    return nullptr;
  }
  return classValue;
}

esp_err_t handleLabel(httpd_req_t *req) {
  char query[64] = {};
  const char *label = queryClass(req, query, sizeof(query));
  if (label == nullptr) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "use /label?class=absent or /label?class=seated");
    return ESP_FAIL;
  }

  CapturedCameraFrame capture = {};
  if (!captureCameraFrame(&capture)) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera capture failed");
    return ESP_FAIL;
  }
  const camera_fb_t *frame = &capture.logicalFrame;

  int8_t features[kFeatureCount] = {};
  presenceDetector.exportNormalizedFeatures(frame, features, kFeatureCount);

  char filename[80] = {};
  snprintf(filename, sizeof(filename), "attachment; filename=\"%s_%lu.pgm\"",
           label,
           static_cast<unsigned long>(++sampleCounter));

  char header[64] = {};
  const int headerLength = snprintf(header, sizeof(header), "P5\n8 8\n255\n");
  uint8_t pixels[kFeatureCount] = {};
  for (size_t i = 0; i < kFeatureCount; ++i) {
    pixels[i] = static_cast<uint8_t>(std::max(0, std::min(255, static_cast<int>(features[i]) + 128)));
  }

  httpd_resp_set_type(req, "application/octet-stream");
  httpd_resp_set_hdr(req, "Content-Disposition", filename);
  httpd_resp_send_chunk(req, header, headerLength);
  httpd_resp_send_chunk(req, reinterpret_cast<const char *>(pixels), sizeof(pixels));
  httpd_resp_send_chunk(req, nullptr, 0);
  releaseCameraFrame(&capture);
  return ESP_OK;
}

const char *querySampleId(httpd_req_t *req, char *buffer, size_t bufferSize) {
  if (httpd_req_get_url_query_str(req, buffer, bufferSize) != ESP_OK) {
    return nullptr;
  }
  static char idValue[64] = {};
  if (httpd_query_key_value(buffer, "id", idValue, sizeof(idValue)) != ESP_OK) {
    return nullptr;
  }
  for (char *cursor = idValue; *cursor != '\0'; ++cursor) {
    const char c = *cursor;
    const bool valid = (c >= 'a' && c <= 'z') ||
                       (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') ||
                       c == '_' ||
                       c == '-';
    if (!valid) {
      return nullptr;
    }
  }
  return idValue;
}

esp_err_t sendSamplesList(httpd_req_t *req) {
  std::vector<std::string> ids;
  const esp_err_t listErr = sample_store::listIds(&ids);
  if (listErr != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sample list failed");
    return ESP_FAIL;
  }

  std::string payload;
  payload.reserve(16384);
  payload += "{\"count\":";
  payload += std::to_string(ids.size());
  payload += ",\"items\":[";

  bool first = true;
  for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
    std::string metadata;
    if (sample_store::loadMetadata(it->c_str(), &metadata) != ESP_OK) {
      continue;
    }

    char state[16] = {};
    char wifiMode[8] = {};
    bool present = false;
    uint32_t bootMs = 0;
    uint32_t jpegBytes = 0;
    float modelProb = 0.0f;
    jsonFindString(metadata.c_str(), "state", state, sizeof(state));
    jsonFindString(metadata.c_str(), "wifi_mode", wifiMode, sizeof(wifiMode));
    jsonFindBool(metadata.c_str(), "present", &present);
    jsonFindUint(metadata.c_str(), "boot_ms", &bootMs);
    jsonFindUint(metadata.c_str(), "jpeg_bytes", &jpegBytes);
    jsonFindFloat(metadata.c_str(), "model_prob", &modelProb);

    char item[512] = {};
    snprintf(item,
             sizeof(item),
             "%s{\"id\":\"%s\",\"state\":\"%s\",\"present\":%s,"
             "\"model_prob\":%.3f,\"wifi_mode\":\"%s\",\"boot_ms\":%lu,"
             "\"jpeg_bytes\":%lu,\"preview_url\":\"/samples/file?id=%s\","
             "\"download_url\":\"/samples/file?id=%s\",\"meta_url\":\"/samples/meta?id=%s\"}",
             first ? "" : ",",
             it->c_str(),
             state[0] == '\0' ? "-" : state,
             present ? "true" : "false",
             static_cast<double>(modelProb),
             wifiMode[0] == '\0' ? "-" : wifiMode,
             static_cast<unsigned long>(bootMs),
             static_cast<unsigned long>(jpegBytes),
             it->c_str(),
             it->c_str(),
             it->c_str());
    payload += item;
    first = false;
  }
  payload += "]}";

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, payload.c_str(), payload.size());
}

esp_err_t sendSampleFile(httpd_req_t *req) {
  char query[96] = {};
  const char *id = querySampleId(req, query, sizeof(query));
  if (id == nullptr) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing id");
    return ESP_FAIL;
  }

  std::vector<uint8_t> jpegData;
  const esp_err_t err = sample_store::loadJpeg(id, &jpegData);
  if (err == ESP_ERR_NOT_FOUND) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "sample not found");
    return ESP_FAIL;
  }
  if (err != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sample read failed");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req,
                         reinterpret_cast<const char *>(jpegData.data()),
                         jpegData.size());
}

esp_err_t sendSampleMetadata(httpd_req_t *req) {
  char query[96] = {};
  const char *id = querySampleId(req, query, sizeof(query));
  if (id == nullptr) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing id");
    return ESP_FAIL;
  }

  std::string metadata;
  const esp_err_t err = sample_store::loadMetadata(id, &metadata);
  if (err == ESP_ERR_NOT_FOUND) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "sample not found");
    return ESP_FAIL;
  }
  if (err != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "metadata read failed");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, metadata.c_str(), metadata.size());
}

esp_err_t handleSamplesClear(httpd_req_t *req) {
  const esp_err_t err = sample_store::clear();
  if (err != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sample clear failed");
    return ESP_FAIL;
  }
  return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

esp_err_t handleSampleCapture(httpd_req_t *req) {
  const uint32_t nowMs = millis32();
  char sampleId[48] = {};
  if (!captureAndStoreSample("button2", nowMs, sampleId, sizeof(sampleId))) {
    buzzerPlayer.startSequence(nowMs, 2, kCaptureBeepOnMs, kCaptureBeepOffMs);
    showDisplayOverlay("SAVE ERR", nowMs, kTransientDisplayMs);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "capture failed");
    return ESP_FAIL;
  }

  buzzerPlayer.startSequence(nowMs, 1, kCaptureBeepOnMs, kCaptureBeepOffMs);
  showDisplayOverlay("CAPTURED", nowMs, kTransientDisplayMs);

  char payload[96] = {};
  snprintf(payload, sizeof(payload), "{\"ok\":true,\"id\":\"%s\"}", sampleId);
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
}
} // namespace

void startWebServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 8192;
  config.max_uri_handlers = 20;
  ESP_ERROR_CHECK(httpd_start(&httpServer, &config));

  httpd_uri_t index = {};
  index.uri = "/";
  index.method = HTTP_GET;
  index.handler = ENABLE_CLOUD_REMOTE ? sendIndexCloud : sendIndex;

  httpd_uri_t capture = {};
  capture.uri = "/capture";
  capture.method = HTTP_GET;
  capture.handler = sendCapture;

  httpd_uri_t stream = {};
  stream.uri = "/stream";
  stream.method = HTTP_GET;
  stream.handler = sendStream;

  httpd_uri_t status = {};
  status.uri = "/status";
  status.method = HTTP_GET;
  status.handler = sendStatus;

  httpd_uri_t reset = {};
  reset.uri = "/reset";
  reset.method = HTTP_GET;
  reset.handler = handleReset;

  httpd_uri_t settingsGet = {};
  settingsGet.uri = "/settings";
  settingsGet.method = HTTP_GET;
  settingsGet.handler = sendSettings;

  httpd_uri_t settingsPost = {};
  settingsPost.uri = "/settings";
  settingsPost.method = HTTP_POST;
  settingsPost.handler = handleSettingsPost;

  httpd_uri_t label = {};
  label.uri = "/label";
  label.method = HTTP_GET;
  label.handler = handleLabel;

  httpd_uri_t samplesPage = {};
  samplesPage.uri = "/samples";
  samplesPage.method = HTTP_GET;
  samplesPage.handler = sendSamplesPage;

  httpd_uri_t samplesList = {};
  samplesList.uri = "/samples/list";
  samplesList.method = HTTP_GET;
  samplesList.handler = sendSamplesList;

  httpd_uri_t sampleFile = {};
  sampleFile.uri = "/samples/file";
  sampleFile.method = HTTP_GET;
  sampleFile.handler = sendSampleFile;

  httpd_uri_t sampleMeta = {};
  sampleMeta.uri = "/samples/meta";
  sampleMeta.method = HTTP_GET;
  sampleMeta.handler = sendSampleMetadata;

  httpd_uri_t samplesClear = {};
  samplesClear.uri = "/samples/clear";
  samplesClear.method = HTTP_POST;
  samplesClear.handler = handleSamplesClear;

  httpd_uri_t samplesCapture = {};
  samplesCapture.uri = "/samples/capture";
  samplesCapture.method = HTTP_POST;
  samplesCapture.handler = handleSampleCapture;

  httpd_uri_t cloudGet = {};
  cloudGet.uri = "/cloud";
  cloudGet.method = HTTP_GET;
  cloudGet.handler = sendCloudSettings;

  httpd_uri_t cloudPost = {};
  cloudPost.uri = "/cloud";
  cloudPost.method = HTTP_POST;
  cloudPost.handler = handleCloudPost;

  httpd_uri_t cloudForget = {};
  cloudForget.uri = "/cloud/forget";
  cloudForget.method = HTTP_POST;
  cloudForget.handler = handleCloudForget;

  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &index));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &capture));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &stream));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &status));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &reset));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &settingsGet));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &settingsPost));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &label));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &samplesPage));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &samplesList));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &sampleFile));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &sampleMeta));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &samplesClear));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &samplesCapture));
  if (ENABLE_CLOUD_REMOTE) {
    ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &cloudGet));
    ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &cloudPost));
    ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &cloudForget));
  }
}

} // namespace bell_robot
