TITLE: IoT Smart Home Monitoring System — Temperature & Humidity Telemetry + Dashboard + Cloud Logging

OVERVIEW:
This system allows the user to:
1. Monitor the temeperature and humidity of their living room via. an online dashboard or physical LCD.
2. Receive alerts based on temperature and humidity % thresholds.
3. View the historical data as a timeline by fetching from a database.

The technologies used for this system:
    HARDWARE:
    - Arduino UNO R4 WiFi
    - DHT11 sensor
    - LCD (I2C)
    - Red + Green LEDs

    SOFTWARE:
    - Arduino IDE
    - Python 3.11
    - Firebase Realtime database
    - Netlify (Dashboard Hosting)
    - HiveMQ public broker

MQTT TOPIC: hope/iot/circuit5/living-room/uno-r4/telemetry

DATA FLOW SUMMARY:
Sensor → Arduino → MQTT → Dashboard (live) → Python → Firebase → Dashboard (history).
    Hardware publishes JSON telemetry to MQTT topic
    Dashboard subscribes to MQTT for live updates
    Python ingester listens to the same MQTT topic and writes to firebase
    Dashboard loads historical data from firebase

HOW TO RUN THE SYSTEM:
    1. FIRMWARE:
        a. Verify sketch.ino
        b. Upload sketch.ino
        c. Connect to Wi-Fi using provisioning portal
            - PORTAL ADDRESS http://192.168.4.1
                - DO NOT USE "HTTPS"
        d. Device connects and publishes telemetry
    2. INGESTER
        a. install dependencies
            - pip install paho-mqtt firebase-admin
        b. Place firebase service account JSON in correct location
        c. RUN python firebase_ingester.py inside system correct directory
    3. DASHBOARD
        - Hosted on Netlify
            - https://circuit5db.netlify.app/


