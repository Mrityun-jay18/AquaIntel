/*
 * ============================================================================
 * AQUAINTEL - SMART AUTONOMOUS WATER TANK MONITORING SYSTEM
 * Complete Master Firmware & Interactive Neorphism Web Dashboard
 * ============================================================================
 * 
 * Pin Connections (All 4 Sensors Integrated):
 * 1. Ultrasonic Sensor (HC-SR04 / JSN-SR04T):
 *    - TRIG Pin  --> ESP32 GPIO 5
 *    - ECHO Pin  --> ESP32 GPIO 18
 *    - VCC Pin   --> ESP32 5V (VIN)
 *    - GND Pin   --> ESP32 GND
 * 
 * 2. E-201-C pH Sensor Probe Driver Board:
 *    - Po Pin   --> ESP32 GPIO 34 (Analog Input)
 *    - V+ Pin   --> ESP32 5V (VIN)
 *    - G Pin    --> ESP32 GND
 * 
 * 3. Waterproof DS18B20 Digital Temperature Sensor:
 *    - Data Wire (Yellow) --> ESP32 GPIO 4 (Needs 4.7k resistor to 3.3V)
 *    - VCC Wire (Red)     --> ESP32 3.3V
 *    - GND Wire (Black)   --> ESP32 GND
 * 
 * 4. Water Flow Sensor (YF-S201 / G1/2" Flow Meter):
 *    - Signal Wire (Yellow) --> ESP32 GPIO 16 (Hardware Interrupt Pin)
 *    - VCC Wire (Red)       --> ESP32 5V (VIN)
 *    - GND Wire (Black)     --> ESP32 GND
 * 
 * 5. Status Indicator LED:
 *    - Anode (+) --> ESP32 GPIO 2 (Onboard LED)
 * 
 * Access Web Dashboard:
 * 1. Connect Phone / Laptop to WiFi: "AQUAINTEL_WIFI" (Password: 12345678)
 * 2. Open browser to: http://192.168.4.1
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ============================================================================
// HARDWARE PIN DEFINITIONS
// ============================================================================
const int PIN_TRIG        = 5;   // Ultrasonic Trigger Output
const int PIN_ECHO        = 18;  // Ultrasonic Echo Input
const int PIN_PH_PO       = 34;  // pH Probe Analog Input
const int PIN_DS18B20     = 4;   // Temperature Data Pin
const int PIN_FLOW_SENSOR = 16;  // Water Flow Sensor Pulse Input (Interrupt)
const int PIN_LED         = 2;   // Status Alert LED

// ============================================================================
// TANK & CALIBRATION PARAMETERS
// ============================================================================
const float TANK_TOTAL_HEIGHT_CM   = 30.0f;  // Tank depth in cm
const float SENSOR_MIN_OFFSET_CM   = 4.0f;   // Air gap at 100% full
const float SOUND_SPEED_CM_PER_US  = 0.0343f;
const float TANK_CAPACITY_LITERS   = 1000.0f;
const float FLOW_CALIBRATION_FACTOR = 7.5f;   // YF-S201 (7.5 pulses/sec = 1 L/min)

float ph7Voltage = 1.128f; // Calibrated neutral baseline voltage

// Global Controllers
WebServer server(80);
OneWire oneWire(PIN_DS18B20);
DallasTemperature tempSensor(&oneWire);

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

// Embedded Neorphism Web Dashboard HTML/CSS/JS
const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>AQUAINTEL - Smart Water Tank Dashboard</title>
    <style>
        :root {
            --bg-color: #e0e5ec;
            --card-bg: #e0e5ec;
            --text-main: #31344b;
            --text-sub: #6c757d;
            --accent-blue: #0072ff;
            --accent-green: #00b894;
            --accent-red: #ff7675;
            --shadow-light: #ffffff;
            --shadow-dark: #a3b1c6;
        }

        * { box-sizing: border-box; margin: 0; padding: 0; font-family: 'Segoe UI', system-ui, sans-serif; }

        body {
            background-color: var(--bg-color);
            color: var(--text-main);
            min-height: 100vh;
            padding: 20px;
            display: flex;
            flex-direction: column;
            align-items: center;
        }

        .header { text-align: center; margin-bottom: 25px; }
        .header h1 { font-size: 2.2rem; font-weight: 700; color: #2c3e50; }
        .header p { font-size: 0.95rem; color: var(--text-sub); margin-top: 4px; }

        .dashboard-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
            gap: 25px;
            width: 100%;
            max-width: 1100px;
        }

        .neo-box {
            background: var(--card-bg);
            border-radius: 24px;
            box-shadow: 9px 9px 16px var(--shadow-dark), -9px -9px 16px var(--shadow-light);
            padding: 24px;
        }

        .tank-card {
            grid-row: span 2;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
        }

        .tank-container {
            width: 180px;
            height: 260px;
            border-radius: 30px;
            border: 6px solid #d1d9e6;
            position: relative;
            overflow: hidden;
            background: #dbe2ef;
            box-shadow: inset 5px 5px 10px var(--shadow-dark), inset -5px -5px 10px var(--shadow-light);
            margin: 15px 0;
        }

        .liquid-wave {
            position: absolute;
            bottom: 0;
            left: 0;
            width: 100%;
            height: 0%;
            background: linear-gradient(180deg, #00c6ff 0%, #0072ff 100%);
            transition: height 1s ease-in-out;
        }

        .liquid-wave::before, .liquid-wave::after {
            content: '';
            position: absolute;
            width: 200%;
            height: 200%;
            top: -150%;
            left: -50%;
            background: rgba(255, 255, 255, 0.4);
            border-radius: 40%;
            animation: waveRotate 6s linear infinite;
        }

        .liquid-wave::after {
            border-radius: 35%;
            background: rgba(255, 255, 255, 0.2);
            animation: waveRotate 10s linear infinite;
        }

        @keyframes waveRotate {
            0% { transform: rotate(0deg); }
            100% { transform: rotate(360deg); }
        }

        .tank-percent { font-size: 2.4rem; font-weight: 800; color: #0072ff; margin-top: 10px; }
        .metric-title { font-size: 0.9rem; text-transform: uppercase; color: var(--text-sub); font-weight: 600; }
        .metric-value { font-size: 2rem; font-weight: 700; color: var(--text-main); }

        .badge {
            display: inline-block;
            padding: 6px 14px;
            border-radius: 12px;
            font-size: 0.85rem;
            font-weight: 600;
            margin-top: 10px;
        }

        .badge-safe { background-color: #e3fcf7; color: #00b894; }
        .badge-acidic { background-color: #ffeaa7; color: #d63031; }
        .badge-alkaline { background-color: #dfe6e9; color: #0984e3; }

        .alert-banner {
            width: 100%;
            max-width: 1100px;
            margin-bottom: 20px;
            padding: 15px 20px;
            border-radius: 16px;
            font-weight: 600;
            display: none;
            align-items: center;
            gap: 12px;
            box-shadow: 6px 6px 12px var(--shadow-dark), -6px -6px 12px var(--shadow-light);
        }

        .alert-warning { background-color: #ff7675; color: white; display: flex; }

        .progress-bar-bg {
            width: 100%; height: 12px; background: #dbe2ef; border-radius: 10px; overflow: hidden; margin-top: 12px;
        }

        .progress-bar-fill {
            height: 100%; width: 0%; background: linear-gradient(90deg, #00b894, #0072ff); transition: width 1s ease;
        }

        .controls { margin-top: 25px; display: flex; gap: 15px; flex-wrap: wrap; justify-content: center; }

        .neo-btn {
            background: var(--card-bg);
            border: none;
            padding: 12px 24px;
            border-radius: 14px;
            box-shadow: 6px 6px 12px var(--shadow-dark), -6px -6px 12px var(--shadow-light);
            font-weight: 600;
            cursor: pointer;
        }
    </style>
</head>
<body>

    <div class="header">
        <h1>💧 AQUAINTEL Web Dashboard</h1>
        <p>Smart Autonomous Water Tank Monitoring & Footprint Telemetry</p>
    </div>

    <div id="alertBanner" class="alert-banner">
        <span id="alertIcon">🚨</span>
        <span id="alertMessage">System Nominal</span>
    </div>

    <div class="dashboard-grid">
        
        <!-- Tank Level Card -->
        <div class="neo-box tank-card">
            <div class="metric-title">Tank Fill Level</div>
            <div class="tank-container">
                <div id="waterWave" class="liquid-wave" style="height: 0%;"></div>
            </div>
            <div id="levelPct" class="tank-percent">0.0%</div>
            <div id="volumeLiters" style="font-size: 1.1rem; color: var(--text-sub); margin-top: 5px;">0.0 L / 1000 L</div>
            <div id="distanceCm" style="font-size: 0.85rem; color: #a0a0a0; margin-top: 4px;">Dist: 0.0 cm</div>
        </div>

        <!-- pH Card -->
        <div class="neo-box">
            <div class="metric-title">pH Water Quality</div>
            <div id="phVal" class="metric-value">7.00</div>
            <div id="phVolts" style="font-size: 0.85rem; color: var(--text-sub);">Voltage: 0.000 V</div>
            <div id="phBadge" class="badge badge-safe">SAFE DRINKING WATER</div>
        </div>

        <!-- Water Flow Rate Card -->
        <div class="neo-box">
            <div class="metric-title">Water Flow Speed</div>
            <div id="flowVal" class="metric-value">0.0 L/min</div>
            <div id="flowStatus" style="font-size: 0.85rem; color: var(--text-sub);">Status: IDLE</div>
            <div id="flowBadge" class="badge badge-safe">NO FLOW (NORMAL)</div>
        </div>

        <!-- Temp Card -->
        <div class="neo-box">
            <div class="metric-title">Water Temperature</div>
            <div id="tempVal" class="metric-value">25.0 °C</div>
            <div id="tempF" style="font-size: 0.85rem; color: var(--text-sub);">77.0 °F</div>
            <div id="tempBadge" class="badge badge-safe">OPTIMAL TEMP</div>
        </div>

        <!-- Footprint Facts Card -->
        <div class="neo-box" style="grid-column: span 2;">
            <div class="metric-title">Case 5: Water Footprint & Awareness</div>
            <div id="consumedText" style="font-size: 1.1rem; font-weight: 600; margin-bottom: 6px;">Total Consumed: 0.0 Liters</div>
            <div id="factText" style="font-size: 0.95rem; font-style: italic;">[RICE FACT] Calculating footprint...</div>
            <div class="progress-bar-bg">
                <div id="quotaProgress" class="progress-bar-fill" style="width: 0%;"></div>
            </div>
            <div id="quotaText" style="font-size: 0.8rem; color: var(--text-sub); margin-top: 6px; text-align: right;">0.0% of Indian Daily Quota (135L)</div>
        </div>

    </div>

    <div class="controls">
        <button class="neo-btn" onclick="calibratepH7()">🧪 Calibrate pH 7.00</button>
        <button class="neo-btn" onclick="resetUsageStats()">🔄 Reset Usage Stats</button>
    </div>

    <script>
        async function fetchTelemetry() {
            try {
                const response = await fetch('/api/data');
                const data = await response.json();

                const pct = data.waterLevelPercent.toFixed(1);
                document.getElementById('levelPct').innerText = pct + '%';
                document.getElementById('waterWave').style.height = pct + '%';
                document.getElementById('volumeLiters').innerText = data.currentLiters.toFixed(1) + ' L / ' + data.tankCapacity.toFixed(0) + ' L';
                
                if (data.isLevelValid) {
                    document.getElementById('distanceCm').innerText = 'Dist: ' + data.distanceCM.toFixed(1) + ' cm';
                } else {
                    document.getElementById('distanceCm').innerText = '[Ultrasonic Timeout]';
                }

                document.getElementById('phVal').innerText = data.pH.toFixed(2);
                document.getElementById('phVolts').innerText = 'Voltage: ' + data.phVoltage.toFixed(3) + ' V';
                
                const phBadge = document.getElementById('phBadge');
                if (data.pH < 6.5) {
                    phBadge.innerText = '🍋 ACIDIC WATER'; phBadge.className = 'badge badge-acidic';
                } else if (data.pH <= 8.5) {
                    phBadge.innerText = '💧 SAFE DRINKING WATER'; phBadge.className = 'badge badge-safe';
                } else {
                    phBadge.innerText = '🧼 ALKALINE / BASIC'; phBadge.className = 'badge badge-alkaline';
                }

                document.getElementById('flowVal').innerText = data.flowRateLmin.toFixed(1) + ' L/min';
                const flowBadge = document.getElementById('flowBadge');
                if (data.flowRateLmin > 0.1) {
                    flowBadge.innerText = '🌊 ACTIVE FLOWING'; flowBadge.className = 'badge badge-safe';
                    document.getElementById('flowStatus').innerText = 'Status: TAP / PUMP ACTIVE';
                } else {
                    flowBadge.innerText = '💤 NO FLOW (IDLE)'; flowBadge.className = 'badge badge-alkaline';
                    document.getElementById('flowStatus').innerText = 'Status: IDLE';
                }

                document.getElementById('tempVal').innerText = data.temperatureC.toFixed(1) + ' °C';
                document.getElementById('tempF').innerText = data.temperatureF.toFixed(1) + ' °F';

                const banner = document.getElementById('alertBanner');
                if (data.overflowAlert) {
                    banner.className = 'alert-banner alert-warning';
                    document.getElementById('alertIcon').innerText = '🚨';
                    document.getElementById('alertMessage').innerText = 'OVERFLOW WARNING: Water level is 95%+ Full!';
                } else if (data.maintAlert) {
                    banner.className = 'alert-banner alert-warning';
                    document.getElementById('alertIcon').innerText = '⚠️';
                    document.getElementById('alertMessage').innerText = 'MAINTENANCE REQUIRED: Water pH or Temp out of safe range!';
                } else {
                    banner.style.display = 'none';
                }

                document.getElementById('consumedText').innerText = 'Total Consumed: ' + data.consumedLiters.toFixed(1) + ' Liters';
                document.getElementById('factText').innerText = data.factMessage;
                document.getElementById('quotaProgress').style.width = Math.min(100, data.indianQuotaPercent).toFixed(1) + '%';
                document.getElementById('quotaText').innerText = data.indianQuotaPercent.toFixed(1) + '% of Indian Daily Quota (135L)';
            } catch (err) {}
        }

        async function calibratepH7() {
            await fetch('/api/cal_ph7');
            fetchTelemetry();
        }

        async function resetUsageStats() {
            await fetch('/api/reset_usage');
            fetchTelemetry();
        }

        setInterval(fetchTelemetry, 1000);
        fetchTelemetry();
    </script>
</body>
</html>
)rawliteral";

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

    // Start Access Point
    WiFi.mode(WIFI_AP);
    WiFi.softAP("AQUAINTEL_WIFI", "12345678");
    IPAddress apIP = WiFi.softAPIP();

    Serial.println(F("\n========================================================"));
    Serial.println(F("    AQUAINTEL - SMART AUTONOMOUS WATER TANK SYSTEM     "));
    Serial.print(F("[WIFI AP LOG] Access Point 'AQUAINTEL_WIFI' Started!\n"));
    Serial.print(F("[WIFI AP LOG] Open Web Dashboard at: http://"));
    Serial.println(apIP);
    Serial.println(F("========================================================\n"));

    server.on("/", []() { server.send(200, "text/html", DASHBOARD_HTML); });

    server.on("/api/data", []() {
        float distance = getFilteredDistanceCM(5);
        float waterPct = 0.0f;
        float liters = 0.0f;
        bool isLevelValid = false;

        if (distance > 0.0f && distance <= (TANK_TOTAL_HEIGHT_CM + 10.0f)) {
            isLevelValid = true;
            float usableHeight = TANK_TOTAL_HEIGHT_CM - SENSOR_MIN_OFFSET_CM;
            if (usableHeight <= 0.0f) usableHeight = 1.0f;
            float waterColumnCM = TANK_TOTAL_HEIGHT_CM - distance;
            waterPct = constrain((waterColumnCM / usableHeight) * 100.0f, 0.0f, 100.0f);
            liters = (waterPct / 100.0f) * TANK_CAPACITY_LITERS;
        }

        // Temperature Read
        tempSensor.requestTemperatures();
        float tempC = tempSensor.getTempCByIndex(0);
        if (tempC == DEVICE_DISCONNECTED_C || tempC < -40.0f || tempC > 125.0f) tempC = 25.0f;

        // pH Read
        uint32_t phSum = 0;
        for (int i = 0; i < 64; i++) { phSum += analogRead(PIN_PH_PO); delayMicroseconds(100); }
        float phVolts = ((phSum / 64.0f) / 4095.0f) * 3.3f;
        float uncompPh = 7.00f + ((phVolts - ph7Voltage) * 3.5f);
        float phVal = constrain(uncompPh + (0.0033f * (tempC - 25.0f) * (7.00f - uncompPh)), 0.0f, 14.0f);

        // Case 5 Fact Message
        char factBuf[256];
        factIndex = (factIndex + 1) % 5;
        switch (factIndex) {
            case 0: snprintf(factBuf, sizeof(factBuf), "[RICE FACT] Spent %.1fL! Enough water for %.1fg of Paddy Rice.", cumulativeConsumedLiters, cumulativeConsumedLiters * 0.4f); break;
            case 1: snprintf(factBuf, sizeof(factBuf), "[CHAI FACT] Usage of %.1fL equals virtual water of %.1f cups of Indian Chai!", cumulativeConsumedLiters, cumulativeConsumedLiters / 30.0f); break;
            case 2: snprintf(factBuf, sizeof(factBuf), "[WHEAT FACT] Spending %.1fL equals water footprint for %.1f Wheat Rotis.", cumulativeConsumedLiters, cumulativeConsumedLiters / 50.0f); break;
            case 3: snprintf(factBuf, sizeof(factBuf), "[INDIAN BENCHMARK] Spent %.1fL (%.1f%% of NITI Aayog daily quota of 135L).", cumulativeConsumedLiters, (cumulativeConsumedLiters / 135.0f) * 100.0f); break;
            default: snprintf(factBuf, sizeof(factBuf), "[SHOWER FACT] Spending %.1fL equals running a shower for %.1f minutes.", cumulativeConsumedLiters, cumulativeConsumedLiters / 10.0f); break;
        }

        bool isOverflow = (isLevelValid && waterPct >= 95.0f);
        bool isMaint = (phVal < 6.5f || phVal > 8.5f || tempC > 35.0f);

        String json = "{";
        json += "\"waterLevelPercent\":" + String(waterPct, 1) + ",";
        json += "\"currentLiters\":" + String(liters, 1) + ",";
        json += "\"tankCapacity\":" + String(TANK_CAPACITY_LITERS, 0) + ",";
        json += "\"distanceCM\":" + String(distance, 1) + ",";
        json += "\"isLevelValid\":" + String(isLevelValid ? "true" : "false") + ",";
        json += "\"pH\":" + String(phVal, 2) + ",";
        json += "\"phVoltage\":" + String(phVolts, 3) + ",";
        json += "\"flowRateLmin\":" + String(currentFlowRateLmin, 1) + ",";
        json += "\"temperatureC\":" + String(tempC, 1) + ",";
        json += "\"temperatureF\":" + String((tempC * 1.8f) + 32.0f, 1) + ",";
        json += "\"overflowAlert\":" + String(isOverflow ? "true" : "false") + ",";
        json += "\"maintAlert\":" + String(isMaint ? "true" : "false") + ",";
        json += "\"consumedLiters\":" + String(cumulativeConsumedLiters, 1) + ",";
        json += "\"indianQuotaPercent\":" + String((cumulativeConsumedLiters / 135.0f) * 100.0f, 1) + ",";
        String fMsg = String(factBuf); fMsg.replace("\"", "\\\"");
        json += "\"factMessage\":\"" + fMsg + "\"";
        json += "}";

        server.send(200, "application/json", json);
    });

    server.on("/api/cal_ph7", []() {
        uint32_t sum = 0;
        for (int i = 0; i < 64; i++) { sum += analogRead(PIN_PH_PO); delay(2); }
        ph7Voltage = (sum / 64.0f / 4095.0f) * 3.3f;
        server.send(200, "application/json", "{\"status\":\"ok\",\"ph7Voltage\":" + String(ph7Voltage, 3) + "}");
    });

    server.on("/api/reset_usage", []() {
        cumulativeConsumedLiters = 0.0f;
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server.begin();
}

void loop() {
    server.handleClient();

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
        float waterPct = 0.0f;
        float liters = 0.0f;
        bool isLevelValid = false;

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

        Serial.println(F("--------------------------------------------------"));
        Serial.print(F("ULTRASONIC DIST : "));
        if (isLevelValid) { Serial.print(distance, 1); Serial.println(F(" cm")); }
        else { Serial.println(F("[ERROR: Echo Timeout]")); }

        Serial.print(F("WATER LEVEL     : ")); Serial.print(waterPct, 1); Serial.print(F(" %  ("));
        Serial.print(liters, 1); Serial.println(F(" Liters)"));

        Serial.print(F("FLOW SPEED      : ")); Serial.print(currentFlowRateLmin, 1); Serial.println(F(" L/min"));
        Serial.print(F("WATER TEMP      : ")); Serial.print(tempC, 1); Serial.println(F(" deg C"));
        Serial.print(F("pH VALUE        : ")); Serial.print(phVal, 2); Serial.print(F(" [V: ")); Serial.print(phVolts, 3); Serial.println(F("V]"));

        Serial.print(F("SYSTEM STATUS   : "));
        if (isLevelValid && waterPct >= 95.0f) {
            Serial.println(F("🚨 OVERFLOW WARNING (95%+ FULL!)"));
            digitalWrite(PIN_LED, HIGH);
        } else if (phVal < 6.5f || phVal > 8.5f) {
            Serial.println(F("⚠️ MAINTENANCE REQ (pH Out of Range)"));
            digitalWrite(PIN_LED, HIGH);
        } else {
            Serial.println(F("OK (All Metrics Nominal)"));
            digitalWrite(PIN_LED, LOW);
        }
        Serial.println(F("--------------------------------------------------\n"));
    }
}
