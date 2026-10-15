🌿 Adaptive Dehydration Chamber
This repository contains the embedded-system and hardware-control development for my individual research component:

Adaptive Dehydration Chamber and Remaining Drying Time Prediction

This component is part of the main research project:

Smart Dehydration and Quality Preservation System for Cinnamon Processing

📌 Overview
The purpose of this module is to develop a smart cinnamon dehydration chamber that can continuously monitor drying conditions, estimate the current moisture level of cinnamon using weight reduction, and automatically control the drying process.

The system is built around an ESP32-S3 microcontroller and integrates environmental sensing, weight measurement, user input, display output, and relay-based control of the heater and fan.

The long-term objective of this component is to use the collected drying data to develop a Remaining Drying Time (RDT) prediction model.

🏗️ System Architecture
Chamber Sensors: SHT30 (Temperature & RH) and Load Cell + HX711 (Weight measurement)

Microcontroller: ESP32-S3 DevKitC-1 processing sensor inputs

User Interface & Control: OLED Display (System Status), 4x4 Keypad (Target Moisture), and Relays (Heater & Fan control)

Connectivity & Backend: Wi-Fi communication sending data to Backend / Database, Web Dashboard, and RDT Prediction Model

🔧 Main Hardware Components
ESP32-S3 DevKitC-1 – Main microcontroller

SHT30 Sensor – Chamber temperature and relative humidity monitoring

Load Cell + HX711 – Continuous cinnamon weight measurement

OLED Display – Displays system and drying information

4×4 Keypad – Allows user input such as target moisture level

2-Channel 5V Relay Module – Controls heater and fan

Incandescent Bulb – Used as the heating source

12V Fan – Provides forced air circulation inside the drying system

🔄 Drying Scenario
At the beginning of a drying batch, the initial cinnamon moisture percentage is obtained using a reference cinnamon moisture meter.

The cinnamon bundle is then placed on the load-cell platform inside the dehydration chamber.

The user enters the required target moisture level through the keypad.

During the drying process, the system continuously monitors:

🌡️ Chamber temperature

💧 Relative humidity

⚖️ Cinnamon weight

📉 Estimated moisture level

⏱️ Drying time

🔥 Heater state

🌀 Fan state

The load cell measures the reduction in cinnamon mass as moisture is removed during drying. Using the initial moisture value, initial weight, and current weight, the system can estimate the current moisture condition of the cinnamon.

The heater and fan are controlled according to the drying process. Once the estimated moisture reaches the selected target level, the drying cycle can be stopped automatically.

🤖 Remaining Drying Time Prediction
The system is designed to collect time-series drying data for the development of a Remaining Drying Time (RDT) prediction model.

Typical model inputs include:

Temperature

Humidity

Current weight

Estimated moisture

Initial moisture

Target moisture

Elapsed drying time

Heater state

Fan state

Recent drying rate

The prediction model will estimate the amount of time remaining before the cinnamon reaches the required target moisture level. The RDT prediction will later be integrated with the web application and can also be displayed through the local system interface.

📊 Data Collection
During a drying batch, the ESP32-S3 can continuously collect and transmit drying-related measurements.

Typical recorded data include:

timestamp

batchId

initialWeight

initialMoisture

targetMoisture

temperature

humidity

currentWeight

estimatedMoisture

elapsedTime

heaterState

fanState

These records can later be used for:

Drying behaviour analysis

RDT model training

Model evaluation

Batch comparison

Web dashboard visualization

🌐 Web Application Integration
The ESP32-S3 can communicate with the backend through Wi-Fi. A simplified data flow is:
Sensors → ESP32-S3 → Wi-Fi → Backend API → Database → Web Dashboard → RDT Prediction

The web application can be used to display:

Live temperature

Relative humidity

Current weight

Estimated moisture

Target moisture

Drying duration

Heater and fan status

Remaining Drying Time

(Note: The main web application used by the complete research project is maintained in a separate repository.)

📁 Repository Purpose
This repository contains the firmware, hardware testing, experimental development, and documentation related to the Adaptive Dehydration Chamber. It includes:

Individual component test programs

ESP32-S3 testing

OLED testing

SHT30 sensor testing

Load-cell and HX711 testing

Load-cell calibration

Keypad testing

Relay-control testing

Integrated ESP32 firmware

Wiring diagrams

Chamber-related documentation

Experimental drying development

Future RDT integration

🎯 Research Objective

The dehydration chamber is not only intended to function as a hardware drying system. It also acts as an experimental platform for collecting real cinnamon drying data and studying drying behaviour.

The main research objective of this component is to investigate whether continuous sensor data and drying-state information can be used to provide reliable Remaining Drying Time predictions and support adaptive drying control.
