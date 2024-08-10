# FujiNet Firmware Uploader

## Table of Contents
1. [Introduction](#introduction)
2. [Features](#features)
3. [Requirements](#requirements)
4. [Installation](#installation)
5. [Usage](#usage)
6. [Interactive Menu System](#interactive-menu-system)
7. [Device Monitoring](#device-monitoring)
8. [Command-line Options](#command-line-options)
9. [Troubleshooting](#troubleshooting)
10. [Contributing](#contributing)
11. [License](#license)

## Introduction

The FujiNet Firmware Uploader is a Python script designed to simplify the process of downloading and flashing firmware updates for FujiNet devices. It automates the retrieval of firmware releases from the official FujiNet GitHub repository, allows users to select specific versions through an interactive menu system, handles the flashing process using `esptool.py`, and provides device monitoring capabilities after flashing.

## Features

- Fetches the latest firmware releases from the FujiNet GitHub repository
- Interactive menu system for selecting firmware versions and files
- Automatic detection of USB ports for macOS and Linux systems
- Downloads and extracts firmware files
- Flashes firmware and filesystem images to FujiNet devices
- Monitors device output after flashing
- Supports automatic selection of the latest firmware version

## Requirements

- Python 3.6 or higher
- `pip` (Python package installer)
- Internet connection for downloading firmware releases
- FujiNet device connected via USB

## Installation

1. Clone or download this script to your local machine.

2. Install the required Python packages:

   ```
   pip install requests
   ```

3. Ensure you have `esptool.py` installed:

   ```
   pip install esptool
   ```

4. Install `platformio` for the monitoring feature:

   ```
   pip install platformio
   ```

## Usage

1. Connect your FujiNet device to your computer via USB.

2. Run the script:

   ```
   python fujinet_firmware_uploader.py
   ```

3. Follow the on-screen prompts to select and flash the firmware.

4. After flashing, the script will automatically start monitoring the device output.

## Interactive Menu System

The script provides an interactive menu system to guide you through the firmware selection and flashing process:

1. **Release Selection**: The script will present a list of available firmware releases fetched from the FujiNet GitHub repository. You can choose a specific release by entering its corresponding number.

2. **File Selection**: If a release contains multiple firmware files (e.g., for different FujiNet variants), you'll be presented with a menu to choose the appropriate file.

3. **Confirmation**: The script will display the selected release and file information for confirmation before proceeding with the download and flashing process.

This menu system ensures that you have full control over which firmware version is installed on your FujiNet device.

## Device Monitoring

After successfully flashing the firmware, the script automatically enters a monitoring mode:

1. The script uses the `platformio` tool to establish a serial connection with the FujiNet device.
2. It displays the device's output in real-time, allowing you to see the boot process and any diagnostic information.
3. This monitoring feature is crucial for verifying that the firmware was installed correctly and that the device is functioning as expected.
4. To exit the monitoring mode, you can typically use the key combination `Ctrl+C`.

The monitoring feature provides immediate feedback on the success of the firmware update and can be helpful for troubleshooting if any issues arise.

## Command-line Options

- `-l` or `--latest`: Automatically select and flash the latest firmware version, bypassing the interactive menus.

  Example:
  ```
  python fujinet_firmware_uploader.py --latest
  ```

  Please note: this option is not complete, it will select the first release and the first zip from that release it finds. Do not use it at this time.
  

## Troubleshooting

- If you encounter a "Missing module" error, install the required module using pip as instructed by the error message.
- Ensure only one FujiNet device is connected when flashing.
- If flashing fails, check the USB connection and try again.
- Make sure you have the necessary permissions to access the USB port on your system.
- If the device doesn't appear to boot after flashing, try power cycling the FujiNet device.
- During monitoring, if you don't see any output, ensure that the baud rate (default: 460800) matches your FujiNet device's configuration.

## Contributing

Contributions to improve the FujiNet Firmware Uploader are welcome. Please submit pull requests or open issues on the project's GitHub repository.

## License

This is licensed under the Serious License
