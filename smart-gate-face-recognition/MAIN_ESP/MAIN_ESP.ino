#include "config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>

// [FIX BROWNOUT] Tắt brownout detector để tránh ESP32 restart khi servo hút dòng
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"




#define PIN_SERVO    12
#define PIN_LED      26
#define PIN_BUZZER   27
#define PIN_IR       13      // Cảm biến hồng ngoại vật cản
#define SERVO_DONG   90
#define SERVO_MO     0

// Tự đóng cổng sau 10 giây
#define GATE_AUTO_CLOSE_MS 10000




Servo myServo;
LiquidCrystal_I2C lcd(0x27, 16, 2);
WebServer server(80);

bool gateOpen = false;
int wrongCount = 0;
bool alarmActive = false;
bool lcdLocked = false;
unsigned long lcdLockTime = 0;
unsigned long gateOpenTime = 0;
unsigned long aiBlockTime = 0;
unsigned long lastFirebaseCheck = 0;

// Biến cho alarm state machine
unsigned long alarmStartTime = 0;
unsigned long alarmToggleTime = 0;
bool alarmLedState = false;

// Biến chống lặp cảm biến IR
bool irTriggered = false;

String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "unknown";
  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buf);
}

void writeToFirebase(String hanhDong, String nguoiThucHien) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = String(FIREBASE_URL) + "/smart_gate/trang_thai_thuc.json?auth=" + FIREBASE_SECRET;
  String payload = "{\"hanh_dong\":\"" + hanhDong + "\","
                   "\"nguoi_thuc_hien\":\"" + nguoiThucHien + "\","
                   "\"thoi_gian\":\"" + getTimestamp() + "\"}";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.sendRequest("PATCH", payload);
  http.end();
}

void clearFirebaseCommand() {
  HTTPClient http;
  String url = String(FIREBASE_URL) + "/smart_gate/lenh_dieu_khien.json?auth=" + FIREBASE_SECRET;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.sendRequest("PATCH", "{\"lenh\":\"none\"}");
  http.end();
}

void lcdShow(String line1, String line2) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(line1);
  lcd.setCursor(0, 1); lcd.print(line2);
}

void lcdDefault() {
  lcdShow("Nhom 11         ", "Cua Thong Minh  ");
}

void openGate(String nguoiThucHien) {
  myServo.write(SERVO_MO);
  gateOpen = true;
  gateOpenTime = millis();

  lcdLocked = true;
  lcdLockTime = millis();
  lcdShow("Cua dang mo...  ", "Xin moi vao! ra!    ");

  writeToFirebase("mo_cong", nguoiThucHien);
  Serial.println("[GATE] Mo cong boi: " + nguoiThucHien);
}

void closeGate(String nguoiThucHien) {
  myServo.write(SERVO_DONG);
  gateOpen = false;
  lcdLocked = false;
  lcdDefault();

  writeToFirebase("dong_cong", nguoiThucHien);
  Serial.println("[GATE] Dong cong boi: " + nguoiThucHien);
}

void triggerAlarm() {
  alarmActive = true;
  alarmStartTime = millis();
  alarmToggleTime = millis();
  alarmLedState = false;
  lcdLocked = false;
  lcdShow("!!! BAO DONG !!!", "Sai 3 lan lien  ");
  writeToFirebase("canh_bao_trom", "He Thong");
  wrongCount = 0;
  Serial.println("[ALARM] Bat dau bao dong (non-blocking)");
}

String extractJsonValue(String json, String key) {
  String searchKey = "\"" + key + "\":\"";
  int start = json.indexOf(searchKey);
  if (start == -1) return "";
  start += searchKey.length();
  int end = json.indexOf("\"", start);
  if (end == -1) return "";
  return json.substring(start, end);
}

void checkFirebaseCommand() {
  if (millis() - lastFirebaseCheck < 1500) return;
  lastFirebaseCheck = millis();
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String(FIREBASE_URL) + "/smart_gate/lenh_dieu_khien.json?auth=" + FIREBASE_SECRET;
  http.begin(url);
  int code = http.GET();

  if (code == 200) {
    String payload = http.getString();
    if (payload.indexOf("\"lenh\":\"none\"") == -1 && payload.length() > 5) {
      aiBlockTime = millis();

      if (payload.indexOf("\"lenh\":\"mo_cong\"") > 0) {
        if (!gateOpen) openGate("Admin Web");
      }
      else if (payload.indexOf("\"lenh\":\"dong_cong\"") > 0) {
        if (gateOpen) closeGate("Admin Web");
      }
      else if (payload.indexOf("\"lenh\":\"bat_led\"") > 0) {
        digitalWrite(PIN_LED, HIGH);
        writeToFirebase("bat_led", "Admin Web");
      }
      else if (payload.indexOf("\"lenh\":\"tat_led\"") > 0) {
        digitalWrite(PIN_LED, LOW);
        writeToFirebase("tat_led", "Admin Web");
      }
      else if (payload.indexOf("\"lenh\":\"bat_coi\"") > 0) {
        digitalWrite(PIN_BUZZER, HIGH);
        writeToFirebase("bat_coi", "Admin Web");
      }
      else if (payload.indexOf("\"lenh\":\"tat_coi\"") > 0) {
        digitalWrite(PIN_BUZZER, LOW);
        writeToFirebase("tat_coi", "Admin Web");
      }
      else if (payload.indexOf("\"lenh\":\"xoa_lcd\"") > 0) {
        lcdLocked = false;
        lcdDefault();
        writeToFirebase("xoa_lcd", "Admin Web");
      }
      else if (payload.indexOf("\"lenh\":\"hien_thi_lcd\"") > 0) {
        String l1 = extractJsonValue(payload, "l1");
        String l2 = extractJsonValue(payload, "l2");
        lcdLocked = true;
        lcdLockTime = millis();
        lcdShow(l1, l2);
        writeToFirebase("hien_thi_lcd", "Admin Web");
      }

      clearFirebaseCommand();
    }
  }
  http.end();
}

void handle_dung() {
  server.send(200, "text/plain", "OK");
  wrongCount = 0;
  if (!gateOpen && (millis() - aiBlockTime > 10000)) {
    openGate("Camera AI");
  } else if (!gateOpen) {
    Serial.println("[AI] Bi block boi Web Admin, con " +
                   String(10 - (millis() - aiBlockTime) / 1000) + "s");
  }
}

void handle_sai() {
  server.send(200, "text/plain", "OK");
  if (alarmActive) return;
  wrongCount++;
  lcdLocked = true;
  lcdLockTime = millis();
  lcdShow("Khong nhan dien!", "Lan sai: " + String(wrongCount));
  writeToFirebase("sai_khuon_mat", "Camera AI");
  if (wrongCount >= 3) triggerAlarm();
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  Wire.begin(25, 33);
  lcd.init();
  lcd.backlight();
  lcdShow("Dang khoi dong..", "Vui long cho... ");

  myServo.attach(PIN_SERVO);
  myServo.write(SERVO_DONG);

  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_IR, INPUT);   // Khởi tạo cảm biến IR

  digitalWrite(PIN_LED, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  int tries = 0;
  while (!getLocalTime(&timeinfo) && tries++ < 10) delay(500);

  clearFirebaseCommand();

  server.on("/dung", HTTP_GET, handle_dung);
  server.on("/sai",  HTTP_GET, handle_sai);
  server.begin();

  lcdDefault();
  Serial.println("[SETUP] San sang! IP: " + WiFi.localIP().toString());
}

void loop() {
  server.handleClient();
  checkFirebaseCommand();

  // =========================
  // XỬ LÝ CẢM BIẾN HỒNG NGOẠI
  // =========================
  int irValue = digitalRead(PIN_IR);

  // Thường cảm biến vật cản IR xuất LOW khi gặp vật cản
  if (irValue == LOW && !gateOpen && !irTriggered) {
    Serial.println("[IR] Phat hien vat can -> Mo cong");
    openGate("Cam bien IR");
    irTriggered = true;
  }

  // Reset cờ khi vật cản rời đi
  if (irValue == HIGH) {
    irTriggered = false;
  }

  // Alarm non-blocking
  if (alarmActive) {
    if (millis() - alarmToggleTime >= 150) {
      alarmLedState = !alarmLedState;
      digitalWrite(PIN_LED, alarmLedState ? HIGH : LOW);
      digitalWrite(PIN_BUZZER, alarmLedState ? HIGH : LOW);
      alarmToggleTime = millis();
    }

    if (millis() - alarmStartTime >= 10000) {
      digitalWrite(PIN_LED, LOW);
      digitalWrite(PIN_BUZZER, LOW);
      alarmActive = false;
      lcdDefault();
      Serial.println("[ALARM] Ket thuc bao dong");
    }
  }

  // Tự đóng cổng sau 10 giây
  if (gateOpen && (millis() - gateOpenTime >= GATE_AUTO_CLOSE_MS)) {
    Serial.println("[GATE] Het 10 giay -> Tu dong DONG CONG!");
    closeGate("Tu Dong");
  }

  if (lcdLocked && (millis() - lcdLockTime > 20000)) {
    lcdLocked = false;
    lcdShow(gateOpen ? "Cua dang mo...  " : "Nhom 11         ",
            gateOpen ? "Xin moi vao!    " : "Cua Thong Minh  ");
  }

  delay(10);
}