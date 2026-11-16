/*
 * TremorGuard — ESP32-S3 XIAO
 * Serves the TremorCare app over HTTP (port 80)
 * AND streams MPU-6050 IMU data over WebSocket (port 81)
 *
 * Phone workflow:
 *   1. ESP32 connects to your hotspot
 *   2. Serial Monitor shows: "IP: 192.168.x.x"
 *   3. Phone (on same hotspot) opens: http://192.168.x.x
 *   4. App loads → tap "Connect ESP32" → ws://192.168.x.x:81 auto-filled
 *
 * Libraries needed (install via Arduino Library Manager):
 *   - ArduinoWebsockets  by Gil Maimon
 *   - WebServer          (built-in ESP32 core)
 *   - MPU6050            by Electronic Cats  (or Adafruit MPU6050)
 *   - Wire               (built-in)
 *
 * Board: "XIAO_ESP32S3" in ESP32 Arduino core
 */

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>   // from arduinoWebSockets library
#include <Wire.h>
#include <MPU6050.h>

// ── WiFi credentials ──────────────────────────────────
const char* SSID     = "iPhone";
const char* PASSWORD = "lifetime18";

// ── Servers ───────────────────────────────────────────
WebServer        httpServer(80);
WebSocketsServer wsServer(81);

// ── MPU-6050 ──────────────────────────────────────────
MPU6050 mpu;
const float ACCEL_SCALE = 16384.0f;   // ±2g range → LSB/g
const float G_TO_MS2    = 9.80665f;

// ── Tremor detection ──────────────────────────────────
// Simple RMS threshold on Z-axis over a rolling window
const int   WINDOW_SIZE   = 20;      // samples (~200ms at 100Hz)
const float TREMOR_THRESH = 0.35f;   // m/s² RMS threshold
float zBuf[WINDOW_SIZE];
int   zIdx = 0;
bool  tremoring = false;
unsigned long tremorStart = 0;

// ── Motor (haptic feedback) ───────────────────────────
const int MOTOR_PIN       = D3;      // PWM-capable pin on XIAO ESP32S3
const int MOTOR_DELAY_MS  = 1000;    // 1 second after tremor detection
bool  motorScheduled = false;
unsigned long motorFireAt = 0;

// ── Sampling rate ─────────────────────────────────────
const unsigned long SAMPLE_INTERVAL_US = 10000;  // 100 Hz
unsigned long lastSampleUs = 0;

// ── HTML page (gzip-compressed inline, or plain below) ──
// For production: gzip the HTML and serve it as bytes.
// For simplicity here, we redirect to the pre-gzipped SPIFFS file
// OR just store the page on SPIFFS and serve it.
// 
// EASIEST APPROACH: put "tremor-app-fixed.html" in a "data/" folder,
// flash with ESP32 Sketch Data Uploader, then serve it below.
//
// If you haven't set up SPIFFS, use the fallback redirect HTML below.

// Fallback tiny page that redirects phone to download/open the file
const char REDIRECT_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TremorGuard</title>
<style>body{font-family:sans-serif;background:#0e0e12;color:#fff;display:flex;flex-direction:column;align-items:center;justify-content:center;min-height:100vh;margin:0;padding:20px;text-align:center;}</style>
</head><body>
<h2 style="color:#e8624a;margin-bottom:8px">TremorGuard</h2>
<p style="color:rgba(255,255,255,0.5);font-size:14px;margin-bottom:20px">
  ESP32 is running.<br>WebSocket active on port 81.
</p>
<p style="font-size:13px;color:rgba(255,255,255,0.7);line-height:1.8">
  Open the <b>TremorCare app</b> on your browser,<br>
  then connect to: <code style="background:rgba(255,255,255,0.1);padding:2px 8px;border-radius:5px">ws://)rawliteral";

// We insert the IP dynamically in handleRoot()

const char REDIRECT_HTML2[] PROGMEM = R"rawliteral(:81</code>
</p>
</body></html>
)rawliteral";

// ── WebSocket event handler ───────────────────────────
void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    IPAddress ip = wsServer.remoteIP(num);
    Serial.printf("[WS] Client %u connected from %s\n", num, ip.toString().c_str());
  } else if (type == WStype_DISCONNECTED) {
    Serial.printf("[WS] Client %u disconnected\n", num);
  }
  // We only send (not receive) in this sketch
}

// ── HTTP root handler ─────────────────────────────────
void handleRoot() {
  // If you have SPIFFS set up, swap this for SPIFFS serving:
  //   if(SPIFFS.exists("/tremor-app-fixed.html")){
  //     File f = SPIFFS.open("/tremor-app-fixed.html","r");
  //     httpServer.streamFile(f,"text/html");
  //     f.close(); return;
  //   }
  
  // Fallback: tiny info page
  String page = FPSTR(REDIRECT_HTML);
  page += WiFi.localIP().toString();
  page += FPSTR(REDIRECT_HTML2);
  httpServer.send(200, "text/html", page);
}

// ── Compute RMS of z buffer ───────────────────────────
float computeRMS() {
  float sum = 0;
  for (int i = 0; i < WINDOW_SIZE; i++) sum += zBuf[i] * zBuf[i];
  return sqrt(sum / WINDOW_SIZE);
}

// ── Setup ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  // Motor pin
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);

  // MPU-6050
  Wire.begin();
  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("MPU6050 connection FAILED — check wiring!");
    while (1) delay(500);
  }
  mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2);
  mpu.setFullScaleGyroRange(MPU6050_GYRO_FS_250);
  mpu.setDLPFMode(MPU6050_DLPF_BW_42);   // 42Hz low-pass (removes aliasing)
  Serial.println("MPU6050 OK");

  // WiFi
  Serial.printf("Connecting to %s ...\n", SSID);
  WiFi.begin(SSID, PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500); Serial.print(".");
    attempts++;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi FAILED — check SSID/password!");
    while (1) delay(1000);
  }
  Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("► Open on phone: http://%s\n", WiFi.localIP().toString().c_str());
  Serial.printf("► WebSocket:     ws://%s:81\n",  WiFi.localIP().toString().c_str());

  // HTTP server
  httpServer.on("/", handleRoot);
  httpServer.begin();
  Serial.println("HTTP server started on port 80");

  // WebSocket server
  wsServer.begin();
  wsServer.onEvent(onWsEvent);
  Serial.println("WebSocket server started on port 81");
}

// ── Loop ──────────────────────────────────────────────
void loop() {
  httpServer.handleClient();
  wsServer.loop();

  // Sample at 100 Hz
  unsigned long now = micros();
  if (now - lastSampleUs < SAMPLE_INTERVAL_US) return;
  lastSampleUs = now;

  // Read IMU
  int16_t ax16, ay16, az16, gx16, gy16, gz16;
  mpu.getMotion6(&ax16, &ay16, &az16, &gx16, &gy16, &gz16);

  // Convert to m/s²
  float ax = ax16 / ACCEL_SCALE * G_TO_MS2;
  float ay = ay16 / ACCEL_SCALE * G_TO_MS2;
  float az = az16 / ACCEL_SCALE * G_TO_MS2;

  // Push z into rolling buffer (z = main tremor axis when wrist-worn)
  zBuf[zIdx % WINDOW_SIZE] = az;
  zIdx++;

  // Tremor detection
  float rms = computeRMS();
  bool nowTremoring = (rms > TREMOR_THRESH);

  if (nowTremoring && !tremoring) {
    // Tremor onset
    tremoring = true;
    tremorStart = millis();
    // Schedule haptic motor 1000ms after onset
    motorScheduled = true;
    motorFireAt = tremorStart + MOTOR_DELAY_MS;
    Serial.println("[TREMOR] Detected — motor in 1s");
  } else if (!nowTremoring && tremoring) {
    // Tremor ended
    tremoring = false;
    motorScheduled = false;
    digitalWrite(MOTOR_PIN, LOW);
    Serial.printf("[TREMOR] Ended — duration %lums\n", millis() - tremorStart);
  }

  // Fire motor if scheduled
  if (motorScheduled && millis() >= motorFireAt) {
    motorScheduled = false;
    if (tremoring) {                      // still tremoring? fire
      digitalWrite(MOTOR_PIN, HIGH);
      delay(200);                         // 200ms pulse (non-blocking in production)
      digitalWrite(MOTOR_PIN, LOW);
      Serial.println("[MOTOR] Haptic pulse fired");
    }
  }

  // Build JSON and broadcast to all WebSocket clients
  // Format matches what the app expects: {"x":...,"y":...,"z":...}
  char json[64];
  snprintf(json, sizeof(json),
    "{\"x\":%.4f,\"y\":%.4f,\"z\":%.4f,\"rms\":%.4f,\"t\":%d}",
    ax, ay, az, rms, (int)tremoring);
  wsServer.broadcastTXT(json);
}
