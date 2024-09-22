import datetime
import re
import subprocess
import sys

Import("env")

# Don't do anything if this is an 'uploadfs' or 'erase' target
cmdline = ','.join(sys.argv)
if cmdline.find('buildfs') or cmdline.find('uploadfs'):
    # Change build tool if we are using LittleFS
    if any("FLASH_LITTLEFS" in x for x in env['BUILD_FLAGS']):
        print("\033[1;31mReplacing MKSPIFFSTOOL with mklittlefs\033[1;37m")
        #env.Replace (MKSPIFFSTOOL = "mklittlefs")

# Disable automatic versioning
if 0:
    print("Automatic versioning disabled")

# Don't do anything if nothing has changed
elif len(subprocess.check_output(["git", "diff", "--name-only"], universal_newlines=True)) == 0:
    print("Nothing has changed")

else:
    try:
        ver_commit = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], universal_newlines=True).strip()
        ver_build = subprocess.check_output(["git", "describe", "HEAD"], universal_newlines=True).strip()
    except subprocess.CalledProcessError as e:
        ver_build = "NOGIT"

    header_file = "include/version.h"

    # FIXME - only use current date if there are uncommitted changes
    ver_date = datetime.datetime.utcnow().strftime("%Y-%m-%d %H:%M:%S")

    rxs = {
        'MAJOR': r'^#define FN_VERSION_MAJOR (\w)',
        'MINOR': r'^#define FN_VERSION_MINOR (\w)',
        'BUILD': r'^(#define FN_VERSION_BUILD)',
        'DATE': r'^(#define FN_VERSION_DATE)',
        'FULL': r'^(#define FN_VERSION_FULL)',
    }

    ver_maj = ""
    ver_min = ""
    m = re.match(r"^v([0-9]+)[.]([0-9]+)[.]", ver_build)
    if m:
        ver_maj = m.group(1)
        ver_min = m.group(2)

    txt = [line for line in open(header_file)]

    fout = open(header_file, "w")

    for line in txt:

        for key in rxs:
            m = re.match(rxs[key], line)
            if m is not None:
                break

        if m is not None:
            if key == 'MAJOR':
                if not ver_maj:
                    ver_maj = m.groups(0)[0]
                line = line[:m.span(1)[0]] + ver_maj + "\n"
            elif key == 'MINOR':
                if not ver_min:
                    ver_min = m.groups(0)[0]
                line = line[:m.span(1)[0]] + ver_min + "\n"
            elif key == 'BUILD':
                line = m.groups(0)[0] + " \"" + ver_commit + "\"\n"
            elif key == 'DATE':
                line = m.groups(0)[0] + " \"" + ver_date + "\"\n"
            elif key == 'FULL':
                line = m.groups(0)[0] + " \"" + ver_build + "\"\n"

        fout.write(line)

    fout.close()
