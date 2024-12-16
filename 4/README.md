# ESP32-CAM Video Streaming (Core pining tasks)

## Key Features:
- **Multi-WiFi Network Support**: Supports multiple Wi-Fi networks with automatic failover.
- **MJPEG Streaming**: Stream video via MJPEG with adjustable rotation.
- **Web Interface**: Control video feed and LED (via GPIO) using a simple web interface.
- **LED Indicator**: Control an LED (GPIO 4) based on URL request.
- **Camera Configuration**: Auto-adjusts settings based on PSRAM availability (e.g., frame size and buffer count).
- **Device Management**: Easy network connection setup with predefined Wi-Fi credentials.
- **Multi-Core Usage**: Utilises both cores of the ESP32 to handle video streaming on one core and web server tasks (e.g., controlling GPIO) on the other, ensuring efficient task distribution and performance.

## Setup:
- Connect to Wi-Fi networks
- Start the video stream and web server
- Control LED via URL queries