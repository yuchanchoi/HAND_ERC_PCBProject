#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NAU7802.h>

Adafruit_NAU7802 nau;

// ---------------- CONFIG ----------------
const uint32_t SAMPLE_RATE_HZ = 320; // Fastest NAU7802 sample rate
const uint32_t TOTAL_DURATION_S = 20; // seconds
const uint32_t TOTAL_DURATION_MS = TOTAL_DURATION_S * 1000UL; // milliseconds
const uint32_t TOTAL_SAMPLES = SAMPLE_RATE_HZ * TOTAL_DURATION_S; // total samples collected

// ---------------- STORAGE ----------------
uint32_t time_ms[TOTAL_SAMPLES];
int32_t  raw[TOTAL_SAMPLES];

uint32_t sampleIndex = 0;

bool started = false;
uint32_t runStart = 0;

bool finished = false;

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);

  // Wait for Serial to be ready
  while (!Serial && millis() < 3000) {}

  // Initialize and begin I2C on NAU7802
  Wire.begin();
  if (!nau.begin()) {
    Serial.println("ERROR: NAU7802 not detected");
    while (1) {}
  }
  
  // Setting NAU7802 parameters
  nau.setGain(NAU7802_GAIN_128);
  nau.setRate(NAU7802_RATE_320SPS);
  nau.calibrate(NAU7802_CALMOD_INTERNAL);

  Serial.println("READY");
}

// ------------- LOOP ----------------
void loop() {

  if(finished) {
    return;
  }

  if (!started) {
    started = true;
    runStart = millis();
  }

  if (nau.available()) {
    uint32_t t_ms = millis() - runStart;     // time since start of run
    float t_s = t_ms / 1000.0f;              // seconds for nicer plotting
    int32_t v = nau.read();

    // ---- THIS LINE FORMAT MATCHES YOUR PYTHON PARSER ----
    // Must be whitespace-separated tokens: "X:" <num> "Y:" <num>
    Serial.print("time: ");
    Serial.print(t_s, 6);
    Serial.print(" raw: ");
    Serial.println((float)v);  // print as float so Python float() always works

    if(t_s >= TOTAL_DURATION_MS / 1000.0f) {
      finished = true;
      Serial.println("DONE");
    }
  }

}
