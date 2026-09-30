#define TRIG_PIN 4
#define ECHO_PIN 5

#define CALIBRATION_OFFSET_CM 5.15f
#define SEND_INTERVAL_MS 200   

#define MIN_VALID_CM 25.0
#define MAX_VALID_CM 500.0


#define DELTA_LOOKBACK 5
float history[DELTA_LOOKBACK];
int historyIndex = 0;
int historyCount = 0;

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

float lastGoodValue = -1;

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  delay(1000);

  for (int i = 0; i < DELTA_LOOKBACK; i++) history[i] = 0;
}

.
float computeDelta(float currentDistance) {
  if (historyCount < DELTA_LOOKBACK) {
    return 0; 
  }

  int oldestIdx = historyIndex; 
  float oldValue = history[oldestIdx];

  return currentDistance - oldValue;
}

void loop() {
  float distance = readDistanceFiltered();

  if (distance < 0) {
    distance = (lastGoodValue < 0) ? 0 : lastGoodValue;
  }
  lastGoodValue = distance;

  float delta = computeDelta(distance);


  history[historyIndex] = distance;
  historyIndex = (historyIndex + 1) % DELTA_LOOKBACK;
  if (historyCount < DELTA_LOOKBACK) historyCount++;

  
  Serial.print(distance, 3);
  Serial.print(",");
  Serial.println(delta, 3);

  delay(SEND_INTERVAL_MS);
}
