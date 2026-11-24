// Tes flow meter YF-S201 di ESP32
// Sinyal (kuning) -> pembagi tegangan 10k/20k -> GPIO25
#define FLOW_PIN 25
#define CAL_FACTOR 7.5   // YF-S201: F(Hz) = 7.5 * Q(L/min)

volatile unsigned long pulses = 0;

void IRAM_ATTR onPulse() { pulses++; }

void setup() {
  Serial.begin(115200);
  pinMode(FLOW_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), onPulse, RISING);
  Serial.println("Flow test siap. Alirkan/tiup air lewat sensor.");
}

void loop() {
  noInterrupts(); pulses = 0; interrupts();
  delay(1000);
  noInterrupts(); unsigned long p = pulses; interrupts();

  float flowLmin = p / CAL_FACTOR;
  Serial.printf("[Flow] pulsa/detik=%lu  debit=%.2f L/min\n", p, flowLmin);
}
