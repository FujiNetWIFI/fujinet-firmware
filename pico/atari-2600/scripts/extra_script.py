import os

Import("env")

#print(env.Dump())

board = env["BOARD"]
libdeps_dir = env["PROJECT_LIBDEPS_DIR"]

destdir = f'{libdeps_dir}/{board}/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src/ff15/source'

if os.path.isdir(destdir):
    env.Execute(f'cp patches/ffconf.h {destdir}')
