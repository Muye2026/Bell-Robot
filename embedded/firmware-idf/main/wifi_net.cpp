#include "wifi_net.h"

#include <string.h>

#include "app_config.h"
#include "app_state.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/ip4_addr.h"

namespace bell_robot {

namespace {
constexpr char kTag[] = "bell_robot";
constexpr uint32_t kStaConnectTimeoutMs = 15000;
constexpr EventBits_t kWifiConnectedBit = BIT0;

EventGroupHandle_t wifiEventGroup = nullptr;
esp_netif_t *apNetif = nullptr;
esp_netif_t *staNetif = nullptr;
bool wifiStartedAsSta = false;
bool wifiStartedAsAp = false;

void wifiEventHandler(void *arg, esp_event_base_t eventBase, int32_t eventId, void *eventData) {
  (void)arg;
  if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_DISCONNECTED) {
    if (wifiEventGroup != nullptr) {
      xEventGroupClearBits(wifiEventGroup, kWifiConnectedBit);
    }
    if (wifiStartedAsSta) {
      esp_wifi_connect();
    }
  } else if (eventBase == IP_EVENT && eventId == IP_EVENT_STA_GOT_IP) {
    if (wifiEventGroup != nullptr) {
      xEventGroupSetBits(wifiEventGroup, kWifiConnectedBit);
    }
    const ip_event_got_ip_t *event = static_cast<ip_event_got_ip_t *>(eventData);
    ESP_LOGI(kTag, "STA got IP: " IPSTR, IP2STR(&event->ip_info.ip));
  }
}

void initWifiDriver() {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  wifiEventGroup = xEventGroupCreate();
  staNetif = esp_netif_create_default_wifi_sta();
  apNetif = esp_netif_create_default_wifi_ap();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                       ESP_EVENT_ANY_ID,
                                                       &wifiEventHandler,
                                                       nullptr,
                                                       nullptr));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                       IP_EVENT_STA_GOT_IP,
                                                       &wifiEventHandler,
                                                       nullptr,
                                                       nullptr));
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
}

void configureApIp() {
  if (apNetif == nullptr) {
    return;
  }
  esp_netif_ip_info_t ip = {};
  IP4_ADDR(&ip.ip, CAMERA_PREVIEW_AP_IP_0, CAMERA_PREVIEW_AP_IP_1, CAMERA_PREVIEW_AP_IP_2, CAMERA_PREVIEW_AP_IP_3);
  IP4_ADDR(&ip.gw, CAMERA_PREVIEW_AP_IP_0, CAMERA_PREVIEW_AP_IP_1, CAMERA_PREVIEW_AP_IP_2, CAMERA_PREVIEW_AP_IP_3);
  IP4_ADDR(&ip.netmask, 255, 255, 255, 0);
  ESP_ERROR_CHECK(esp_netif_dhcps_stop(apNetif));
  ESP_ERROR_CHECK(esp_netif_set_ip_info(apNetif, &ip));
  ESP_ERROR_CHECK(esp_netif_dhcps_start(apNetif));
}

void startWifiApOnly() {
  configureApIp();

  wifi_config_t wifiConfig = {};
  strncpy(reinterpret_cast<char *>(wifiConfig.ap.ssid), CAMERA_PREVIEW_AP_SSID, sizeof(wifiConfig.ap.ssid));
  strncpy(reinterpret_cast<char *>(wifiConfig.ap.password), CAMERA_PREVIEW_AP_PASSWORD, sizeof(wifiConfig.ap.password));
  wifiConfig.ap.ssid_len = strlen(CAMERA_PREVIEW_AP_SSID);
  wifiConfig.ap.channel = 1;
  wifiConfig.ap.max_connection = 4;
  wifiConfig.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
  if (strlen(CAMERA_PREVIEW_AP_PASSWORD) == 0) {
    wifiConfig.ap.authmode = WIFI_AUTH_OPEN;
  }

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifiConfig));
  ESP_ERROR_CHECK(esp_wifi_start());
  wifiStartedAsAp = true;
  wifiStartedAsSta = false;
  ESP_LOGI(kTag, "AP started: %s / http://192.168.4.1/", CAMERA_PREVIEW_AP_SSID);
}

bool startWifiStaOnly() {
  wifi_config_t wifiConfig = {};
  safeCopy(reinterpret_cast<char *>(wifiConfig.sta.ssid), sizeof(wifiConfig.sta.ssid), cloudSettings.ssid);
  safeCopy(reinterpret_cast<char *>(wifiConfig.sta.password), sizeof(wifiConfig.sta.password), cloudSettings.password);
  wifiConfig.sta.threshold.authmode = WIFI_AUTH_OPEN;
  wifiConfig.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

  if (wifiEventGroup != nullptr) {
    xEventGroupClearBits(wifiEventGroup, kWifiConnectedBit);
  }

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifiConfig));
  wifiStartedAsSta = true;
  wifiStartedAsAp = false;
  ESP_ERROR_CHECK(esp_wifi_start());

  const EventBits_t bits = xEventGroupWaitBits(wifiEventGroup,
                                               kWifiConnectedBit,
                                               pdFALSE,
                                               pdTRUE,
                                               pdMS_TO_TICKS(kStaConnectTimeoutMs));
  if ((bits & kWifiConnectedBit) != 0) {
    setCloudError("wifi_connected");
    return true;
  }

  ESP_LOGW(kTag, "STA connect timeout, fallback to AP");
  setCloudError("wifi_timeout");
  esp_wifi_stop();
  wifiStartedAsSta = false;
  return false;
}
} // namespace

void startWifi() {
  initWifiDriver();
  if (!ENABLE_CLOUD_REMOTE) {
    setCloudError("disabled");
    startWifiApOnly();
    return;
  }
  if (cloudSettingsComplete() && startWifiStaOnly()) {
    ESP_LOGI(kTag, "STA mode active, cloud server: %s", cloudSettings.serverUrl);
    return;
  }
  startWifiApOnly();
}

bool wifiModeIsSta() {
  return wifiStartedAsSta;
}

bool wifiIsConnected() {
  return wifiEventGroup != nullptr &&
         (xEventGroupGetBits(wifiEventGroup) & kWifiConnectedBit) != 0;
}

void restartWifiApFallback() {
  // startWifiApOnly 内部会把 wifiStartedAsSta 置回 false。
  startWifiApOnly();
}

} // namespace bell_robot
