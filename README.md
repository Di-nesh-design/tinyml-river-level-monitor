# tinyml-river-level-monitor
### RiverSense AI — An Edge AI–Based River Water Level Monitoring and Flood Early-Warning System

A real-time river water level monitoring system utilizing Edge Impulse machine learning models deployed on an ESP32-S3 microcontroller.

Hackathon project by **Dinesh Behera (Team Lead)**, **Mihir Kumar Behera**, and **Sanath Kumar Sahoo** — Department of Electronics and Communication Engineering (ECE), National Institute of Technology (NIT) Rourkela.

---

## Overview

RiverSense AI senses river water level using a low-cost ultrasonic sensor, classifies the trend (rising / stable / falling) directly on-device using an Edge Impulse-trained TinyML model running on an ESP32-S3, fuses that with live weather forecast data, and pushes a real-time flood-risk score and automated alerts to a Blynk dashboard.

The core idea: trend detection stays local to the sensor node and keeps working even if the network drops, while cloud-based weather data adds early warning on top — giving a warning before the water has actually risen, not just after.

## Key Features

- **On-device (TinyML) inference** — rising / stable / falling classification runs directly on the ESP32-S3 using an Edge Impulse-exported model, with no cloud round-trip required for the core sensing decision.
- **Engineered delta feature** — a rate-of-change signal computed on-device that lets the model distinguish direction of change, which raw distance statistics alone cannot capture.
- **On-device anomaly detection** — a K-means anomaly model flags unreliable sensor windows (debris, faults, noise) so bad readings don't trigger false alarms.
- **Weather-fused risk scoring** — combines the sensed trend with live current-rainfall and 24-hour rainfall-forecast data (Open-Meteo API) into a transparent, explainable 0–100 risk score.
- **Live dashboard + automated alerts** — all data is pushed to a Blynk dashboard, with a push notification fired automatically when risk crosses the HIGH threshold.

## Architecture

![System Architecture](docs/architecture_diagram.png)

```
                 ┌───────────────────────┐
                 │   JSN-SR04T Sensor    │
                 │ (ultrasonic distance) │
                 └───────────┬───────────┘
                             │ distance reading
                             ▼
                 ┌───────────────────────┐
                 │      ESP32-S3          │
                 │  (Edge AI Node)        │
                 │  - Median filter       │
                 │  - Delta computation   │
                 │  - Edge Impulse model  │
                 │    (trend + anomaly)   │
                 └───────────┬───────────┘
                             │ distance, trend,
                             │ confidence, anomaly
                             │ (WiFi, HTTP POST JSON)
                             ▼
                 ┌───────────────────────┐
                 │  receiver_server.py    │
                 │      (laptop)          │
                 └─────┬─────────┬───────┘
                       │         │
           ┌───────────▼──┐  ┌───▼─────────────┐
           │ Open-Meteo    │  │ Risk Scoring     │
           │ Weather API   │  │ Engine           │
           │ (current +    │  │ (weighted        │
           │  24h forecast)│  │  formula)        │
           └───────────┬──┘  └───┬─────────────┘
                       │         │
                       └────┬────┘
                            ▼
                 ┌───────────────────────┐
                 │      Blynk Cloud       │
                 └─────┬─────────┬───────┘
                       │         │
             ┌─────────▼──┐   ┌──▼──────────────────┐
             │ Live         │   │ Push Notification    │
             │ Dashboard    │   │ (Risk = HIGH)         │
             │ (Chart,      │   │                       │
             │  Gauges,     │   │                       │
             │  Status)     │   │                       │
             └─────────────┘   └───────────────────────┘
```

## How It Works

1. The **JSN-SR04T** ultrasonic sensor measures distance to the water surface. A median-of-three filter rejects single-reading noise.
2. The **ESP32-S3** computes a rolling **delta** feature (change in distance over a multi-second lookback) alongside the raw distance — this directly encodes trend direction, which raw average/min/max statistics cannot.
3. Both signals feed an **Edge Impulse-trained classification model** running on-device, producing a trend label (rising / stable / falling) with a confidence score, plus an **anomaly score** from a separate K-means model.
4. The ESP32-S3 sends `{distance, trend, confidence, anomaly}` as JSON to `receiver_server.py` over WiFi.
5. The server fetches **current + 24-hour-forecast rainfall** from Open-Meteo, computes a **weighted risk score** (water level 40%, trend 30%, forecast rain 20%, current rain 10%), and pushes all six values to **Blynk**.
6. Blynk renders the live dashboard and fires a **push notification** when risk crosses the HIGH threshold.

## Demo Video
### 1. Server (laptop)
```bash
pip install -r requirements.txt
export BLYNK_AUTH_TOKEN="your_blynk_token_here"   # or edit the placeholder directly in the file
python server/receiver_server.py
```
It prints your laptop's local IP — copy this into the firmware's `SERVER_URL`.

### 2. Firmware (ESP32-S3)
Open `firmware/riversense_esp32_only.ino` in Arduino IDE and fill in:
- `WIFI_SSID`, `WIFI_PASSWORD`
- `SERVER_URL` (from step 1)
- Your exported Edge Impulse Arduino library (`RiverSense-AI_inferencing.h`), added via Sketch > Include Library > Add .ZIP Library
- `DELTA_LOOKBACK` — must match whatever lookback was used when the training data was collected

Flash it. On boot it connects to WiFi, samples the sensor, runs on-device inference, and POSTs results to the server every ~5 seconds.

### 3. Blynk Dashboard
Set up a Blynk template with virtual pins V0–V5 (water level, current rain, trend, forecast rain, risk score, risk status), a chart/gauge/label widget for each, and an automation that fires a push notification when the risk score crosses the HIGH threshold.

### 4. (Optional) Retraining the model
Use `Data_forwarder.ino` with `edge-impulse-data-forwarder` to collect fresh labeled samples (rising / stable / falling).

## Model Development Notes

Raw distance statistics (average / min / max) alone could not distinguish a rising trend from a falling one covering the same range, since these are order-invariant — they describe *what* values occurred in a window, not the *sequence* they occurred in. Introducing an engineered **delta** feature (change in distance over a multi-second lookback) resolved this by directly encoding trend direction.

Model iteration also surfaced the importance of raw data diversity: early models validated well internally (>95% validation accuracy) but generalized poorly to genuinely new recordings (as low as ~23% on held-out test data), a classic small-dataset overfitting symptom. This was resolved by collecting a broader range of recordings across different starting distances and rates of change, and by validating strictly on held-out data rather than trusting validation-set accuracy alone.

Reliability safeguards added on top of the raw classifier:
- **Majority voting** across the last 5 confident classification windows, to prevent a single noisy window from flipping the displayed trend.
- **Confidence thresholding** — low-confidence predictions are held at the previous state rather than acted upon.
- **Anomaly-based exclusion** — flagged readings are excluded from the risk calculation to prevent false alarms from unreliable sensor data.

## Known Limitations

- **Single-point sensing** — the system can't distinguish a locally-caused rise from one driven by upstream rainfall or tributary inflow well outside its own forecast radius.
- **Heuristic risk score** — the risk formula is a transparent, tunable heuristic rather than a hydrologically calibrated model; a production deployment would benefit from historical water-level and rainfall data specific to the deployment river.
- **WiFi for this demo** — WiFi was used for setup speed and ease of debugging during development. A **LoRa** transmission path (ESP32-S3 → LoRa → Arduino Uno gateway) was also evaluated and is architecturally preferable for a production/field deployment, since it offers greater data privacy (a private radio link vs. shared network infrastructure) and continued operation if the gateway or internet connection is temporarily unavailable.

## Future Work

- Reintroduce the LoRa transmission path for off-grid, privacy-focused field deployment, paired with solar/battery power.
- Multi-node deployment along a river for spatial coverage.
- Incorporate upstream forecast data as an additional risk input.
- Historical data-driven water-level forecasting alongside the current trend classifier.

