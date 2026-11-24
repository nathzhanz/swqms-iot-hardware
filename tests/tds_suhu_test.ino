// Tes TDS + kompensasi suhu (DS18B20) di ESP32
// TDS : + -> 5V, - -> GND, A -> GPIO35
// Suhu: DAT -> GPIO32 (4.7k antara DAT dan 3.3V), VCC 3.3V, GND
// Library: OneWire, DallasTemperature

#include <OneWire.h>
#include <DallasTemperature.h>

#define TDS_PIN     35
#define ONEWIRE_PIN 32
#define SAMPLES     30

OneWire oneWire(ONEWIRE_PIN);
DallasTemperature ds(&oneWire);

float medianMv(int pin, int n) {
  int a[SAMPLES];
  for (int i = 0; i < n; i++) { a[i] = analogReadMilliVolts(pin); delay(10); }
  for (int i = 0; i < n - 1; i++)
    for (int j = 0; j < n - i - 1; j++)
      if (a[j] > a[j + 1]) { int t = a[j]; a[j] = a[j + 1]; a[j + 1] = t; }
  return a[n / 2];
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetPinAttenuation(TDS_PIN, ADC_11db);
  ds.begin();
}

void loop() {
  // suhu (kalau gagal, pakai 25 C)
  ds.requestTemperatures();
  float t = ds.getTempCByIndex(0);
  bool suhuOk = (t != DEVICE_DISCONNECTED_C);
  if (!suhuOk) t = 25.0;

  // TDS
  float v = medianMv(TDS_PIN, SAMPLES) / 1000.0f;
  float vc = v / (1.0f + 0.02f * (t - 25.0f));   // kompensasi suhu ke 25 C
  float tds = (133.42f * vc * vc * vc - 255.86f * vc * vc + 857.39f * vc) * 0.5f;
  if (tds < 0) tds = 0;

  Serial.printf("[TDS] V=%.3f  TDS=%.0f ppm  | Suhu=%.2f C %s\n",
                v, tds, t, suhuOk ? "" : "(suhu tidak terbaca, pakai 25)");
  delay(1000);
}
