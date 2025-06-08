# Atari 2600 PlusCart-Pico

Latest version of this code is now here: https://github.com/gtortone/PlusCart-Pico

## Description
Atari 2600 PlusCart-Pico is a porting on Al-Nafuur [PlusCart](https://github.com/Al-Nafuur/United-Carts-of-Atari) to Raspberry Pi Pico platform. 

It is being merged into the FujiNet firmware, with the intent of replacing the ESP32-AT firmware, with calls to the fujinet-firmware.

## Features
- SD card support for ROMs
- on-board flash storage (1MB or 16MB) for ROMs
- Internet access and WiFi connection (with ESP8266)
- [PlusROM](http://pluscart.firmaplus.de/pico/?PlusROM) support
- [PlusStore](https://pcart.firmaplus.de/pico/?PlusStore) support

## Hardware
PlusCart-Pico consists of these modules:
- Raspberry Pi Pico (purple version is recommended due to more pins available)
  
  <img src="https://github.com/gtortone/PlusCart-Pico/blob/main/images/rpi-pico.jpg" height="180" width="200" />
  <img src="https://github.com/gtortone/PlusCart-Pico/blob/main/images/rpi-purple.jpg" height="180" width="200" />

- Atari 2600 cartridge breakout board [link](https://github.com/robinhedwards/UnoCart-2600/tree/master/pcbs/cartridge_slot_breakout)

  <img src="https://github.com/gtortone/PlusCart-Pico/blob/main/images/atari.png" height="180" width="200" />

- ESP8266-01 (or ESP32)

  <img src="https://github.com/gtortone/PlusCart-Pico/blob/main/images/esp8266.jpg" width="200" />
  
- SD card breakout board

  <img src="https://github.com/gtortone/PlusCart-Pico/blob/main/images/microsd.jpg" width="200" />
  
- USB UART (optional for debugging purposes)

  <img src="https://github.com/gtortone/PlusCart-Pico/blob/main/images/usb-uart.jpg" width="200" />

### Connections

| RPi Pico pin | module pin |
| ------------- | ------------- |
| GP2...GP14 | cartridge bus ADDR[0...12] |
| GP22...GP29 | cartridge bus DATA[0...7] |
| GP16...GP19 | microSD (MISO, CS, SCK, MOSI) |
| GP0, GP1, GP15 | ESP8266 (TX, RX, RST) |
| GP20, GP21 | USB-UART (RX, TX) |

RPi Pico connections are defined in [board.h](https://github.com/gtortone/PlusCart-Pico/blob/main/include/board.h) 

### Build

Development environment is based on PlatformIO with two board profiles: `pico` and `vccgnd_yd_rp2040`. At first build all dependency library and whole development environment will be automatically installed and configured by PlatformIO.

#### Start the build:

```
pio run -e vccgnd_yd_rp2040
  or
pio run -e pico
```

First run takes some minutes to complete in order to setup PlatformIO environment.

#### Upload firmware on board:

Run `upload` command while pressing BOOTSEL button:

``` 
pio run -e vccgnd_yd_rp2040 -t upload
  or
pio run -e pico -t upload
```

### ESP-AT firmware flashing on ESP

PlusCart-Pico uses ESP as wifi frontend and it requires [ESP-AT](https://github.com/espressif/esp-at) firmware installed on ESP module.

In order to install ESP-AT please refer to [release](https://github.com/espressif/esp-at/releases) page and download [v2.2.1.0_esp8266](https://github.com/espressif/esp-at/releases/tag/v2.2.1.0_esp8266) if you are using ESP8266 module (ESP01, ...) or [v2.2.0.0_esp32](https://github.com/espressif/esp-at/releases/tag/v2.2.0.0_esp32) if you are using ESP32 module (WROVER, WROOM32, ...).

Two additional tools are required to flash the ESP-AT firmware: [esptool](https://github.com/espressif/esptool) and [at.py](https://raw.githubusercontent.com/espressif/esp-at/113702d9bf0224ed15e873bdc09898e804f4bd28/tools/at.py).

#### Patching factory_param.bin

Before flashing the file `customized_partitions/factory_param.bin` needs to be patched to use GPIO3 (RX) and GPIO1 (TX) as UART for AT commands. To proceed with patching use the following command with `at.py` script:

```
at.py modify_bin --tx_pin 1 --rx_pin 3 --input customized_partitions/factory_param.bin
```

The patched file will be named `target.bin` and is available in the working directory.

#### ESP-AT firmware flashing on ESP8266

```
esptool.py --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 2MB 0x8000 partition_table/partition-table.bin 0x9000 ota_data_initial.bin 0x0 bootloader/bootloader.bin 0x10000 esp-at.bin 0xF0000 at_customize.bin 0xFC000 customized_partitions/client_ca.bin 0x106000 customized_partitions/mqtt_key.bin 0x104000 customized_partitions/mqtt_cert.bin 0x108000 customized_partitions/mqtt_ca.bin 0xF1000 target.bin 0xF8000 customized_partitions/client_cert.bin 0xFA000 customized_partitions/client_key.bin
```

#### ESP-AT firmware flashing on ESP32

```
esptool.py --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB 0x8000 partition_table/partition-table.bin 0x10000 ota_data_initial.bin 0xf000 phy_init_data.bin 0x1000 bootloader/bootloader.bin 0x100000 esp-at.bin 0x20000 at_customize.bin 0x24000 customized_partitions/server_cert.bin 0x39000 customized_partitions/mqtt_key.bin 0x26000 customized_partitions/server_key.bin 0x28000 customized_partitions/server_ca.bin 0x2e000 customized_partitions/client_ca.bin 0x30000 target.bin 0x21000 customized_partitions/ble_data.bin 0x3B000 customized_partitions/mqtt_ca.bin 0x37000 customized_partitions/mqtt_cert.bin 0x2a000 customized_partitions/client_cert.bin 0x2c000 customized_partitions/client_key.bin
```


