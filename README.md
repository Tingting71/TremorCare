# TremorCare: Low-Cost Wearable Wrist Tremor Monitoring System

## Overview
**TremorCare** is an accessible, low-cost (~$30 BOM) wearable device and visualization platform designed for continuous, objective monitoring of hand and wrist tremors (such as Parkinsonian resting tremor and essential tremor). 

Clinical tremor assessments often rely on brief, subjective in-clinic observations that fail to capture symptom fluctuations throughout daily life. TremorCare bridges this gap by combining a compact wrist-worn sensor prototype with an intuitive web and mobile interface, enabling patients to track daily symptom patterns and providing clinicians with quantitative, longitudinal motion data.

---

## System Architecture

### 1. Wearable Hardware Prototype
* **Microcontroller & Sensing:** Built around an **ESP32** microcontroller paired with a tri-axial **IMU** (accelerometer and gyroscope) to capture high-resolution wrist kinematics.
* **Accessible Design:** Engineered with off-the-shelf components (~$30 total material cost) and a lightweight wrist-mounted form factor to support comfortable at-home use.
* **Wireless Transmission:** Streams motion data wirelessly via Wi-Fi / Bluetooth for real-time logging and visualization.

### 2. Signal Processing & Tremor Quantification
* **Artifact Filtering:** Applies digital bandpass filtering to isolate pathological tremor frequency bands (typically 3–12 Hz) from voluntary daily hand movements and baseline drift.
* **Feature Extraction:** Computes objective time- and frequency-domain metrics—including dominant tremor frequency, peak-to-peak amplitude, RMS intensity, and tremor episode duration—using Python and MATLAB pipelines.

### 3. Real-Time Web & Mobile Visualization
* **Patient View:** Displays clear daily tremor trends, medication/symptom logs, and simplified severity summaries to support self-management.
* **Clinician View:** Provides interactive time-series waveforms, spectral power distributions, and longitudinal session comparisons to assist with treatment titration.

---

## Live Demo & Preview
* **Interactive Web Interface:** (https://github.com/Tingting71/TremorCare/blob/main/tremor-monitor-v3%20(2).html)
* https://github.com/Tingting71/TremorCare/blob/main/tremor-app-v2.html

---

## Repository Structure
```text
TremorCare/
├── firmware/           # ESP32 data acquisition and wireless streaming code
├── signal_processing/  # Filtering, spectral analysis, and tremor metric extraction scripts
├── web_app/            # Frontend web/mobile visualization dashboard (HTML/CSS/JS)
├── docs/               # Hardware schematic, BOM breakdown, and system diagrams
└── README.md
