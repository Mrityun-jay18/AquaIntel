/*
 * ============================================================================
 * AQUAINTEL - SMART AUTONOMOUS WATER TANK MONITORING SYSTEM
 * Master Dedicated Bluetooth Firmware for Mobile Application
 * ============================================================================
 * 
 * Pin Connections Verified:
 * 1. Ultrasonic Level Sensor:
 *    - TRIG Pin  --> ESP32 GPIO 5
 *    - ECHO Pin  --> ESP32 GPIO 18 (via 1kΩ / 2kΩ Voltage Divider)
 * 2. Status Indicator LED:
 *    - Anode (+) --> ESP32 GPIO 2 (via 220Ω Resistor)
 * 3. Waterproof DS18B20 Temp Sensor:
 *    - Yellow (Data) --> ESP32 GPIO 4 (with 4.7kΩ Pull-up Resistor to 3.3V)
 *    - Red (VCC)     --> ESP32 3.3V
 *    - Black (GND)   --> ESP32 GND
 * 4. E-201-C pH Sensor Probe:
 *    - Po Pin   --> ESP32 GPIO 32 (Analog Input)
 *    - V+ Pin   --> ESP32 5V (VIN)
 *    - G Pin    --> ESP32 GND
 * 5. YF-S201 Water Flow Rate Sensor:
 *    - Yellow (Signal) --> ESP32 GPIO 16 (Hardware Interrupt Pin)
 *    - Red (VCC)       --> ESP32 5V (VIN)
 *    - Black (GND)     --> ESP32 GND
 */

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <BluetoothSerial.h>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please enable it in Arduino IDE settings.
#endif

// ============================================================================
// HARDWARE PIN DEFINITIONS
// ============================================================================
const int PIN_TRIG        = 5;   // Ultrasonic Trigger Output
const int PIN_ECHO        = 18;  // Ultrasonic Echo Input
const int PIN_PH_PO       = 32;  // pH Probe Analog Input (GPIO 32)
const int PIN_DS18B20     = 4;   // Temperature Data Pin (GPIO 4)
const int PIN_FLOW_SENSOR = 16;  // Water Flow Sensor Interrupt (GPIO 16)
const int PIN_LED         = 2;   // Status Alert LED (GPIO 2)

// ============================================================================
// TANK & CALIBRATION PARAMETERS
// ============================================================================
const float TANK_TOTAL_HEIGHT_CM   = 30.0f;  // Tank depth in cm
const float SENSOR_MIN_OFFSET_CM   = 4.0f;   // Air gap at 100% full
const float SOUND_SPEED_CM_PER_US  = 0.0343f;
const float TANK_CAPACITY_LITERS   = 1000.0f;
const float FLOW_CALIBRATION_FACTOR = 7.5f;   // YF-S201

float ph7Voltage = 1.128f; // Calibrated neutral baseline voltage

// Global Controllers
OneWire oneWire(PIN_DS18B20);
DallasTemperature tempSensor(&oneWire);
BluetoothSerial SerialBT;

// State Variables
volatile uint32_t flowPulseCount = 0;
float currentFlowRateLmin = 0.0f;
float cumulativeConsumedLiters = 0.0f;
float previousWaterLevelPct = -1.0f;
uint8_t factIndex = 0;
unsigned long lastUpdateTimestamp = 0;

// Interrupt Service Routine (ISR) for Flow Sensor Pulses
void IRAM_ATTR pulseCounterISR() {
    flowPulseCount++;
}

// ULTRASONIC DRIVER
float readRawDistanceCM() {
    digitalWrite(PIN_TRIG, LOW); delayMicroseconds(2);
    digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);

    long durationUS = pulseIn(PIN_ECHO, HIGH, 30000);
    if (durationUS <= 0) return -1.0f;

    float distance = (durationUS * SOUND_SPEED_CM_PER_US) / 2.0f;
    if (distance < 1.0f || distance > 400.0f) return -1.0f;
    return distance;
}

float getFilteredDistanceCM(int samples = 5) {
    float sum = 0.0f; int validCount = 0;
    for (int i = 0; i < samples; i++) {
        float d = readRawDistanceCM();
        if (d > 0.0f) { sum += d; validCount++; }
        delay(15);
    }
    if (validCount == 0) return -1.0f;
    return sum / (float)validCount;
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(PIN_TRIG, OUTPUT);
    pinMode(PIN_ECHO, INPUT_PULLDOWN);
    digitalWrite(PIN_TRIG, LOW);
    pinMode(PIN_LED, OUTPUT);
    
    // Water Flow Sensor Interrupt Setup
    pinMode(PIN_FLOW_SENSOR, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_FLOW_SENSOR), pulseCounterISR, RISING);

    analogSetAttenuation(ADC_11db);
    tempSensor.begin();

    // Start Bluetooth Classic SPP Serial ("AQUAINTEL_BT")
    SerialBT.begin("AQUAINTEL_BT");

    Serial.println(F("\n========================================================"));
    Serial.println(F(" AQUAINTEL - DEDICATED BLUETOOTH MOBILE APP FIRMWARE   "));
    Serial.println(F("[BLUETOOTH LOG] Bluetooth Serial 'AQUAINTEL_BT' Started!"));
    Serial.println(F("Pair your Mobile App with 'AQUAINTEL_BT' to start telemetry"));
    Serial.println(F("========================================================\n"));
}

void loop() {
    // Check for Inbound Bluetooth Commands from Mobile App
    if (SerialBT.available()) {
        String btCmd = SerialBT.readStringUntil('\n');
        btCmd.trim();
        if (btCmd == "CAL_PH7") {
            uint32_t sum = 0;
            for (int i = 0; i < 64; i++) { sum += analogRead(PIN_PH_PO); delay(2); }
            ph7Voltage = (sum / 64.0f / 4095.0f) * 3.3f;
            SerialBT.println("{\"status\":\"ok\",\"ph7Voltage\":" + String(ph7Voltage, 3) + "}");
        } else if (btCmd == "RESET_USAGE") {
            cumulativeConsumedLiters = 0.0f;
            SerialBT.println("{\"status\":\"ok\"}");
        }
    }

    unsigned long currentMillis = millis();
    if (currentMillis - lastUpdateTimestamp >= 1000) {
        lastUpdateTimestamp = currentMillis;

        // Calculate Flow Rate L/min from Hardware Interrupt Pulses
        noInterrupts();
        uint32_t pulses = flowPulseCount;
        flowPulseCount = 0;
        interrupts();

        currentFlowRateLmin = ((float)pulses) / FLOW_CALIBRATION_FACTOR;
        cumulativeConsumedLiters += (currentFlowRateLmin / 60.0f);

        float distance = getFilteredDistanceCM(5);
        float waterPct = 0.0f; float liters = 0.0f; bool isLevelValid = false;
        if (distance > 0.0f && distance <= (TANK_TOTAL_HEIGHT_CM + 10.0f)) {
            isLevelValid = true;
            float usableHeight = TANK_TOTAL_HEIGHT_CM - SENSOR_MIN_OFFSET_CM;
            if (usableHeight <= 0.0f) usableHeight = 1.0f;
            float waterColumnCM = TANK_TOTAL_HEIGHT_CM - distance;
            waterPct = constrain((waterColumnCM / usableHeight) * 100.0f, 0.0f, 100.0f);
            liters = (waterPct / 100.0f) * TANK_CAPACITY_LITERS;
        }

        tempSensor.requestTemperatures();
        float tempC = tempSensor.getTempCByIndex(0);
        if (tempC == DEVICE_DISCONNECTED_C || tempC < -40.0f || tempC > 125.0f) tempC = 25.0f;

        uint32_t phSum = 0;
        for (int i = 0; i < 64; i++) { phSum += analogRead(PIN_PH_PO); delayMicroseconds(100); }
        float phVolts = ((phSum / 64.0f) / 4095.0f) * 3.3f;
        float uncompPh = 7.00f + ((phVolts - ph7Voltage) * 3.5f);
        float phVal = constrain(uncompPh + (0.0033f * (tempC - 25.0f) * (7.00f - uncompPh)), 0.0f, 14.0f);

        // Case Alert Flags for Bluetooth Mobile Push Notifications
        bool isOverflow = (isLevelValid && waterPct >= 95.0f);
        bool isMaint = (phVal < 6.5f || phVal > 8.5f || tempC > 35.0f);
        bool isLeak = (previousWaterLevelPct > 0.0f && (previousWaterLevelPct - waterPct) >= 2.0f && currentFlowRateLmin == 0.0f);

        String alertTitle = "NOMINAL";
        String alertMsg = "All metrics nominal";

        if (isOverflow) {
            alertTitle = "🚨 OVERFLOW WARNING";
            alertMsg = "Tank is 95%+ FULL! Turn off water pump immediately.";
            digitalWrite(PIN_LED, HIGH);
        } else if (isLeak) {
            alertTitle = "💧 LEAKAGE ALERT";
            alertMsg = "Drastic water level drop detected! Check for tank pipe leak.";
            digitalWrite(PIN_LED, HIGH);
        } else if (isMaint) {
            alertTitle = "⚠️ MAINTENANCE REQUIRED";
            alertMsg = "Water pH or Temperature out of safe bounds!";
            digitalWrite(PIN_LED, HIGH);
        } else {
            digitalWrite(PIN_LED, LOW);
        }

        if (isLevelValid) previousWaterLevelPct = waterPct;

        char factBuf[128];
        factIndex = (factIndex + 1) % 4;
        if (factIndex == 0) snprintf(factBuf, sizeof(factBuf), "[RICE] Spent %.1fL! Water for %.1fg Paddy Rice.", cumulativeConsumedLiters, cumulativeConsumedLiters * 0.4f);
        else if (factIndex == 1) snprintf(factBuf, sizeof(factBuf), "[CHAI] Spent %.1fL! Equals %.1f cups of Chai.", cumulativeConsumedLiters, cumulativeConsumedLiters / 30.0f);
        else if (factIndex == 2) snprintf(factBuf, sizeof(factBuf), "[WHEAT] Spent %.1fL! Water for %.1f Rotis.", cumulativeConsumedLiters, cumulativeConsumedLiters / 50.0f);
        else snprintf(factBuf, sizeof(factBuf), "[QUOTA] Spent %.1fL! (%.1f%% of 135L Indian Quota).", cumulativeConsumedLiters, (cumulativeConsumedLiters / 135.0f) * 100.0f);

        String json = "{";
        json += "\"waterLevelPercent\":" + String(waterPct, 1) + ",";
        json += "\"currentLiters\":" + String(liters, 1) + ",";
        json += "\"tankCapacity\":" + String(TANK_CAPACITY_LITERS, 0) + ",";
        json += "\"distanceCM\":" + String(distance, 1) + ",";
        json += "\"isLevelValid\":" + String(isLevelValid ? "true" : "false") + ",";
        json += "\"pH\":" + String(phVal, 2) + ",";
        json += "\"flowRateLmin\":" + String(currentFlowRateLmin, 1) + ",";
        json += "\"temperatureC\":" + String(tempC, 1) + ",";
        json += "\"consumedLiters\":" + String(cumulativeConsumedLiters, 1) + ",";
        json += "\"indianQuotaPercent\":" + String((cumulativeConsumedLiters / 135.0f) * 100.0f, 1) + ",";
        json += "\"overflowAlert\":" + String(isOverflow ? "true" : "false") + ",";
        json += "\"maintAlert\":" + String(isMaint ? "true" : "false") + ",";
        json += "\"leakAlert\":" + String(isLeak ? "true" : "false") + ",";
        json += "\"alertTitle\":\"" + alertTitle + "\",";
        json += "\"alertMessage\":\"" + alertMsg + "\",";
        String fMsg = String(factBuf); fMsg.replace("\"", "\\\"");
        json += "\"factMessage\":\"" + fMsg + "\"";
        json += "}";

        // Broadcast JSON Telemetry over Bluetooth Serial to Mobile App
        SerialBT.println(json);

        // Serial Monitor Logging
        Serial.println(F("--------------------------------------------------"));
        Serial.print(F("WATER LEVEL     : ")); Serial.print(waterPct, 1); Serial.print(F(" %  ("));
        Serial.print(liters, 1); Serial.println(F(" Liters)"));
        Serial.print(F("FLOW SPEED      : ")); Serial.print(currentFlowRateLmin, 1); Serial.println(F(" L/min"));
        Serial.print(F("WATER TEMP      : ")); Serial.print(tempC, 1); Serial.println(F(" deg C"));
        Serial.print(F("pH VALUE        : ")); Serial.print(phVal, 2); Serial.println(F(""));
        Serial.print(F("SYSTEM STATUS   : ")); Serial.println(alertTitle);
        Serial.println(F("--------------------------------------------------\n"));
    }
}
