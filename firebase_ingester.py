import json
import time
from datetime import datetime, timezone

import paho.mqtt.client as mqtt  # pip install paho-mqtt
import ssl

# --- 1. MQTT CONFIG (MATCH UNO + DASHBOARD) ---
BROKER = "broker.hivemq.com"
PORT = 1883                     # public, non-TLS MQTT port
TOPIC = "hope/iot/circuit5/living-room/uno-r4/telemetry"


# --- 2. FIREBASE CONFIG (REALTIME DATABASE) ---
#  a) In Firebase console, create a project.
#  b) Enable Realtime Database.
#  c) Create a service account key JSON file and download it.
#  d) Put the path to that JSON file below.
#  e) Set DATABASE_URL to your db URL from Firebase console.

import firebase_admin           # pip install firebase-admin
from firebase_admin import credentials, db

SERVICE_ACCOUNT_PATH = r"c:/Users/Alex/OneDrive/Desktop/Year 3/1 - Internet of Things/serviceAccountKey.json"
DATABASE_URL = "https://iotsystem-circuit5-default-rtdb.firebaseio.com"   


def init_firebase():
    """
    Initialise Firebase Admin SDK with a service account.
    This script is a backend/ingester, so using Admin SDK is appropriate.
    """
    if not firebase_admin._apps:
        cred = credentials.Certificate(SERVICE_ACCOUNT_PATH)
        firebase_admin.initialize_app(cred, {
            "databaseURL": DATABASE_URL
        })
    print("[Firebase] Initialised connection to Realtime Database.")


# --- 3. TELEMETRY VALIDATION (similar to your JS) ---

def parse_and_validate_payload(payload: str):
    """
    Parse MQTT payload as JSON and validate basic structure and ranges.
    Returns a dict {deviceId, temperature, humidity, status} or None.
    """
    try:
        data = json.loads(payload)
    except json.JSONDecodeError:
        print("[WARN] Invalid JSON, ignoring:", payload)
        return None

    if not isinstance(data, dict):
        print("[WARN] Payload is not a JSON object, ignoring:", data)
        return None

    # Device ID is optional but recommended
    device_id = data.get("deviceId", "unknown-device")

    temp_raw = data.get("temperature")
    hum_raw = data.get("humidity")
    status_raw = data.get("status", "unknown")

    try:
        temperature = float(temp_raw)
        humidity = float(hum_raw)
    except (TypeError, ValueError):
        print("[WARN] Missing or non-numeric temperature/humidity, ignoring:", data)
        return None

    # Physical sanity checks (same as client-side)
    if temperature < -40 or temperature > 80:
        print("[WARN] Temperature out of expected range, ignoring:", temperature)
        return None
    if humidity < 0 or humidity > 100:
        print("[WARN] Humidity out of expected range, ignoring:", humidity)
        return None

    status = str(status_raw).lower()
    if status not in ("normal", "alert", "error", "unknown"):
        status = "unknown"

    return {
        "deviceId": device_id,
        "temperature": temperature,
        "humidity": humidity,
        "status": status,
    }


# --- 4. FIREBASE WRITE HELPER ---

def store_reading_to_firebase(reading: dict):
    """
    Store a single validated reading into Firebase Realtime Database.
    Structure (example):

    /readings/<deviceId>/<auto-push-id> = {
        timestamp: "...",
        temperature: ...,
        humidity: ...,
        status: "normal"
    }
    """
    # Attach server-side timestamp (UTC ISO 8601)
    timestamp = datetime.now(timezone.utc).isoformat()

    payload = {
        "timestamp": timestamp,
        "temperature": reading["temperature"],
        "humidity": reading["humidity"],
        "status": reading["status"],
    }

    device_id = reading["deviceId"]

    # Path: /readings/<deviceId>/...
    ref = db.reference(f"readings/{device_id}")
    new_ref = ref.push(payload)

    print(f"[Firebase] Stored reading under key {new_ref.key}: {payload}")


# --- 5. MQTT CALLBACKS ---

def on_connect(client, userdata, flags, rc):
    print("[MQTT] Connected with result code", rc)
    if rc == 0:
        print(f"[MQTT] Subscribing to topic: {TOPIC}")
        client.subscribe(TOPIC, qos=0)
    else:
        print("[MQTT] Connection failed.")


def on_message(client, userdata, msg):
    payload_str = msg.payload.decode(errors="ignore")
    print(f"[MQTT] Received on {msg.topic}: {payload_str}")

    reading = parse_and_validate_payload(payload_str)
    if reading is None:
        # Invalid / malicious / garbage payload
        return

    try:
        store_reading_to_firebase(reading)
    except Exception as e:
        print("[ERROR] Failed to store reading to Firebase:", e)


# --- 6. MAIN ENTRYPOINT ---

def main():
    print("[System] Initialising Firebase...")
    init_firebase()

    print("[System] Connecting to MQTT broker...")
    client = mqtt.Client()

    # NOTE: we are using public broker.hivemq.com:1883 with no auth here to
    # match your UNO + dashboard. If you later move to a secured broker,
    # uncomment and adapt:
    #
    # client.username_pw_set("your-username", "your-password")
    # client.tls_set(cert_reqs=ssl.CERT_REQUIRED)   # and change PORT to 8883

    client.on_connect = on_connect
    client.on_message = on_message

    client.connect(BROKER, PORT)
    print("[System] MQTT connected. Listening for messages...")
    client.loop_forever()


if __name__ == "__main__":
    main()
