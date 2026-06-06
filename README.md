# lora-acoustic-edge-ai
Autonomous AI edge node utilizing an ESP32-S3, TinyML, and an encrypted LoRa mesh network to detect environmental threats in deep forest environments without cloud dependency.

# Hardware Architecture
* **Microcontroller:** ESP32-S3 (Utilizing vector instructions for efficient ML inference)
* **Acoustic Sensors:** MAX9814 (Hardware-gated analog wake-up trigger), INMP441 (I2S digital microphone for clean spectrogram generation)
* **Environmental Sensor:** MQ2 Gas Sensor (Utilized for Dual-Verification logic)
* **Communication:** SX1278 LoRa Transceiver (433MHz)
* **Peripherals:** DS3231 Real-Time Clock, MicroSD Card Module (Dual-SPI/HSPI bus implementation)

# Software & Machine Learning Stack
* **Firmware:** C++ (Interrupt-driven state machine)
* **Machine Learning:** Edge Impulse (INT8-quantized Convolutional Neural Network)
* **Signal Processing:** Mel-Frequency Cepstral Coefficients (MFCC) extraction
* **Security:** Symmetric XOR Stream Cipher

# Key System Features
* **Low Power State Machine:** The primary CPU remains in a Light Sleep state (~2mA) for >99% of its operational life, waking within milliseconds exclusively via hardware analog interrupts to preserve off-grid battery capacity.
* **Dual-Verification Sensor Fusion:** Mitigates false positive alert rates by cross-referencing acoustic neural network outputs (e.g., acoustic "Fire Crackling") with physical chemical data (Smoke/Gas).
* **Forensic Audio Logging:** Dynamically generates and writes standard 44-byte RIFF WAV headers to raw PCM audio buffers, storing timestamped evidence locally to an SD card for legal accountability.
* **Zero-Cloud Mesh Network:** Implements a localized flooding mesh algorithm with packet collision jitter and memory buffers to bounce alerts out of dense forest canopies.
* **Offline Web Dashboard:** The Central Gateway hosts a local Wi-Fi Access Point and dynamically serves an HTML/CSS interface, allowing field rangers to view real-time decrypted logs without internet access.

## Repository Structure
* `/Scout_Node/` - Firmware for the battery-powered edge inference device.
* `/Central_Gateway/` - Firmware for the base station, SD logging, and web server.
* `/Hardware/` - Wiring schematics and pinout mapping tables.
