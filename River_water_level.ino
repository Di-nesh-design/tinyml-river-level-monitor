#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <RiverSense-AI_inferencing.h>


#define WIFI_SSID     "TP_Link_3290"
#define WIFI_PASSWORD "9047638"


#define SERVER_URL "http://192.168.1.0:5005/data"


#define TRIG_PIN 4
#define ECHO_PIN 5

#define CALIBRATION_OFFSET_CM 5.15f


#define SAMPLE_INTERVAL_MS 500     
#define DELTA_LOOKBACK 10         

#define MIN_VALID_CM 25.0f
#define MAX_VALID_CM 500.0f

#define CONFIDENCE_THRESHOLD 0.6f
#define ANOMALY_THRESHOLD 0.3f
#define VOTE_WINDOW 5

static const int SAMPLES_PER_WINDOW = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE / 2;
static float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];

float deltaHistory[DELTA_LOOKBACK];
int deltaHistIndex = 0;
int deltaHistCount = 0;

String voteHistory[VOTE_WINDOW];
int voteIndex = 0;
int voteCount = 0;
String currentTrend = "stable";

float lastGoodValue = -1.0f;

float readDistanceRaw() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return -1.0f;

  float distance = (duration * 0.0343f) / 2.0f;
  distance -= CALIBRATION_OFFSET_CM;
  return distance;
}

float readDistanceFiltered() {
  float samples[3];
  int valid = 0;

  for (int i = 0; i < 3; i++) {
    float d = readDistanceRaw();
    if (d >= MIN_VALID_CM && d <= MAX_VALID_CM) {
      samples[valid++] = d;
    }
    delay(15);
  }

  if (valid == 0) return -1.0f;

  for (int i = 0; i < valid - 1; i++) {
    for (int j = i + 1; j < valid; j++) {
      if (samples[j] < samples[i]) {
        float t = samples[i]; samples[i] = samples[j]; samples[j] = t;
      }
    }
  }
  return samples[valid / 2];
}

float computeDelta(float currentDistance) {
  if (deltaHistCount < DELTA_LOOKBACK) return 0.0f;
  return currentDistance - deltaHistory[deltaHistIndex];
}

void updateDeltaHistory(float distance) {
  deltaHistory[deltaHistIndex] = distance;
  deltaHistIndex = (deltaHistIndex + 1) % DELTA_LOOKBACK;
  if (deltaHistCount < DELTA_LOOKBACK) deltaHistCount++;
}

int raw_feature_get_data(size_t offset, size_t length, float *out_ptr) {
  memcpy(out_ptr, features + offset, length * sizeof(float));
  return 0;
}

void get_top_prediction(ei_impulse_result_t result, String &predicted_trend, float &confidence) {
  int best_idx = 0;
  float best_value = 0;
  for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    if (result.classification[i].value > best_value) {
      best_value = result.classification[i].value;
      best_idx = i;
    }
  }
  predicted_trend = String(ei_classifier_inferencing_categories[best_idx]);
  confidence = best_value;
}

String applyVoting(String newTrend, float confidence) {
  if (confidence < CONFIDENCE_THRESHOLD) {
    return currentTrend;
  }

  voteHistory[voteIndex] = newTrend;
  voteIndex = (voteIndex + 1) % VOTE_WINDOW;
  if (voteCount < VOTE_WINDOW) voteCount++;

  int risingCount = 0, fallingCount = 0, stableCount = 0;
  for (int i = 0; i < voteCount; i++) {
    if (voteHistory[i] == "rising") risingCount++;
    else if (voteHistory[i] == "falling") fallingCount++;
    else stableCount++;
  }

  if (risingCount >= fallingCount && risingCount >= stableCount) return "rising";
  if (fallingCount >= stableCount) return "falling";
  return "stable";
}

void connectWiFi() {
  Serial.printf("Connecting to WiFi: %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi connection failed -- will keep classifying, sends skipped");
  }
}

.
void sendToServer(float distance, const String &trend, float confidence, float anomaly) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"distance\":" + String(distance, 3) + ",";
  json += "\"trend\":\"" + trend + "\",";
  json += "\"confidence\":" + String(confidence, 3) + ",";
  json += "\"anomaly\":" + String(anomaly, 3);
  json += "}";

  int httpCode = http.POST(json);
  if (httpCode != 200) {
    Serial.printf("[server] POST failed, status %d\n", httpCode);
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  delay(2000);

  connectWiFi();

  Serial.println("RiverSense AI - live inference (distance + delta)");
  Serial.printf("Expected frame size: %d (window=%d samples x 2 axes)\n",
                EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, SAMPLES_PER_WINDOW);
  Serial.printf("Sampling frequency (model): %.1f Hz | configured interval: %d ms\n",
                EI_CLASSIFIER_FREQUENCY, SAMPLE_INTERVAL_MS);
  Serial.println("CHECK: the two lines above must be consistent, or predictions will be wrong.\n");
}

void loop() {
  for (int i = 0; i < SAMPLES_PER_WINDOW; i++) {
    float distance = readDistanceFiltered();
    if (distance < 0) {
      distance = (lastGoodValue < 0) ? 0.0f : lastGoodValue;
    }
    lastGoodValue = distance;

    float delta = computeDelta(distance);
    updateDeltaHistory(distance);

    features[i * 2]     = distance;
    features[i * 2 + 1] = delta;

    delay(SAMPLE_INTERVAL_MS);
  }

  signal_t signal;
  signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
  signal.get_data = &raw_feature_get_data;

  ei_impulse_result_t result = { 0 };
  EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false);

  if (res != EI_IMPULSE_OK) {
    Serial.printf("Classifier error: %d\n", res);
    return;
  }

  String predicted_trend;
  float confidence;
  get_top_prediction(result, predicted_trend, confidence);

  float anomaly = 0.0f;
#if EI_CLASSIFIER_HAS_ANOMALY
  anomaly = result.anomaly;
#endif

  bool sensorUnreliable = (anomaly > ANOMALY_THRESHOLD);

  if (!sensorUnreliable) {
    currentTrend = applyVoting(predicted_trend, confidence);
  }

  Serial.printf("raw=%s (%.2f)  voted=%s  anomaly=%.3f  %s\n",
                predicted_trend.c_str(), confidence,
                currentTrend.c_str(), anomaly,
                sensorUnreliable ? "[SENSOR UNRELIABLE]" : "");

  sendToServer(lastGoodValue, currentTrend, confidence, anomaly);
}
