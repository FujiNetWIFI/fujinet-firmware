# Create a compressed ZIP file of FujiNet firmware
# for use with the FujiNet-Flasher tool

Import("env")

platform = env.PioPlatform()

import sys, os, configparser, shutil, re, subprocess
import json
from os.path import join
from datetime import datetime
from zipfile import ZipFile
from pathlib import Path

print("Build firmware ZIP enabled")

ini_file = 'platformio.ini'
# this is specified with "-c /path/to/your.ini" when running pio
if env["PROJECT_CONFIG"] is not None:
    ini_file = env["PROJECT_CONFIG"]

print(f"Reading from config file {ini_file}")

def makezip(source, target, env):
    # Make sure all the files are built and ready to zip
    zipit = True
    if not os.path.exists(env.subst("$BUILD_DIR/bootloader.bin")):
        print("BOOTLOADER not available to pack in firmware zip")
        zipit = False
    if not os.path.exists(env.subst("$BUILD_DIR/partitions.bin")):
        print("PARTITIONS not available to pack in firmware zip")
        zipit = False
    if not os.path.exists(env.subst("$BUILD_DIR/firmware.bin")):
        print("FIRMWARE not available to pack in firmware zip")
        zipit = False
    if not os.path.exists(env.subst("$BUILD_DIR/littlefs.bin")):
        print("LittleFS not available to pack in firmware zip, run \"Build Filesystem Image\" first")
        zipit = False
    if not os.path.exists(env.subst("$BUILD_DIR/update.bin")):
        # Create temporary update partition file
        # When mlff is ready it will replace this but we need *something* for now
        update_path = Path(env.subst("$BUILD_DIR/update.bin"))
        size = 384 * 1024
        with open(update_path, "wb") as f:
            f.seek(size - 1)
            f.write(b"\0")
        print(f"Created fake {update_path} with {size} bytes of zeros")

    if zipit == True:
        # Get the build_board variable
        config = configparser.ConfigParser()
        config.read(ini_file)
        environment = "env:"+config['fujinet']['build_board'].split()[0]
        print(f"Creating firmware zip for FujiNet ESP32 Board: {config[environment]['board']}")

        # Get version information
        with open("include/version.h", "r") as file:
            version_content = file.read()
        defines = re.findall(r'#define\s+(\w+)\s+"?([^"\n]+)"?\n', version_content)

        version = {}
        for define in defines:
            name = define[0]
            value = define[1]
            version[name] = value

        # Get and clean the current commit message
        try:
            version_desc = subprocess.getoutput("git log -1 --pretty=%B | tr '\n' ' '")
        except subprocess.CalledProcessError as e:
            # Revert to full version if no commit msg or error
            version_desc = version['FN_VERSION_FULL']

        try:
            version_build = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], universal_newlines=True).strip()
        except subprocess.CalledProcessError as e:
            version_build = "NOGIT"

        version['FN_VERSION_DESC'] = version_desc
        version['FN_VERSION_BUILD'] = version_build
        version['BUILD_DATE'] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        # Create the 'firmware' output dir if it doesn't exist
        firmdir = 'firmware'
        if not os.path.exists(firmdir):
            os.makedirs(firmdir)

        # Filename variables
        releasefile = firmdir+"/release.json"
        if 'platform_name' in config['fujinet']:
            firmwarezip = firmdir+"/fujinet-"+config['fujinet']['platform_name']+"-"+version['FN_VERSION_FULL']+".zip"
        else:
            firmwarezip = firmdir+"/fujinet-"+config['fujinet']['build_platform'].split("_")[1]+"-"+version['FN_VERSION_FULL']+".zip"

        # Clean the firmware output dir
        try:
            if os.path.isfile(releasefile):
                os.unlink(releasefile)
        except Exception as e:
            print('Failed to delete %s. Reason: %s' % (releasefile, e))
        try:
            if os.path.isfile(firmwarezip):
                os.unlink(firmwarezip)
        except Exception as e:
            print('Failed to delete %s. Reason: %s' % (firmwarezip, e))

        # Create release JSON
        json_contents = {
            "version": version['FN_VERSION_FULL'],
            "version_date": version['FN_VERSION_DATE'],
            "build_date": version['BUILD_DATE'],
            "description": version['FN_VERSION_DESC'],
            "git_commit": version['FN_VERSION_BUILD'],
            "files": []
        }

        if config[environment]['board'] == "fujinet-v1":
            json_contents['files'] += [
                {
                    "filename": "bootloader.bin",
                    "offset": "0x1000"
                },
                {
                    "filename": "partitions.bin",
                    "offset": "0x8000"
                },
                {
                    "filename": "firmware.bin",
                    "offset": "0x10000"
                },
                {
                    "filename": "update.bin",
                    "offset": "0xA10000"
                },
                {
                    "filename": "littlefs.bin",
                    "offset": "0xA70000"
                }
            ]
        elif config[environment]['board'] == "esp32-s3-wroom-1-n16r8":
            json_contents['files'] += [
                {
                    "filename": "bootloader.bin",
                    "offset": "0x0000"
                },
                {
                    "filename": "partitions.bin",
                    "offset": "0x8000"
                },
                {
                    "filename": "firmware.bin",
                    "offset": "0x10000"
                },
                {
                    "filename": "update.bin",
                    "offset": "0xA10000"
                },
                {
                    "filename": "littlefs.bin",
                    "offset": "0xA70000"
                }
            ]
        elif config[environment]['board'] == "fujinet-v1-8mb":
            json_contents['files'] += [
                {
                    "filename": "bootloader.bin",
                    "offset": "0x1000"
                },
                {
                    "filename": "partitions.bin",
                    "offset": "0x8000"
                },
                {
                    "filename": "firmware.bin",
                    "offset": "0x10000"
                },
                {
                    "filename": "update.bin",
                    "offset": "0x510000"
                },
                {
                    "filename": "littlefs.bin",
                    "offset": "0x570000"
                }
            ]
        elif config[environment]['board'] == "esp32-s3-wroom-1-n16r8":
            json_contents['files'] += [
                {
                    "filename": "bootloader.bin",
                    "offset": "0x0000"
                },
                {
                    "filename": "partitions.bin",
                    "offset": "0x8000"
                },
                {
                    "filename": "firmware.bin",
                    "offset": "0x10000"
                },
                {
                    "filename": "update.bin",
                    "offset": "0x510000"
                },
                {
                    "filename": "littlefs.bin",
                    "offset": "0x570000"
                }
            ]
        elif config[environment]['board'] == "fujinet-v1-4mb":
            json_contents['files'] += [
                {
                    "filename": "bootloader.bin",
                    "offset": "0x1000"
                },
                {
                    "filename": "partitions.bin",
                    "offset": "0x8000"
                },
                {
                    "filename": "firmware.bin",
                    "offset": "0x10000"
                },
                {
                    "filename": "update.bin",
                    "offset": "0x2E0000"
                },
                {
                    "filename": "littlefs.bin",
                    "offset": "0x340000"
                }
            ]
        elif config[environment]['board'] == "fujinet-iec-nugget":
            json_contents['files'] += [
                {
                    "filename": "bootloader.bin",
                    "offset": "0x1000"
                },
                {
                    "filename": "partitions.bin",
                    "offset": "0x8000"
                },
                {
                    "filename": "firmware.bin",
                    "offset": "0x10000"
                },
                {
                    "filename": "update.bin",
                    "offset": "0xA10000"
                },
                {
                    "filename": "littlefs.bin",
                    "offset": "0xA70000"
                }
            ]
        elif config[environment]['board'] == "esp32-s3-xdrive-n4r2":
            json_contents['files'] += [
                {
                    "filename": "bootloader.bin",
                    "offset": "0x0000"
                },
                {
                    "filename": "partitions.bin",
                    "offset": "0x8000"
                },
                {
                    "filename": "firmware.bin",
                    "offset": "0x10000"
                },
                {
                    "filename": "littlefs.bin",
                    "offset": "0x250000"
                }
            ]            
        # Save Release JSON
        with open('firmware/release.json', 'w') as f:
            f.write(json.dumps(json_contents, indent=4))

        # Create the ZIP File
        try:
            with ZipFile(firmwarezip, 'w') as zip_object:
                zip_object.write(env.subst("$BUILD_DIR/bootloader.bin"), "bootloader.bin")
                zip_object.write(env.subst("$BUILD_DIR/partitions.bin"), "partitions.bin")
                zip_object.write(env.subst("$BUILD_DIR/firmware.bin"), "firmware.bin")
                zip_object.write(env.subst("$BUILD_DIR/update.bin"), "update.bin")
                zip_object.write(env.subst("$BUILD_DIR/littlefs.bin"), "littlefs.bin")
                zip_object.write("firmware/release.json", "release.json")
        finally:
            print("*" * 80)
            print("*")
            print("*   FIRMWARE ZIP CREATED AT: " + firmwarezip)
            print("*")
            print("*" * 80)


    else:
        print("Skipping making firmware ZIP due to error")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", makezip)
env.AddPostAction("buildfs", makezip)
