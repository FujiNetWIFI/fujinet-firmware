# Create a compressed ZIP file of FujiNet firmware
# for use with the FujiNet-Flasher tool

Import("env")

platform = env.PioPlatform()

import sys, os, configparser, shutil, re, subprocess
from os.path import join
from datetime import datetime
from zipfile import ZipFile

#sys.path.append(join(platform.get_package_dir("tool-esptoolpy")))
#import esptool


# Create the 'firmware' output dir if it doesn't exist
if not os.path.exists('firmware'):
    os.makedirs('firmware')

# Clean the firmware output dir
folder = 'firmware'
for filename in os.listdir(folder):
    file_path = os.path.join(folder, filename)
    try:
        if os.path.isfile(file_path) or os.path.islink(file_path):
            os.unlink(file_path)
        elif os.path.isdir(file_path):
            shutil.rmtree(file_path)
    except Exception as e:
        print('Failed to delete %s. Reason: %s' % (file_path, e))

# Get the build_board variable
config = configparser.ConfigParser()
config.read('platformio.ini')
environment = "env:"+config['fujinet']['build_board'].split()[0]
print(f"Creating firmware zip for FujiNet ESP32 Board: {config[environment]['board']}")

# Make sure all the files are built
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
if not os.path.exists(env.subst("$BUILD_DIR/spiffs.bin")):
    print("SPIFFS not available to pack in firmware zip, run \"Build Filesystem Image\" first")
    zipit = False

if zipit == False:
    print("Skipping making firmware ZIP due to error")

if zipit == True:
    # {'FN_VERSION_MAJOR': '0', 'FN_VERSION_MINOR': '5', 'FN_VERSION_BUILD': '63d992c8', 'FN_VERSION_DATE': '2023-05-07 08:00:00', 'FN_VERSION_FULL': 'v1.0'}
    with open("include/version.h", "r") as file:
        version_content = file.read()
    defines = re.findall(r'#define\s+(\w+)\s+"?([^"\n]+)"?\n', version_content)

    version = {}
    for define in defines:
        name = define[0]
        value = define[1]
        version[name] = value

    # get commit msg. needs to be cleaned for JSON before using!
    try:
        version_desc = subprocess.check_output(["git", "log", "-1", "--pretty=%B"], universal_newlines=True).strip().replace('"', '')
    except subprocess.CalledProcessError as e:
        version_desc = version['FN_VERSION_FULL']

    try:
        version_build = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], universal_newlines=True).strip()
    except subprocess.CalledProcessError as e:
        version_build = "NOGIT"

    version['FN_VERSION_DESC'] = version_desc
    version['FN_VERSION_BUILD'] = version_build
    version['BUILD_DATE'] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    json_contents = """{
        "version": "%s",
        "version_date": "%s",
        "build_date": "%s",
        "description": "%s",
        "git_commit": "%s",
        "files":
        [
        """ % (version['FN_VERSION_FULL'], version['FN_VERSION_DATE'], version['BUILD_DATE'], version['FN_VERSION_DESC'], version['FN_VERSION_BUILD'])

    if "16mb" in config[environment]['board']:
        json_contents += """		{
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
                        "filename": "spiffs.bin",
                        "offset": "0x910000"
                    }
                ]
            }"""
    elif "8mb" in config[environment]['board']:
        json_contents += """		{
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
                        "filename": "spiffs.bin",
                        "offset": "0x60000"
                    }
                ]
            }"""
    elif "4mb" in config[environment]['board']:
        json_contents += """		{
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
                "filename": "spiffs.bin",
                "offset": "0x250000"
            }
        ]
    }"""

    # Save Release JSON
    # print(json_contents)
    with open('firmware/release.json', 'w') as f:
        f.write(json_contents)

    # Create a ZipFile Object
    firmwarezip = "firmware/fujinet-"+config['fujinet']['build_platform'].split("_")[1]+"-"+version['FN_VERSION_FULL']+".zip"
    with ZipFile(firmwarezip, 'w') as zip_object:
        # Adding files that need to be zipped
        zip_object.write(env.subst("$BUILD_DIR/bootloader.bin"), "bootloader.bin")
        zip_object.write(env.subst("$BUILD_DIR/partitions.bin"), "partitions.bin")
        zip_object.write(env.subst("$BUILD_DIR/firmware.bin"), "firmware.bin")
        zip_object.write(env.subst("$BUILD_DIR/spiffs.bin"), "spiffs.bin")
        zip_object.write("firmware/release.json", "release.json")
        #zip_object.write('E:/Folder to be zipped/Introduction.txt')
