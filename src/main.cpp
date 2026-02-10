#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NAU7802.h>

Adafruit_NAU7802 nau;

// ---------------- USER-DEFINED CONSTANTS ----------------
uint32_t USER_DURATION_S = 60;        // seconds (set once in setup)
float    USER_CAL_WEIGHT = 0.0f;     // known calibration weight (units in gram)

// ---------------- CONFIG ----------------
const uint32_t SAMPLE_RATE_HZ = 320; // Fastest NAU7802 sample rate
const uint32_t TOTAL_DURATION_S = 40; // seconds
const uint32_t TOTAL_DURATION_MS = TOTAL_DURATION_S * 1000UL; // milliseconds
const uint32_t TOTAL_SAMPLES = SAMPLE_RATE_HZ * TOTAL_DURATION_S; // total samples collected
const uint32_t MAX_DURATION_S = 60; // Maximum allowed duration (seconds)

// ---------------- STORAGE ----------------
uint32_t time_ms[TOTAL_SAMPLES];
int32_t  raw[TOTAL_SAMPLES];

uint32_t sampleIndex = 0;

bool started = false;
uint32_t runStart = 0;

bool finished = false;

// ---------------- CALIBRATION MATH ---------------- (y = mx + b) to convert raw NAU7802 counts to grams
float strain_offset = 415043.1887; // NEEDS TO CHANGE BASED ON YOUR LOAD CELL AND CALIBRATION WEIGHT! (units in raw NAU7802 counts)
float strain_slope_pos = 208.5548782;// NEEDS TO CHANGE BASED ON YOUR LOAD CELL AND CALIBRATION WEIGHT! (units in raw NAU7802 counts per gram)
float strain_slope_neg = 209.6062518; // NEEDS TO CHANGE BASED ON YOUR LOAD CELL AND CALIBRATION WEIGHT! (units in raw NAU7802 counts per gram)
uint32_t n = 0; // number of samples collected (for calibration math)
float rtotal = 0;
uint32_t m = 0; // number of modified weight samples collected (for calibration math)
float mtotal = 0; // total modified weight (for calibration math)


// helper to wait for a line of input (non-deadlocking)
static void waitForUserInputLine() {   
  while (!Serial.available()) {        
    delay(10);                         // yields time to USB/Serial)
  }
}

// helper to prompt user for duration + cal weight (re-used every run)
static void promptUserConfig() {                    
  Serial.println("Enter duration (seconds): ");     
  waitForUserInputLine();                           
  USER_DURATION_S = Serial.parseInt();              
  Serial.readStringUntil('\n');                     // (clear rest of line)

  if (USER_DURATION_S == 0) USER_DURATION_S = 1; 
  if (USER_DURATION_S > MAX_DURATION_S) USER_DURATION_S = MAX_DURATION_S;

  delay(200);                                      // (small buffer clear delay)

  Serial.println("Enter calibration weight (in grams): ");
  waitForUserInputLine();                           
  USER_CAL_WEIGHT = Serial.parseFloat();            
  Serial.readStringUntil('\n');                     // (clear rest of line)

  Serial.println("CONFIG RECEIVED");                
  Serial.print("Duration (s): ");                   
  Serial.println(USER_DURATION_S);                  
  Serial.print("Calibration weight: ");             
  Serial.println(USER_CAL_WEIGHT);                  
}

// helper to ask user if they want to run again
static bool askRunAgain() {                         
  Serial.println("Run again? (y/n): ");             
  waitForUserInputLine();                           
  String ans = Serial.readStringUntil('\n');        
  ans.trim();                                       
  ans.toLowerCase();                                
  return (ans.startsWith("y"));                     
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);

  // Wait for Serial to be ready
  while (!Serial && millis() < 3000) {}


  // IMPORTANT NOTE: The following block of code is meant to be commented out if you want to use the live plot Python script
  //-----------------------------------------------------------------------------------------------------------------------
  // --- Comment out the following if you wanna use live plot

  delay(500); // Small delay to ensure Serial is ready before prompting user

  Serial.println("Enter duration (seconds): ");
  waitForUserInputLine(); 
  USER_DURATION_S = Serial.parseInt();
  Serial.readStringUntil('\n'); // Clear the rest of the line


  if (USER_DURATION_S == 0) USER_DURATION_S = 1;
  if (USER_DURATION_S > MAX_DURATION_S) USER_DURATION_S = MAX_DURATION_S;

  delay(500); // Small delay to ensure Serial buffer is clear before next prompt
  
  
  Serial.println("Enter calibration weight (in grams): ");
  waitForUserInputLine();
  USER_CAL_WEIGHT = Serial.parseFloat();
  Serial.readStringUntil('\n'); // Clear the rest of the line

  Serial.println("CONFIG RECEIVED");
  Serial.print("Duration (s): ");
  Serial.println(USER_DURATION_S);
  Serial.print("Calibration weight: ");
  Serial.println(USER_CAL_WEIGHT);

  // End of comment out block for Python live plot (loadcell.liveplot.py)
  //-----------------------------------------------------------------------------------------------------------------------


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
  Serial.println("[Starting in 1 second...]");
  delay(1000); // Give user a moment to see "READY" before data starts flowing
  Serial.println("START");
}

// ------------- LOOP ----------------
void loop() {

  if (finished) {                                   
    bool again = askRunAgain();                     
    if (!again) {                                   
      Serial.println("Stopping. (Reset board to start again)");
      while (1) {        // (idle forever)
        delay(1000); 
      }
    }

    // user chose to run again -> re-prompt config + reset run state
    promptUserConfig();                              
    finished = false;                                
    started  = false;                                
    n = 0;      
    m = 0;                                     
    rtotal = 0;
    mtotal = 0;                                       

    Serial.println("[Starting in 1 second...]");     
    delay(1000);                                     
    Serial.println("START"); 
  }                        

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
    float modified_weight = 0;

    // ---- THIS LINE FORMAT MATCHES PYTHON PARSER ----
    // Must be whitespace-separated tokens: "X:" <num> "Y:" <num>
    Serial.print("time: ");
    Serial.print(t_s, 6);
    Serial.print(" raw: ");
    Serial.print((float)v);  // print as float so Python float() always works
    Serial.print(" modified_weight: ");

    // modified weight calculator
    float delta = (float)v - strain_offset;          // use delta from offset (not sign of v)

    if (delta >= 0) {                                // branch on delta sign
      modified_weight = delta / strain_slope_pos;    // (v - offset)/slope_pos
    } else {
      modified_weight = delta / strain_slope_neg;    // still (v - offset), keeps negative values negative
    }

    Serial.println(modified_weight, 6);  // print as float so Python float() always works
    
    n++;
    rtotal += v;

    m++;
    mtotal += modified_weight;
    
    
    if(t_s >= (float)USER_DURATION_S) {
      finished = true;

      // printing test results
      float average = rtotal / n;
      Serial.println("\n ----- TEST RESULTS ----- \n");
      Serial.print("Average raw value: ");
      Serial.println(average, 6);
      float average_modified = mtotal / m;
      Serial.print("Average modified weight: ");
      Serial.println(average_modified, 6);
      Serial.print("% error (modified weight vs cal weight): ");
      float error_percent = 100.0f * (fabs(average_modified) - fabs(USER_CAL_WEIGHT)) / USER_CAL_WEIGHT;
      Serial.println(error_percent, 2);
      Serial.println("\n ----- CALIBRATION INFO ----- \n");

      // printing test parameters info for reference
      Serial.print("Calibration weight (g): ");
      Serial.println(USER_CAL_WEIGHT, 6);
      Serial.print("Calculated strain offset: ");
      Serial.println(strain_offset, 6);
      Serial.print("Calculated strain slope (pos): ");
      Serial.println(strain_slope_pos, 6);
      Serial.print("Calculated strain slope (neg): ");
      Serial.println(strain_slope_neg, 6);

      // Serial.print("Predicted weight (g): ");
      // float predicted_weight = (average - strain_offset) / strain_slope; // <<< NEW: apply calibration math
      // Serial.println(predicted_weight, 6);
      Serial.println("\nTest Done \n");
    }
  }
}
