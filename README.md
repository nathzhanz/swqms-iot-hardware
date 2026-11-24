# SWQMS - Smart Water Quality Monitoring System

Kelompok 3, UNJ: Meindi Renada, Naila Shabrina, Muhammad Naufal Athallah.

Estimasi waktu kritis kualitas air berbasis parameter pH, suhu, dan flow meter pada cloud computing (ThingSpeak).
Sensor TDS menunggu modul pengganti.

## Alur sistem
Sender (ESP32 + sensor) -> LoRa 433 MHz -> Gateway (ESP32 + LoRa) -> WiFi -> ThingSpeak

## Struktur folder
- `sender/` firmware node pengirim
- `gateway/` firmware penerima, upload ke ThingSpeak
- `tests/` sketsa uji hardware per sensor
- `docs/` diagram draw.io

## Pin sender
| Komponen | Pin ESP32 |
|---|---|
| pH (Po) | GPIO34 |
| DS18B20 (DAT) | GPIO32 |
| Flow YF-S201 | GPIO25 (lewat pembagi tegangan 10k/20k) |
| INA219 SDA / SCL | GPIO21 / GPIO22 |
| LoRa SCK/MISO/MOSI | GPIO18 / 19 / 23 |
| LoRa SS / RST / DIO0 | GPIO5 / 27 / 26 |

## Pin gateway (LoRa)
SS GPIO5, RST GPIO14, DIO0 GPIO4.

## Cara pakai
1. Salin `gateway/secrets_example.h` menjadi `gateway/secrets.h`, lalu isi SSID, password, dan Write API Key.
2. Upload `sender/sender.ino` ke ESP32 pengirim.
3. Upload `gateway/gateway.ino` ke ESP32 penerima.
4. Kalibrasi pH: ubah `PH7_V` di sender sesuai tegangan Po pada larutan pH 7.

## Format paket LoRa
`ID,seq,pH,suhu,flow,total,busV,mA,status`
