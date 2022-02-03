# FujiNet - Script to add new target platforms

import re, sys, configparser

# add target [SYSTEM] [BUS]
#   copy lib/bus/.template to lib/bus/BUS
#   copy lib/device/.template to lib/device/BUS
#   copy lib/media/.template to lib/device/SYSTEM
#   add include block to lib/bus/*.h
#   add include block to lib/device/*.h
#   add include block to lib/media/*.h
#   add build_platform & build_bus to platformio.ini
