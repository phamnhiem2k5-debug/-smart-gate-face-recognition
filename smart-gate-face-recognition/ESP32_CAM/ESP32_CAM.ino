#include "config.h"
#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"

// ============================================================
// 1. CAU HINH WIFI
// ============================================================



// ============================================================
// 2. CAU HINH CHAN CAMERA (AI THINKER)
// ============================================================
#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27
#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22

httpd_handle_t stream_httpd = NULL;

#define PART_BOUNDARY "gc0p4Jq0M2Yt08jU534c0p"
static const char* STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART     =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ============================================================
// 3. STREAM HANDLER
// ============================================================
static esp_err_t stream_handler(httpd_req_t* req) {
  esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  while (true) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) { continue; }

    // Tra FB ngay sau khi copy de camera chup frame tiep
    size_t   jpg_len = fb->len;
    uint8_t* jpg_buf = (uint8_t*)malloc(jpg_len);
    if (!jpg_buf) { esp_camera_fb_return(fb); continue; }
    memcpy(jpg_buf, fb->buf, jpg_len);
    esp_camera_fb_return(fb);

    char part_buf[64];
    size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, jpg_len);
    res = httpd_resp_send_chunk(req, part_buf, hlen);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)jpg_buf, jpg_len);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    free(jpg_buf);

    if (res != ESP_OK) break;
  }
  return ESP_OK;
}

// ============================================================
// 4. KHOI DONG SERVER
// ============================================================
void startCameraServer() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port    = 81;
  cfg.ctrl_port      = 32769;
  cfg.stack_size     = 8192;

  httpd_uri_t stream_uri = {
    .uri = "/stream", .method = HTTP_GET,
    .handler = stream_handler, .user_ctx = NULL
  };
  if (httpd_start(&stream_httpd, &cfg) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
    Serial.println("[HTTP] Stream server port 81 OK");
  }
}

// ============================================================
// 5. SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 CAM KHOI DONG ===");

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // ===========================================================
  // TOI UU OV2640 DE DAT FPS CAO NHAT
  // ===========================================================
  config.frame_size   = FRAMESIZE_QVGA;  // 320x240 — du cho AI, nhe cho WiFi
  config.jpeg_quality = 15;              // Tang len 15 (tu 12): file nhe hon
                                         // 20% → ESP32 gui nhanh hon, FPS tang
  config.fb_count     = 2;              // Double buffer — camera chup lien tuc
  config.grab_mode    = CAMERA_GRAB_LATEST; // Luon lay frame moi nhat

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] Loi: 0x%x\n", err);
    return;
  }

  // Tinh chinh sensor OV2640 tang FPS
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_framesize(s, FRAMESIZE_QVGA);
    s->set_quality(s, 15);
    s->set_brightness(s, 1);
    s->set_contrast(s, 1);
    s->set_saturation(s, 0);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_agc_gain(s, 0);
    s->set_gainceiling(s, (gainceiling_t)2);
    s->set_bpc(s, 1);
    s->set_wpc(s, 1);
    s->set_raw_gma(s, 1);
    s->set_lenc(s, 1);

    // TAT tinh nang nang cao de tang FPS
    // (khong can thiet cho nhan dien khuon mat)
    s->set_hmirror(s, 0);
    s->set_vflip(s, 0);
    s->set_dcw(s, 1);        // Downsize enable — tang toc do xuat anh
    s->set_colorbar(s, 0);   // Tat color bar test
    Serial.println("[CAM] Sensor OV2640 toi uu OK");
  }

  // Ket noi WiFi
  WiFi.begin(ssid, password);
  WiFi.setSleep(false); // Tat WiFi sleep — giam latency
  Serial.print("[WiFi] Dang ket noi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\n[WiFi] IP: " + WiFi.localIP().toString());

  startCameraServer();

  Serial.println("\n[OK] San sang!");
  Serial.printf("  Stream: http://%s:81/stream\n\n", WiFi.localIP().toString().c_str());
}

// ============================================================
// 6. LOOP
// ============================================================
void loop() {
  // In RAM dinh ky de phat hien memory leak
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 30000) {
    Serial.printf("[MEM] Free heap: %u bytes\n", esp_get_free_heap_size());
    lastPrint = millis();
  }
  delay(10);
}
