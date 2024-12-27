
# PowerDisplay

## Overview

**PowerDisplay** is a smart e-paper touchscreen project designed to interact seamlessly with a Home Automation system running [HomeAssistant](https://www.home-assistant.io/). The project fetches real-time data, displays it on a low-power e-paper screen, and allows direct control of smart home parameters through its touchscreen interface.

## Features

- **Real-Time Data Fetching**: Integrates with HomeAssistant via Wi-Fi to retrieve sensor data and system states.
- **Interactive Interface**: Custom icons and touchscreen capabilities enable users to interact with their smart home directly.
- **Energy Efficiency**: Uses an e-paper display for low power consumption and a persistent display.

## Hardware Requirements

- ESP32-based microcontroller (e.g., LilyGo EPD47 with integrated Wi-Fi and touchscreen support).
- E-paper display with grayscale capabilities.

## Software Requirements

- [PlatformIO](https://platformio.org/) with the Arduino framework.
- Libraries:
  - WiFi
  - ArduinoJson
  - Custom e-paper and icon libraries (included in the repository).

## Installation

1. Clone this repository:
   ```bash
   git clone https://github.com/serjru/PowerDisplay.git
   cd PowerDisplay
   ```

2. Open the project in [PlatformIO](https://platformio.org/).

3. Build and upload the firmware to the ESP32 device.

## Usage

1. Power on the device.
2. Connect the ESP32 to your Wi-Fi network.
3. The e-paper display will show real-time data from HomeAssistant and enable touchscreen-based controls.

## Configuration

- Modify `config.h` to add your HomeAssistant API endpoint and credentials.
- Customize displayed icons and layout in `icons.h` and relevant sections of `main.cpp`.

## Acknowledgments

- Project built using examples and libraries for the LilyGo EPD47 platform.
- Special thanks to the HomeAssistant community for inspiration.

## License

Licensed under the MIT License. See `LICENSE` for details.
