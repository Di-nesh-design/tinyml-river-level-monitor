# tinyml-river-level-monitor
A real-time river water level monitoring system utilizing Edge Impulse machine learning models deployed on an ESP32-S3 microcontroller.
# RiverSense AI
### An Edge AI–Based River Water Level Monitoring and Flood Early-Warning System

Hackathon project by **Dinesh Behera (Team Lead)**, **Mihir Kumar Behera**, and **Sanath Kumar Sahoo** — Department of Electronics and Communication Engineering (ECE), National Institute of Technology (NIT) Rourkela.

---

## What this is

RiverSense AI senses river water level using a low-cost ultrasonic sensor, classifies the trend (rising / stable / falling) directly on-device using an Edge Impulse-trained model running on an ESP32-S3, fuses that with live weather forecast data, and pushes a real-time flood-risk score and automated alerts to a Blynk dashboard.

The core idea: trend detection stays local to the sensor node (works even if the network drops), while cloud-based weather data adds early warning on top.

## Architecture



## How to run it

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

### 3. Blynk dashboard
Set up a Blynk template with virtual pins V0–V5 (water level, current rain, trend, forecast rain, risk score, risk status), a chart/gauge/label widget for each, and an automation that fires a push notification when the risk score crosses the HIGH threshold.

## Model development notes

Raw distance statistics (average / min / max) alone could not distinguish a rising trend from a falling one covering the same range, since these are order-invariant. Adding an engineered **delta** feature (change in distance over a multi-second lookback) resolved this by directly encoding trend direction. See the full report for the complete iteration history, including the diversity/overfitting issue found during validation-vs-test evaluation.

## Known limitations

- Single-point sensing — can't distinguish a locally caused rise from upstream-driven flooding outside its own forecast radius.
- The risk score is a transparent, tunable heuristic, not a hydrologically calibrated model.
- WiFi was used for this demo for setup speed; a LoRa transmission path was also evaluated for production deployments needing greater range and reduced dependence on shared network infrastructure (see report, Section 6).

Full details in `docs/RiverSense_AI_Hackathon_Report.pdf`.
