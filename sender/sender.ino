// SWQMS - SKETCH PENGIRIM FINAL (ESP32) - tanpa TDS
// Sensor: pH (PH-4502C), suhu DS18B20, flow YF-S201, INA219, LoRa SX1278
// Library: LoRa (Sandeep Mistry), Adafruit INA219, OneWire, DallasTemperature
//
// WIRING:
//   pH   Po -> GPIO34       (V+ -> 5V, G -> GND)
//   Suhu DAT -> GPIO32      (VCC 3.3V, GND, resistor 4.7k antara DAT dan 3.3V)
//   Flow kuning -> pembagi 10k/20k -> GPIO25  (merah 5V, hitam GND)
//   INA219 SDA -> GPIO21, SCL -> GPIO22       (VCC 3.3V)
//   LoRa SCK 18, MISO 19, MOSI 23, NSS 5, RST 27, DIO0 26   (VCC 3.3V saja)

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ---------- PIN ----------
#define PIN_PH    34
#define PIN_TEMP  32
#define PIN_FLOW  25
#define I2C_SDA   21
#define I2C_SCL   22
#define LORA_SCK  18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_SS    5
#define LORA_RST  27
#define LORA_DIO0 26
#define LORA_FREQ 433E6            // harus sama dengan penerima

#define NODE_ID   "SWQMS1"

// ---------- KALIBRASI ----------
const float PH7_V    = 1.42;       // tegangan Po di buffer pH 7 (hasil kalibrasi)
const float PH_SLOPE = 0.18;       // volt per 1 pH (kalibrasi lagi dengan buffer pH 4 bila perlu)
const float FLOW_CAL = 7.5;        // YF-S201: Hz = 7.5 * L/min

// ---------- GLOBAL ----------
OneWire oneWire(PIN_TEMP);
DallasTemperature ds(&oneWire);
Adafruit_INA219 ina;
bool inaOk = false;

volatile unsigned long pulses = 0;
unsigned long lastSample = 0, lastTx = 0;
uint16_t seq = 0;                // nomor urut paket (untuk hitung paket hilang di gateway)
float totalLiter = 0;

void IRAM_ATTR onPulse() { pulses++; }

// median beberapa pembacaan (mV) agar stabil
float medianMv(int pin, int n = 15) {
  int a[31];
  if (n > 31) n = 31;
  for (int i = 0; i < n; i++) { a[i] = analogReadMilliVolts(pin); delay(5); }
  for (int i = 0; i < n - 1; i++)
    for (int j = 0; j < n - i - 1; j++)
      if (a[j] > a[j + 1]) { int t = a[j]; a[j] = a[j + 1]; a[j + 1] = t; }
  return a[n / 2];
}

float readPh() {
  float v = medianMv(PIN_PH) / 1000.0f;
  return 7.0f + (PH7_V - v) / PH_SLOPE;
}

// Status mutu (Tabel 3.4, tanpa TDS): 0 = baik, 1 = perhatian, 2 = tidak layak
int statusMutu(float ph, float suhu, bool suhuOk) {
  bool tidakLayak = (ph < 6.0 || ph > 9.0) || (suhuOk && (suhu > 35 || suhu < 5));
  bool perhatian  = (ph < 6.5 || ph > 8.5) || (suhuOk && (suhu > 30 || suhu < 10));
  if (tidakLayak) return 2;
  if (perhatian)  return 1;
  return 0;
}

// Interval transmisi adaptif (Tabel 3.6)
unsigned long intervalMs(int status) {
  if (status == 2) return 15000;
  if (status == 1) return 30000;
  return 60000;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== SWQMS PENGIRIM FINAL ===");

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_PH, ADC_11db);

  pinMode(PIN_FLOW, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_FLOW), onPulse, RISING);

  ds.begin();
  ds.setResolution(10);           // konversi lebih cepat (~190 ms)
  Serial.printf("[DS18B20] jumlah sensor: %d\n", ds.getDeviceCount());

  Wire.begin(I2C_SDA, I2C_SCL);
  inaOk = ina.begin();
  Serial.println(inaOk ? "[INA219] OK" : "[INA219] GAGAL");

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("[LoRa] GAGAL -> cek wiring & 3.3V");
    while (true) delay(1000);
  }
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.enableCrc();
  Serial.println("[LoRa] OK");

  lastSample = lastTx = millis();
}

void loop() {
  unsigned long now = millis();
  if (now - lastSample < 1000) return;
  float dt = (now - lastSample) / 1000.0f;
  lastSample = now;

  // flow
  noInterrupts(); unsigned long p = pulses; pulses = 0; interrupts();
  float flowLmin = (p / dt) / FLOW_CAL;
  totalLiter += flowLmin / 60.0f * dt;

  // suhu
  ds.requestTemperatures();
  float suhu = ds.getTempCByIndex(0);
  bool suhuOk = (suhu != DEVICE_DISCONNECTED_C);
  if (!suhuOk) suhu = -127.0f;

  // pH
  float ph = readPh();

  // INA
  float busV = inaOk ? ina.getBusVoltage_V() : 0;
  float mA   = inaOk ? ina.getCurrent_mA()   : 0;

  int st = statusMutu(ph, suhu, suhuOk);
  const char* stTxt = st == 0 ? "BAIK" : (st == 1 ? "PERHATIAN" : "TIDAK LAYAK");

  Serial.printf("pH=%.2f | Suhu=%.2f C%s | Flow=%.2f L/min (total %.2f L) | INA=%.2fV %.1fmA | %s\n",
                ph, suhu, suhuOk ? "" : " (TIDAK TERBACA)", flowLmin, totalLiter, busV, mA, stTxt);

  // kirim LoRa sesuai interval adaptif
  if (now - lastTx >= intervalMs(st)) {
    lastTx = now;
    LoRa.beginPacket();
    // Format 9 kolom: ID,seq,pH,suhu,flow,total,busV,mA,status
    LoRa.printf("%s,%u,%.2f,%.2f,%.2f,%.2f,%.2f,%.1f,%d",
                NODE_ID, seq++, ph, suhu, flowLmin, totalLiter, busV, mA, st);
    LoRa.endPacket();
    Serial.println(">> paket LoRa terkirim");
  }
}
