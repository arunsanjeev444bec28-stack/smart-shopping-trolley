#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "HX711.h"
#include "upi_qr_bitmap.h"

// ================= SETTINGS (edit for each deployment) =================
#define WIFI_SSID     "HoTsPoT"
#define WIFI_PASS     "P.@run44"
#define SERVER_HOST   "10.100.189.188"      // laptop IPv4 from ipconfig
#define SERVER_PORT   8000
#define DEVICE_TOKEN  "pQcmAzke7h0IjcYniFjMAsQg2vtgP7vv"
#define TROLLEY_ID    "T01"
// =======================================================================

// ---------- Pins ----------
#define SS_PIN      5
#define RST_PIN     4
#define BUZZER_PIN  27
#define BUTTON_PIN  14
#define HX_DT_PIN   32
#define HX_SCK_PIN  33

// ---------- OLED ----------
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_ADDR     0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

MFRC522 mfrc522(SS_PIN, RST_PIN);
HX711 scale;

// ---------- Weight alarm settings ----------
#define CAL_FACTOR        323.201
#define WEIGHT_THRESHOLD  20.0
#define CONFIRM_MS        400
#define GRACE_MS          6000

bool  weightOK = false;
float weightBuf[4] = {0, 0, 0, 0};
int   wIdx = 0;
float weightNow = 0;
float baseline  = 0;
unsigned long graceUntil = 0;
unsigned long overSince  = 0;
bool  alarmActive = false;

// ---------- Product database ----------
struct Product {
  const char* uid;
  const char* name;
  int price;
  int qty;
};

Product products[] = {
  {"03432B07", "MARIGOLD BISCUIT", 10, 0},
  {"BA62CA01", "GOODDAY BISCUIT",  10, 0},
  {"B69D4E06", "MILKBIKIS",        10, 0},
  {"A9217106", "RICE 100G",        30, 0},
  {"AD8FB302", "URAD DHAL 100G",   50, 0},
  {"A6C21407", "WHEAT 100G",       25, 0}
};
const int NUM_PRODUCTS = sizeof(products) / sizeof(products[0]);

// ---------- Cart state ----------
int totalItems = 0;
int cartTotal  = 0;

bool popupActive = false;
unsigned long popupUntil = 0;

String lastUid = "";
unsigned long lastScanTime = 0;

// ---------- Network state ----------
String cartId;
unsigned long lastWifiTry = 0;
bool lastNetState = false;
String checkoutJson = "";
bool checkoutPending = false;
unsigned long lastSendTry = 0;

// ---------- Buzzer (ACTIVE buzzer) ----------
void beep(int ms) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(ms);
  digitalWrite(BUZZER_PIN, alarmActive ? HIGH : LOW);
}
void beepOK()      { beep(60); }
void beepWarning() { beep(200); delay(80); beep(200); }

// ---------- Network ----------
bool netOnline() { return WiFi.status() == WL_CONNECTED; }

void maintainNetwork() {
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastWifiTry > 10000) {
      lastWifiTry = millis();
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
  }
}

String buildCheckoutJson() {
  String s = "{\"cart_id\":\"" + cartId + "\",\"trolley_id\":\"" + TROLLEY_ID + "\",\"lines\":[";
  bool first = true;
  for (int i = 0; i < NUM_PRODUCTS; i++) {
    if (products[i].qty > 0) {
      if (!first) s += ",";
      first = false;
      s += "{\"uid\":\"" + String(products[i].uid) + "\",\"qty\":" + String(products[i].qty) + "}";
    }
  }
  s += "]}";
  return s;
}

// Sends the bill. Prices are NOT sent: the server looks them up itself.
void trySendCheckout() {
  if (!checkoutPending) return;
  if (millis() - lastSendTry < 4000 && lastSendTry != 0) return;
  lastSendTry = millis();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Bill not sent yet: Wi-Fi offline, will retry.");
    return;
  }

  HTTPClient http;
  String url = String("http://") + SERVER_HOST + ":" + SERVER_PORT + "/api/checkout";
  http.setConnectTimeout(3000);
  http.setTimeout(4000);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Token", DEVICE_TOKEN);
  int code = http.POST(checkoutJson);

  if (code == 200 || code == 201) {
    Serial.printf("BILL SAVED on server (HTTP %d): %s\n", code, http.getString().c_str());
    checkoutPending = false;
  } else if (code >= 400 && code < 500) {
    Serial.printf("Server REJECTED the bill (HTTP %d): %s\n", code, http.getString().c_str());
    Serial.println("Check DEVICE_TOKEN and product tags. Not retrying.");
    checkoutPending = false;
  } else {
    Serial.printf("Send failed (code %d). Will retry.\n", code);
  }
  http.end();
}

// ---------- Display helpers ----------
void drawNetDot(int x, int y, uint16_t color) {
  if (netOnline()) display.fillCircle(x, y, 3, color);
  else             display.drawCircle(x, y, 3, color);
}

void drawHeader() {
  display.fillRect(0, 0, 128, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(2, 1);
  display.print("Items:");
  display.print(totalItems);
  String t = "Rs " + String(cartTotal);
  display.setCursor(126 - (t.length() * 6), 1);
  display.print(t);
  drawNetDot(64, 5, SSD1306_BLACK);
  display.setTextColor(SSD1306_WHITE);
}

void showHome() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(18, 0);
  display.print("SMART TROLLEY");
  drawNetDot(120, 4, SSD1306_WHITE);
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(0, 16);
  display.print("Items:");
  display.print(totalItems);
  display.setCursor(0, 36);
  display.print("Rs ");
  display.print(cartTotal);

  display.setTextSize(1);
  display.setCursor(0, 56);
  display.print("Scan items/Checkout");
  display.display();
  popupActive = false;
}

void showAlert() {
  display.clearDisplay();
  drawHeader();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 16);
  display.print("UNSCANNED");
  display.setCursor(0, 34);
  display.print("ITEM!");
  display.setTextSize(1);
  display.setCursor(0, 54);
  display.print("Scan its RFID tag");
  display.display();
  popupActive = false;
}

void showItemPopup(const Product &p) {
  display.clearDisplay();
  drawHeader();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 15);
  display.print(p.name);
  display.setCursor(0, 29);
  display.print("Price: Rs ");
  display.print(p.price);
  display.setCursor(0, 41);
  display.print("Qty  : ");
  display.print(p.qty);
  display.setCursor(0, 54);
  display.print("Item added");
  display.display();
  popupActive = true;
  popupUntil = millis() + 1200;
}

void showUnknownTag(const String &uid) {
  display.clearDisplay();
  drawHeader();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 16);
  display.print("UNKNOWN");
  display.setCursor(0, 34);
  display.print("TAG!");
  display.setTextSize(1);
  display.setCursor(0, 54);
  display.print(uid);
  display.display();
  popupActive = true;
  popupUntil = millis() + 1500;
}

void showMessage(const char* l1, const char* l2) {
  display.clearDisplay();
  drawHeader();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 24);
  display.print(l1);
  display.setCursor(0, 38);
  display.print(l2);
  display.display();
}

// ---------- Alarm control ----------
void startAlarm() {
  alarmActive = true;
  digitalWrite(BUZZER_PIN, HIGH);
  Serial.printf("ALARM: weight rose by %.1f g without an RFID scan\n", weightNow - baseline);
  showAlert();
}

void stopAlarm() {
  alarmActive = false;
  digitalWrite(BUZZER_PIN, LOW);
  Serial.println("Alarm cleared.");
  showHome();
}

// ---------- Weight (not shown on screen) ----------
void updateWeight() {
  if (!weightOK) return;
  if (scale.is_ready()) {
    weightBuf[wIdx] = scale.get_units(1);
    wIdx = (wIdx + 1) % 4;
    weightNow = (weightBuf[0] + weightBuf[1] + weightBuf[2] + weightBuf[3]) / 4.0;
  }
}

void checkWeightAlarm() {
  if (!weightOK) return;

  if (millis() < graceUntil) {
    baseline = weightNow;
    overSince = 0;
    return;
  }

  float diff = weightNow - baseline;

  if (diff > WEIGHT_THRESHOLD) {
    if (overSince == 0) overSince = millis();
    else if (!alarmActive && millis() - overSince >= CONFIRM_MS) startAlarm();
  } else {
    overSince = 0;
    if (alarmActive) {
      stopAlarm();
    } else if (diff < -WEIGHT_THRESHOLD) {
      baseline = weightNow;
    }
  }
}

// ---------- RFID ----------
String readUID() {
  String uid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

void handleCard() {
  String uid = readUID();
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  if (uid == lastUid && millis() - lastScanTime < 300) return;
  lastUid = uid;
  lastScanTime = millis();

  for (int i = 0; i < NUM_PRODUCTS; i++) {
    if (uid == products[i].uid) {
      products[i].qty++;
      totalItems++;
      cartTotal += products[i].price;

      graceUntil = millis() + GRACE_MS;
      baseline = weightNow;
      overSince = 0;
      if (alarmActive) {
        alarmActive = false;
        digitalWrite(BUZZER_PIN, LOW);
      }

      beepOK();
      Serial.printf("SCAN: %s | UID %s | Rs %d | Qty %d | Items %d | Total Rs %d\n",
                    products[i].name, uid.c_str(), products[i].price,
                    products[i].qty, totalItems, cartTotal);
      showItemPopup(products[i]);
      return;
    }
  }

  beepWarning();
  Serial.printf("WARNING: Unknown tag UID %s (cart unchanged)\n", uid.c_str());
  showUnknownTag(uid);
}

// ---------- Checkout ----------
void printBill() {
  Serial.println();
  Serial.println("============== BILL ==============");
  Serial.printf("Cart ID: %s\n", cartId.c_str());
  for (int i = 0; i < NUM_PRODUCTS; i++) {
    if (products[i].qty > 0) {
      Serial.printf("%-18s x%d @ Rs %-3d = Rs %d\n",
                    products[i].name, products[i].qty, products[i].price,
                    products[i].qty * products[i].price);
    }
  }
  Serial.println("----------------------------------");
  Serial.printf("Total items : %d\n", totalItems);
  Serial.printf("TOTAL AMOUNT: Rs %d\n", cartTotal);
  Serial.println("==================================");
  Serial.println("Scan the QR on the OLED to pay.");
  Serial.println("(Fixed QR: payment is NOT confirmed automatically)");
  Serial.println("Press EN/RESET to start a new cart.");
}

void doCheckout() {
  if (alarmActive) return;

  if (totalItems == 0) {
    beepWarning();
    showMessage("CART IS EMPTY", "Scan items first");
    delay(1500);
    showHome();
    return;
  }

  beepOK();
  printBill();

  checkoutJson = buildCheckoutJson();
  checkoutPending = true;
  lastSendTry = 0;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(9, 0);
  display.print("CHECKOUT COMPLETE");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  display.setCursor(0, 16);
  display.print("Total items: ");
  display.print(totalItems);
  display.setCursor(0, 30);
  display.print("Amount to pay:");
  display.setTextSize(2);
  display.setCursor(0, 44);
  display.print("Rs ");
  display.print(cartTotal);
  display.display();
  delay(2500);

  display.clearDisplay();
  display.drawBitmap(32, 0, UPI_QR_BITMAP, 64, 64, SSD1306_WHITE);
  display.display();

  // QR stays until EN/RESET. The bill is sent in the background.
  while (true) {
    maintainNetwork();
    trySendCheckout();
    delay(50);
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED NOT FOUND - check SDA/SCL/power");
    while (true) delay(1000);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(10, 8);
  display.print("SMART");
  display.setCursor(10, 28);
  display.print("TROLLEY");
  display.setTextSize(1);
  display.setCursor(10, 52);
  display.print("RFID Auto Billing");
  display.display();

  // Unique cart ID for this session (new one after every reset)
  char idbuf[32];
  snprintf(idbuf, sizeof(idbuf), "%s-%08X", TROLLEY_ID, (unsigned int)esp_random());
  cartId = String(idbuf);
  Serial.printf("Cart ID: %s\n", cartId.c_str());

  // Wi-Fi (non-blocking: the trolley works even if offline)
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  delay(1200);

  // RFID self-check
  SPI.begin(18, 19, 23, 5);
  mfrc522.PCD_Init();
  byte v = mfrc522.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.printf("MFRC522 version register: 0x%02X\n", v);
  if (v == 0x00 || v == 0xFF) {
    Serial.println("RFID READER NOT FOUND - check wiring and 3.3V");
    showMessage("RFID NOT FOUND", "Check wiring + 3.3V");
    beepWarning();
    while (true) delay(1000);
  }

  // Load cell: zero with an EMPTY basket
  showMessage("Keep basket EMPTY", "Setting zero...");
  scale.begin(HX_DT_PIN, HX_SCK_PIN);
  scale.set_scale(CAL_FACTOR);
  if (scale.wait_ready_timeout(2000)) {
    scale.tare(15);
    weightOK = true;
    for (int i = 0; i < 4; i++) weightBuf[i] = 0;
    weightNow = 0;
    baseline = 0;
    Serial.println("Load cell ready. Weight alarm ENABLED.");
  } else {
    weightOK = false;
    Serial.println("HX711 NOT READY - weight alarm DISABLED.");
  }

  Serial.println("System ready. Scan products or press checkout.");
  beepOK();
  showHome();
}

// ---------- Loop ----------
void loop() {
  maintainNetwork();
  updateWeight();
  checkWeightAlarm();

  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(30);
    if (digitalRead(BUTTON_PIN) == LOW) {
      doCheckout();
    }
  }

  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    handleCard();
  }

  if (popupActive && millis() > popupUntil) {
    if (alarmActive) showAlert();
    else             showHome();
  }

  // Refresh the Wi-Fi dot when the state changes
  bool st = netOnline();
  if (st != lastNetState) {
    lastNetState = st;
    if (!popupActive && !alarmActive) showHome();
  }
}