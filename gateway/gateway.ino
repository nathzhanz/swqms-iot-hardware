
#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

// =====================================================
// WIFI CONFIGURATION
// =====================================================

// Kredensial ada di secrets.h (masuk .gitignore). Salin dari secrets_example.h
#include "secrets.h"

// =====================================================
// LORA PIN CONFIGURATION
// =====================================================

#define LORA_SCK  18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_SS    5
#define LORA_RST  14
#define LORA_DIO0  4

#define LORA_FREQ 433E6

// =====================================================
// PARAMETERS
// =====================================================

#define BUF_N 240

#define TS_MIN_MS 15000UL
#define WIFI_RETRY_MS 5000UL
#define WIFI_TIMEOUT_MS 15000UL

#define HTTP_CONNECT_MS 5000
#define HTTP_TIMEOUT_MS 5000

#define NTP_VALID 1700000000L

// =====================================================
// DATA STRUCTURE
// =====================================================

struct Rekam {
  time_t waktu;

  bool adaPh;
  bool adaSuhu;
  bool adaDebit;
  bool adaHilang;

  float ph;
  float suhu;
  float debitMlS;

  int rssi;
  float snr;

  int hilang;
  int status;
};

// =====================================================
// GLOBAL VARIABLES
// =====================================================

Rekam buf[BUF_N];

int head = 0;
int tail = 0;
int isi = 0;

unsigned long timpaan = 0;
unsigned long paketRusak = 0;
unsigned long totalDiterima = 0;

uint16_t lastSeq = 0;
bool adaSeq = false;

unsigned long hilangTotal = 0;

unsigned long tKirimTS = 0;
unsigned long tWiFi = 0;
unsigned long tWiFiAttempt = 0;

int gagalBeruntun = 0;

bool wifiOnline = false;
bool wifiAttempting = false;

// =====================================================
// WIFI CONNECTION
// =====================================================

void mulaiWiFi() {

  Serial.println();
  Serial.println("[WiFi] Memulai koneksi hotspot...");

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  wifiAttempting = true;
  tWiFiAttempt = millis();
  tWiFi = millis();

}

void cekWiFi() {

  if (WiFi.status() == WL_CONNECTED) {

    wifiAttempting = false;

    if (!wifiOnline) {

      wifiOnline = true;

      Serial.println();
      Serial.println("================================");
      Serial.println("[WiFi] BERHASIL TERHUBUNG!");
      Serial.print("[WiFi] SSID: ");
      Serial.println(WiFi.SSID());

      Serial.print("[WiFi] IP Address: ");
      Serial.println(WiFi.localIP());

      Serial.print("[WiFi] Signal RSSI: ");
      Serial.println(WiFi.RSSI());

      Serial.println("================================");

    }

    return;
  }

  if (wifiOnline) {

    wifiOnline = false;

    Serial.println();
    Serial.println("[WiFi] KONEKSI TERPUTUS!");

  }

  // Cek timeout koneksi
  if (wifiAttempting &&
      millis() - tWiFiAttempt >= WIFI_TIMEOUT_MS) {

    Serial.println("[WiFi] Timeout! Koneksi gagal.");

    WiFi.disconnect(false, false);

    wifiAttempting = false;
    tWiFi = millis();

  }

  // Coba koneksi ulang setiap 5 detik
  if (!wifiAttempting &&
      millis() - tWiFi >= WIFI_RETRY_MS) {

    Serial.println("[WiFi] Mencoba koneksi ulang...");

    WiFi.begin(WIFI_SSID, WIFI_PASS);

    wifiAttempting = true;
    tWiFiAttempt = millis();
    tWiFi = millis();

  }

}

// =====================================================
// DATA VALIDATION
// =====================================================

bool angka(const String &s) {

  bool digit = false;

  for (unsigned int i = 0; i < s.length(); i++) {

    char c = s[i];

    if (c >= '0' && c <= '9') {
      digit = true;
    }

    else if (c != '-' && c != '+' && c != '.') {
      return false;
    }

  }

  return digit;
}

// =====================================================
// CSV PARSER
// =====================================================

int pecah(const String &s, String *out, int maks) {

  int n = 0;
  int start = 0;

  for (int i = 0; i <= (int)s.length() && n < maks; i++) {

    if (i == (int)s.length() || s[i] == ',') {

      out[n++] = s.substring(start, i);

      start = i + 1;

    }

  }

  return n;
}

// =====================================================
// BUFFER FIFO
// =====================================================

void simpan(const Rekam &r) {

  buf[head] = r;

  head = (head + 1) % BUF_N;

  if (isi < BUF_N) {

    isi++;

  } else {

    tail = (tail + 1) % BUF_N;

    timpaan++;

    Serial.printf(
      "[BUF] Penuh, data terlama ditimpa. Total: %lu\n",
      timpaan
    );

  }

}

// =====================================================
// RECEIVE LORA DATA
// =====================================================

void terimaLora() {

  int ukuran = LoRa.parsePacket();

  if (ukuran == 0) return;

  String msg;

  while (LoRa.available()) {
    msg += (char)LoRa.read();
  }

  int rssi = LoRa.packetRssi();
  float snr = LoRa.packetSnr();

  String t[10];

  int n = pecah(msg, t, 10);

  if (n != 8 && n != 9) {

    paketRusak++;

    Serial.printf(
      "[RX] Format tidak sesuai (%d kolom): %s\n",
      n,
      msg.c_str()
    );

    return;
  }

  int o = (n == 9) ? 1 : 0;

  if (!angka(t[7 + o])) {

    paketRusak++;

    Serial.println("[RX] Status tidak valid!");

    return;
  }

  Rekam r;

  r.waktu = 0;

  r.adaPh = false;
  r.adaSuhu = false;
  r.adaDebit = false;
  r.adaHilang = false;

  r.ph = 0;
  r.suhu = 0;
  r.debitMlS = 0;

  r.hilang = 0;

  r.rssi = rssi;
  r.snr = snr;

  r.status = t[7 + o].toInt();

  // pH
  if (angka(t[1 + o])) {

    float v = t[1 + o].toFloat();

    if (v >= 0 && v <= 14) {

      r.ph = v;
      r.adaPh = true;

    }

  }

  // Temperature
  if (angka(t[2 + o])) {

    float v = t[2 + o].toFloat();

    if (v > -100 && v < 125) {

      r.suhu = v;
      r.adaSuhu = true;

    }

  }

  // Flow rate
  if (angka(t[3 + o])) {

    float v = t[3 + o].toFloat();

    if (v >= 0) {

      r.debitMlS = v * 1000.0f / 60.0f;

      r.adaDebit = true;

    }

  }

  // Packet loss
  if (o == 1 && angka(t[1])) {

    uint16_t seq = (uint16_t)t[1].toInt();

    if (adaSeq) {

      uint16_t celah =
        (uint16_t)(seq - lastSeq - 1);

      if (celah < 1000) {

        hilangTotal += celah;

      }

    }

    lastSeq = seq;
    adaSeq = true;

    r.adaHilang = true;
    r.hilang = (int)hilangTotal;

  }

  // NTP timestamp
  time_t sekarang = time(nullptr);

  r.waktu =
    (sekarang > NTP_VALID) ? sekarang : 0;

  // Save to buffer
  totalDiterima++;

  simpan(r);

  // Serial output
  Serial.println();
  Serial.println("================================");

  Serial.printf(
    "[RX] %s\n",
    msg.c_str()
  );

  Serial.printf(
    "RSSI: %d dBm | SNR: %.1f dB\n",
    rssi,
    snr
  );

  Serial.printf(
    "pH: %s\n",
    r.adaPh ? String(r.ph, 2).c_str() : "-"
  );

  Serial.printf(
    "Suhu: %s\n",
    r.adaSuhu ? String(r.suhu, 2).c_str() : "-"
  );

  Serial.printf(
    "Debit: %s mL/s\n",
    r.adaDebit ? String(r.debitMlS, 1).c_str() : "-"
  );

  Serial.printf(
    "Status: %d\n",
    r.status
  );

  Serial.printf(
    "Paket hilang: %s\n",
    r.adaHilang ? String(r.hilang).c_str() : "-"
  );

  Serial.printf(
    "Buffer: %d/%d\n",
    isi,
    BUF_N
  );

  Serial.printf(
    "Total diterima: %lu | Rusak: %lu\n",
    totalDiterima,
    paketRusak
  );

  Serial.println("================================");

}

// =====================================================
// SEND DATA TO THINGSPEAK
// =====================================================

bool kirimRekam(const Rekam &r) {

  String url =
    String("https://api.thingspeak.com/update?api_key=")
    + TS_WRITE_KEY;

  // Field 1: TDS (not available yet)

  if (r.adaPh) {
    url += "&field2=" + String(r.ph, 2);
  }

  if (r.adaSuhu) {
    url += "&field3=" + String(r.suhu, 2);
  }

  if (r.adaDebit) {
    url += "&field4=" + String(r.debitMlS, 1);
  }

  url += "&field5=" + String(r.rssi);

  url += "&field6=" + String(r.snr, 1);

  if (r.adaHilang) {
    url += "&field7=" + String(r.hilang);
  }

  url += "&field8=" + String(r.status);

  // Timestamp UTC
  if (r.waktu > NTP_VALID) {

    char ts[40];

    struct tm tmv;

    gmtime_r(&r.waktu, &tmv);

    strftime(
      ts,
      sizeof(ts),
      "%Y-%m-%d%%20%H:%M:%S%%20%%2B0000",
      &tmv
    );

    url += "&created_at=";
    url += ts;

  }

  WiFiClientSecure klien;

  klien.setInsecure();

  HTTPClient http;

  http.setConnectTimeout(HTTP_CONNECT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);

  if (!http.begin(klien, url)) {

    Serial.println("[TS] http.begin gagal!");

    return false;

  }

  Serial.println("[TS] Mengirim data...");

  int kode = http.GET();

  String balas =
    (kode > 0) ? http.getString() : String("");

  http.end();

  balas.trim();

  bool ok =
    (kode == 200 && balas.toInt() > 0);

  Serial.printf(
    "[TS] HTTP %d | Balasan: '%s' | %s\n",
    kode,
    balas.c_str(),
    ok ? "BERHASIL" : "GAGAL"
  );

  return ok;

}

// =====================================================
// SEND OLDEST BUFFER DATA
// =====================================================

void kirimTertua() {

  tKirimTS = millis();

  if (kirimRekam(buf[tail])) {

    tail = (tail + 1) % BUF_N;

    isi--;

    gagalBeruntun = 0;

    Serial.printf(
      "[TS] Berhasil. Sisa antrean: %d\n",
      isi
    );

  } else {

    gagalBeruntun++;

    Serial.printf(
      "[TS] Gagal berturut-turut: %d\n",
      gagalBeruntun
    );

    if (gagalBeruntun >= 3) {

      Serial.println(
        "[TS] 3 kali gagal. Memeriksa koneksi WiFi..."
      );

      WiFi.disconnect(false, false);

      wifiOnline = false;
      wifiAttempting = false;

      tWiFi = millis();

      gagalBeruntun = 0;

    }

  }

}

// =====================================================
// INITIALIZE LORA
// =====================================================

void initLoRa() {

  SPI.begin(
    LORA_SCK,
    LORA_MISO,
    LORA_MOSI,
    LORA_SS
  );

  LoRa.setPins(
    LORA_SS,
    LORA_RST,
    LORA_DIO0
  );

  bool ok = false;

  for (int i = 1; i <= 5 && !ok; i++) {

    Serial.printf(
      "[LoRa] Memulai modul (%d/5)...\n",
      i
    );

    ok = LoRa.begin(LORA_FREQ);

    if (!ok) {

      Serial.println(
        "[LoRa] Gagal. Periksa wiring dan supply 3.3V."
      );

      delay(5000);

    }

  }

  if (!ok) {

    Serial.println(
      "[LoRa] Gagal 5 kali. Restart ESP32."
    );

    ESP.restart();

  }

  LoRa.setSpreadingFactor(7);

  LoRa.setSignalBandwidth(125E3);

  LoRa.setCodingRate4(5);

  LoRa.enableCrc();

  Serial.println("[LoRa] BERHASIL DIINISIALISASI!");

}

// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("================================");
  Serial.println("     SWQMS GATEWAY FINAL");
  Serial.println("================================");

  // WiFi
  mulaiWiFi();

  // NTP UTC
  configTime(
    0,
    0,
    "pool.ntp.org",
    "time.google.com"
  );

  // LoRa
  initLoRa();

  Serial.println();
  Serial.println("[SYSTEM] Gateway siap bekerja.");

}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {

  // 1. Receive LoRa packets
  terimaLora();

  // 2. Monitor WiFi connection
  cekWiFi();

  // 3. Send buffered data to ThingSpeak
  if (
    isi > 0 &&
    WiFi.status() == WL_CONNECTED &&
    millis() - tKirimTS >= TS_MIN_MS
  ) {

    kirimTertua();

  }

}
