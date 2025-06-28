#!/usr/bin/env python3
# fujinet_firmware_uploader
# Aug2024 - use this script to select and upload released artifacts for fujinet
# check the fujinet_firmware_uploader.md for more detailed information
# v1.0

import sys

try:
    import os
    import requests
    import zipfile
    import json
    import platform
    import glob
    import subprocess
    import argparse
    #from io import BytesIO
except ImportError as e:
    missing_module = str(e).split()[-1]
    print(f"Error: Missing module {missing_module}.")
    print(f"Please install it using the following command:")
    print(f"pip install {missing_module}")
    sys.exit(1)

# URL of the GitHub releases page
GITHUB_RELEASES_URL = "https://api.github.com/repos/FujiNetWIFI/fujinet-firmware/releases"

# Directory to store releases
RELEASES_DIR = "fujinet_releases"

# Global variable for upload baud rate
UPLOAD_BAUDRATE = 460800

# Function to fetch the releases from GitHub
def fetch_releases():
    response = requests.get(GITHUB_RELEASES_URL)
    response.raise_for_status()
    return response.json()

# Function to download and save zip files
def download_zip(url, zip_path):
    response = requests.get(url)
    response.raise_for_status()
    with open(zip_path, 'wb') as f:
        f.write(response.content)

# Function to create releases directory
def create_releases_dir():
    if not os.path.exists(RELEASES_DIR):
        os.makedirs(RELEASES_DIR)

# Function to present the menu to the user
def present_menu(options):
    print("Available options:")
    for idx, option in enumerate(options, start=1):
        print(f"{idx}) {option}")
    selection = int(input("Select an option (number): "))
    return options[selection - 1]

# Function to detect the USB port
def detect_usb_port():
    system = platform.system()
    usb_port = None

    if system == "Darwin":  # macOS
        usb_devices = glob.glob('/dev/cu.usbserial-*')
        if not usb_devices:
            raise OSError("No USB device found")
        elif len(usb_devices) > 1:
            print("There are two USB devices connected to this computer - please disconnect the non-FujiNet or make sure there is just one FujiNet connected and then attempt to flash.")
            exit(1)
        usb_port = usb_devices[0]
    elif system == "Linux":
        usb_devices = glob.glob('/dev/ttyUSB*')
        if not usb_devices:
            raise OSError("No USB device found")
        elif len(usb_devices) > 1:
            print("There are two USB devices connected to this computer - please disconnect the non-FujiNet or make sure there is just one FujiNet connected and then attempt to flash.")
            exit(1)
        usb_port = usb_devices[0]
    else:
        raise OSError("Unsupported operating system")

    return usb_port

# Function to generate esptool commands
def generate_esptool_commands(json_file, usb_port, esptool="esptool.py"):
    with open(json_file, 'r') as f:
        release_info = json.load(f)
    
    # Extract file information
    files_info = release_info.get("files", [])
    
    # Map filenames to their offsets
    file_map = {file_info['filename']: file_info['offset'] for file_info in files_info}
    
    # Paths to your firmware and filesystem files
    firmware_file = 'firmware.bin'
    filesystem_file = 'littlefs.bin'
    
    # Get the offsets
    firmware_offset = file_map.get(firmware_file)
    filesystem_offset = file_map.get(filesystem_file)
    
    # Check if the offsets were found
    if firmware_offset is None or filesystem_offset is None:
        raise ValueError(f"Could not find the required partitions in the {json_file} file.")
    
    # Generate esptool.py commands
    firmware_path = os.path.join(os.path.dirname(json_file), firmware_file)
    filesystem_path = os.path.join(os.path.dirname(json_file), filesystem_file)
    
    # Ensure paths are properly quoted to handle spaces
    firmware_path = f'"{firmware_path}"'
    filesystem_path = f'"{filesystem_path}"'
    
    return (f"{esptool} --port {usb_port} --baud {UPLOAD_BAUDRATE} write_flash {firmware_offset} {firmware_path}",
            f"{esptool} --port {usb_port} --baud {UPLOAD_BAUDRATE} write_flash {filesystem_offset} {filesystem_path}")

# Function to upload firmware using esptool commands
def upload_firmware(esptool_commands):
    for command in esptool_commands:
        print(command)
        result = os.system(command)
        if result != 0:
            print("Flashing failed, please check the connection and try again.")
            exit(1)

# Function to monitor the device
def monitor_device(usb_port):
    print("Monitoring device...")
    monitor_command = f"pio device monitor -b {UPLOAD_BAUDRATE} -p {usb_port}"
    os.system(monitor_command)

def main():
    parser = argparse.ArgumentParser(description="Firmware uploader for FujiNet")
    parser.add_argument('firmware', nargs='?', help="Already downloaded firmware.zip")
    parser.add_argument('-l', '--latest', action='store_true', help="Automatically select the latest release")
    parser.add_argument('-p', '--port', help="Port to use to upload firmware")

    args = parser.parse_args()
    if args.firmware:
        selected_zip_file = args.firmware
    else:
        create_releases_dir()
        releases_data = fetch_releases()

        if args.latest:
            selected_release = releases_data[0]  # The latest release is the first in the list
            print(f"Automatically selected the latest release: {selected_release['name']}")
        else:
            releases = [release["name"] for release in releases_data]
            selected_release_name = present_menu(releases)
            selected_release = next(release for release in releases_data if release["name"] == selected_release_name)

        assets = selected_release.get("assets", [])

        release_dir = os.path.join(RELEASES_DIR, selected_release['name'].replace('/', '_'))
        if not os.path.exists(release_dir):
            os.makedirs(release_dir)

        zip_files = []
        for asset in assets:
            if asset["name"].endswith(".zip"):
                name = asset["name"]
                url = asset["browser_download_url"]
                zip_path = os.path.join(release_dir, name)
                zip_files.append(zip_path)
                if not os.path.exists(zip_path):
                    download_zip(url, zip_path)

        if not args.latest:
            selected_zip_file = present_menu(zip_files)
        else:
            selected_zip_file = zip_files[0]  # Automatically select the first zip file if latest is chosen
    
    if not os.path.exists(selected_zip_file):
        print(f"Selected zip file not found: {selected_zip_file}")
        exit(1)

    selected_dir = os.path.splitext(selected_zip_file)[0]
    if not os.path.exists(selected_dir):
        with zipfile.ZipFile(selected_zip_file, 'r') as zip_ref:
            zip_ref.extractall(selected_dir)
    
    json_file = os.path.join(selected_dir, 'release.json')
    if not os.path.exists(json_file):
        print(f"release.json not found in the selected zip file directory: {selected_dir}")
        exit(1)

    usb_port = args.port
    if not usb_port:
        try:
            usb_port = detect_usb_port()
        except OSError as e:
            print(e)
            exit(1)

    esptool_path = os.path.expanduser("~/.platformio/packages/tool-esptoolpy/esptool.py")
    esptool_commands = generate_esptool_commands(json_file, usb_port, esptool=esptool_path)
    upload_firmware(esptool_commands)

    # Start monitoring the device after flashing
    monitor_device(usb_port)

if __name__ == "__main__":
    main()
