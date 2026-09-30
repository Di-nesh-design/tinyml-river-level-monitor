

from flask import Flask, request, jsonify
import requests
import time

app = Flask(__name__)

BLYNK_AUTH_TOKEN = "nZfRfjLAuElkv2WB3mqlQmyKgjWCxV4Z"   
BLYNK_BASE_URL = "https://blynk.cloud/external/api/update"

LAT = 22.8046   # We can change it according to our River location
LON = 86.2029

V_WATER_LEVEL   = "v0"
V_RAIN_NOW      = "v1"
V_TREND         = "v2"
V_FORECAST_RAIN = "v3"
V_RISK_SCORE    = "v4"
V_RISK_STATUS   = "v5"

LEVEL_LOW_CM = 5.0
LEVEL_HIGH_CM = 40.0
FORECAST_HIGH_MM = 30.0

#
DEMO_OVERRIDE_FORECAST_RAIN = None

WEATHER_FETCH_INTERVAL = 600  
last_weather_fetch = 0
cached_forecast = {"current_rain": 0.0, "forecast_rain_24h": 0.0}


def fetch_weather():
    if DEMO_OVERRIDE_FORECAST_RAIN is not None:
        return {"current_rain": 2.0, "forecast_rain_24h": DEMO_OVERRIDE_FORECAST_RAIN}

    url = (
        "https://api.open-meteo.com/v1/forecast"
        f"?latitude={LAT}&longitude={LON}"
        "&current=precipitation"
        "&hourly=precipitation"
        "&forecast_days=2&timezone=auto"
    )
    try:
        resp = requests.get(url, timeout=10)
        data = resp.json()
        current_rain = data.get("current", {}).get("precipitation", 0.0)
        hourly = data.get("hourly", {}).get("precipitation", [])
        forecast_24h = sum(hourly[:24]) if hourly else 0.0
        return {"current_rain": current_rain, "forecast_rain_24h": forecast_24h}
    except Exception as e:
        print(f"[weather] fetch failed: {e}")
        return cached_forecast


def normalize(value, low, high):
    if high == low:
        return 0.0
    return max(0.0, min(1.0, (value - low) / (high - low)))


def compute_risk(level_cm, trend, current_rain, forecast_rain_24h):
    score = 0.0
    score += normalize(level_cm, LEVEL_LOW_CM, LEVEL_HIGH_CM) * 40
    score += (30 if trend == "rising" else (10 if trend == "stable" else 0))
    score += normalize(forecast_rain_24h, 0, FORECAST_HIGH_MM) * 20
    score += normalize(current_rain, 0, 10) * 10
    return round(score, 1)


def risk_to_status(score):
    if score >= 65:
        return "HIGH"
    if score >= 35:
        return "MODERATE"
    return "LOW"


def push_to_blynk(pin, value):
    try:
        r = requests.get(
            BLYNK_BASE_URL,
            params={"token": BLYNK_AUTH_TOKEN, pin: value},
            timeout=5,
        )
        if r.status_code != 200:
            print(f"[blynk] {pin} push returned status {r.status_code}: {r.text}")
    except Exception as e:
        print(f"[blynk] push failed for {pin}: {e}")


@app.route("/data", methods=["POST"])
def receive_data():
    global last_weather_fetch, cached_forecast

    payload = request.get_json(force=True)

    distance = payload.get("distance")
    trend = payload.get("trend", "stable")
    confidence = payload.get("confidence", 0.0)
    anomaly = payload.get("anomaly", 0.0)
    anomaly_threshold = 0.3

    sensor_unreliable = anomaly > anomaly_threshold
    effective_trend = "stable" if sensor_unreliable else trend

    print(f"[recv] distance={distance:.2f} trend={trend} conf={confidence:.2f} "
          f"anomaly={anomaly:.3f} {'[UNRELIABLE]' if sensor_unreliable else ''}")

    if time.time() - last_weather_fetch > WEATHER_FETCH_INTERVAL or last_weather_fetch == 0:
        cached_forecast = fetch_weather()
        last_weather_fetch = time.time()
        print(f"[weather] {cached_forecast}")

    risk_score = compute_risk(
        distance, effective_trend,
        cached_forecast["current_rain"], cached_forecast["forecast_rain_24h"]
    )
    risk_status = risk_to_status(risk_score)

    print(f"[risk] score={risk_score} status={risk_status}")

    push_to_blynk(V_WATER_LEVEL, round(distance, 2))
    push_to_blynk(V_TREND, "uncertain" if sensor_unreliable else trend)
    push_to_blynk(V_RAIN_NOW, cached_forecast["current_rain"])
    push_to_blynk(V_FORECAST_RAIN, cached_forecast["forecast_rain_24h"])
    push_to_blynk(V_RISK_SCORE, risk_score)
    push_to_blynk(V_RISK_STATUS, risk_status)

    return jsonify({"status": "ok", "risk_score": risk_score, "risk_status": risk_status})


if __name__ == "__main__":
    import socket
    hostname = socket.gethostname()
    local_ip = socket.gethostbyname(hostname)
    print(f"Server starting. Put this in your ESP32-S3 sketch's SERVER_URL:")
    print(f"  http://{local_ip}:5005/data")
    print()
    app.run(host="0.0.0.0", port=5005)
