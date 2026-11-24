// SWQMS - Tes hardware pengirim (ESP32): LoRa SX1278, pH, INA219, DS18B20
// Library (Library Manager): LoRa (Sandeep Mistry), Adafruit INA219,
//                            OneWire, DallasTemperature
// Kalau wiring-mu beda, cukup ubah #define di bawah.

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ---- LoRa SX1278 (VSPI) ----
#define LORA_SCK   18
#define LORA_MISO  19
#define LORA_MOSI  23
#define LORA_NSS    5
#define LORA_RST   27
#define LORA_DIO0  26
#define LORA_FREQ  433E6

// ---- I2C (INA219) ----
#define I2C_SDA 21
#define I2C_SCL 22

// ---- pH (harus ADC1: GPIO 32-39) ----
#define PH_PIN 34

// ---- DS18B20 ----
#define ONEWIRE_PIN 15

Adafruit_INA219 ina;
OneWire oneWire(ONEWIRE_PIN);
DallasTemperature ds(&oneWire);

bool inaOk = false, loraOk = false;

void i2cScan() {
  Serial.println("[I2C] scan...");
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[I2C] ketemu alamat 0x%02X\n", a);
      n++;
    }
  }
  if (n == 0) Serial.println("[I2C] TIDAK ada device -> cek SDA/SCL/VCC/GND INA");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== SWQMS hardware test ===");

  // I2C + INA219
  Wire.begin(I2C_SDA, I2C_SCL);
  i2cScan();
  inaOk = ina.begin();
  Serial.println(inaOk ? "[INA219] OK" : "[INA219] GAGAL");

  // LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  loraOk = LoRa.begin(LORA_FREQ);
  Serial.println(loraOk ? "[LoRa] OK" : "[LoRa] GAGAL -> cek NSS/RST/SCK/MISO/MOSI & 3V3");

  // pH
  analogReadResolution(12);
  analogSetPinAttenuation(PH_PIN, ADC_11db);

  // DS18B20
  ds.begin();
  Serial.printf("[DS18B20] jumlah sensor: %d\n", ds.getDeviceCount());
}

void loop() {
  // pH mentah (rata-rata 10x)
  long sum = 0;
  for (int i = 0; i < 10; i++) { sum += analogRead(PH_PIN); delay(5); }
  float raw = sum / 10.0;
  float volt = raw / 4095.0 * 3.3;
  Serial.printf("[pH] raw=%.0f  V=%.3f\n", raw, volt);

  // Suhu
  ds.requestTemperatures();
  float t = ds.getTempCByIndex(0);
  if (t == DEVICE_DISCONNECTED_C) Serial.println("[Suhu] TIDAK terbaca -> cek DAT/VCC/GND & resistor 4.7k");
  else Serial.printf("[Suhu] %.2f C\n", t);

  // INA219
  if (inaOk) {
    Serial.printf("[INA] bus=%.2fV  arus=%.1fmA\n", ina.getBusVoltage_V(), ina.getCurrent_mA());
  }

  // Kirim paket tes LoRa
  if (loraOk) {
    LoRa.beginPacket();
    LoRa.printf("TEST pH=%.3fV T=%.2f", volt, t);
    LoRa.endPacket();
    Serial.println("[LoRa] paket terkirim");
  }

  Serial.println("---");
  delay(2000);
}
