#pragma once

#include "esp_http_server.h"

// AP 网页 OTA 固件升级。
//
// - GET  /ota：上传页面（选 .bin 文件后由 JS 以 application/octet-stream 直传）。
// - POST /ota：接收固件字节流，写入下一个 OTA 分区（esp_ota_*），
//   校验通过后切换启动分区并延时重启。
//
// 分区表使用 ota_0/ota_1 双分区（见 partitions.csv），升级时不动当前运行分区。

namespace bell_robot {

esp_err_t sendOtaPage(httpd_req_t *req);
esp_err_t handleOtaUpload(httpd_req_t *req);

} // namespace bell_robot
