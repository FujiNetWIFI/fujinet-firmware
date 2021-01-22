import datetime
import re
import subprocess
import sys

# Don't do anything if this is an 'uploadfs' or 'erase' target
if sys.argv[9] == 'uploadfs' or sys.argv[9] == 'erase':
    print("This isn't a build target")

# Don't do anything if nothing has changed
elif len(subprocess.check_output(["git", "diff", "--name-only"], universal_newlines=True)) == 0:
    print("Nothing has changed")

else:
    try:
        ver_build = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], universal_newlines=True).strip()
    except subprocess.CalledProcessError as e:
        ver_build = "NOGIT"
    
    header_file = "include/version.h"

    ver_date = datetime.datetime.utcnow().strftime("%Y-%m-%d %H:%M:%S")

    rxs = ['^#define FN_VERSION_MAJOR (\w+)', '^#define FN_VERSION_MINOR (\w+)',
           '^(#define FN_VERSION_BUILD)', '^(#define FN_VERSION_DATE)', '^(#define FN_VERSION_FULL)']

    ver_maj = ""
    ver_min = ""

    txt = [line for line in open(header_file)]

    fout = open(header_file, "w")

    for line in txt:

        for i in range(len(rxs)):
            m = re.match(rxs[i], line)
            if m is not None:
                break

        if m is not None:
            if i == 0:
                ver_maj = m.groups(0)[0]
                fout.write(line)
            elif i == 1:
                ver_min = m.groups(0)[0]
                fout.write(line)
            elif i == 2:
                line = m.groups(0)[0] + " \"" + ver_build + "\"\n"
                fout.write(line)
            elif i == 3:
                line = m.groups(0)[0] + " \"" + ver_date + "\"\n"
                fout.write(line)
            elif i == 4:
                line = m.groups(0)[0] + " \"" + ver_maj + "." + \
                    ver_min + "." + ver_build + "\"\n"
                fout.write(line)
        else:
            fout.write(line)

    fout.close()
