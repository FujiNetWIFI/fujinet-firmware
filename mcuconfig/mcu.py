from pinmap_header import PinmapHeader
from platformio_ini import PlatformIOIni
from sdkconfig import SDKConfig
from webui_config import WebUIConfig
import re
import os

# Debug
from tui import TUI

class MCU:
  def __init__(self, name):
    self.isDirty = False
    self.name = name
    self.buses = set()
    self.pinmapMacros = {}

    # self.gpio is a list of all the GPIO numbers this MCU has
    self.gpio = set()

    # self.gpioMap is a dict of which signal name is mapped to which GPIO
    self.gpioMap = {}

    # FIXME - get rid of aliases
    return

  # def jsonLoad(self, path):
  #   base, ext = os.path.splitext(os.path.basename(path))
  #   self.name = base
  #   with open(path) as f:
  #     self.config = json.load(f)
  #   self.buses = set(self.config.get('bus_assignment', []))
  #   self.capabilities = set(self.config.get('capabilities', []))
  #   return

  # @property
  # def signalAssignments(self):
  #   signals = {}
  #   signals.update(getattr(self, 'aliases', {}))
  #   # GPIO is stored GPIO: signal_name, we want signal_name: GPIO
  #   signals.update({value: key for key, value in getattr(self, 'gpio', {}).items()})
  #   return signals

  @property
  def signalAssignments(self):
    return self.gpioMap

  # def signalsToGPIO(self):
  #   signals = self.signalAssignments
  #   for key, value in signals.items():
  #     if not isinstance(value, int):
  #       signals[key] = signals[value]
  #   return signals

  # def assignSignal(self, signal, pin):
  #   if pin in self.gpio or not isinstance(pin, int):
  #     # GPIO pin is already assigned, map new signal to other signal
  #     if not hasattr(self, 'aliases'):
  #       self.aliases = {}
  #     if isinstance(pin, int):
  #       pin = self.gpio[pin]
  #     if pin not in self.aliases and pin not in self.gpio.values():
  #       raise ValueError(f"signal {signal} maps to unknown pin {pin}",
  #                        f"\nGPIO: {self.gpio}",
  #                        f"\nALIASES: {self.aliases}")
  #     self.aliases[signal] = pin
  #     return

  #   self.gpio[pin] = signal
  #   return

  def assignSignal(self, signal, pin):
    self.gpioMap[signal] = pin
    if not hasattr(self, 'gpio'):
      self.gpio = set()
    if pin is not None:
      self.gpio.add(pin)
    return

  def removeSignal(self, signal):
    self.gpioMap.pop(signal, None)
    return

  def gpioFromBus(self, bus, ini, pinmaps, capabilities):
    if ini.fixupEnvSectionName():
      self.isDirty = True
    self.pinmapMacros[bus.name] = ini.pinmapMacro
    self.esp32 = ini.esp32
    self.buses.add(bus.name)
    self.gpioMap.update(pinmaps[self.pinmapMacros[bus.name]])
    self.gpio.update(x for x in self.gpioMap.values() if x is not None)
    return

  def save(self, manager):
    # FIXME - make sure all the support files exist and are updated:
    #  - data/{bus_macro}
    #  - data/webui/config/{full_name}.yaml
    #  - build-platforms/{INI_PREFIX}-{full_name}.ini
    #  - boards/{full_name}.json
    #  - include/pinmap/
    #  - include/pinmap.h
    #  - if S3 need sdkconfig.{full_name}
    #      CONFIG_SPIRAM=y
    #      CONFIG_SPIRAM_MODE_OCT=y
    #      # CONFIG_SPIRAM_MODE_QUAD is not set
    #      CONFIG_SPIRAM_TYPE_AUTO=y

    # FIXME - if self._origName doesn't match self.name, rename files

    for bus in self.buses:
      if bus not in self.pinmapMacros:
        self.pinmapMacros[bus] = self.macrotize("PINMAP_" + manager.pathBaseForMCU(self, bus))
      headerPath = manager.pinmapPathForMCU(self, bus)
      pmapH = PinmapHeader(headerPath)
      pmapH.loadMacros()
      pmapH.loadSections()
      if pmapH.name is None:
        pmapH.name = self.pinmapMacros[bus]
      pmapH.write(bus, self, manager)

      hdir, hfile = os.path.split(headerPath)
      _, rdir = os.path.split(hdir)
      self.updatePinmapH(os.path.join(rdir, hfile))

      # update build-platforms/{INI_PREFIX}-{full_name}.ini
      ini_path = manager.iniPathForMCU(self, bus)
      ini = PlatformIOIni(ini_path, manager.config.INI_PREFIX)
      ini.fixupEnvSectionName()
      ini.bus = bus
      ini.platform = manager.buses[bus].macro
      ini.pinmapMacro = self.pinmapMacros[bus]
      ini.esp32 = self.esp32
      ini.write()

      if self.esp32 == "esp32s3":
        # Make sure sdkconfig supports ESP32-S3
        sdkconf = SDKConfig(manager.sdkconfigPathForMCU(self, bus))
        sdkconf.enableESP32S3()
        sdkconf.write()

      confPath = manager.webuiConfPathForMCU(self, bus)
      if not os.path.exists(confPath):
        newConf = WebUIConfig(manager.buses[bus].webui_conf.path)
        newConf.path = confPath
        newConf.write()

    # raise NotImplementedError("data/{bus_macro}")
    # raise NotImplementedError("boards/{full_name}.json")

    self.isDirty = False
    return

  def updatePinmapH(self, path):
    target_line = f'#include "{path}"'
    pinmap_h_path = "include/pinmap.h"
    with open(pinmap_h_path, "r") as f:
      lines = f.readlines()

    # Check if it's already there (ignoring whitespace)
    if any(target_line in line.strip() for line in lines):
      return

    # Find the index of the last #include line
    last_include_idx = -1
    for i, line in enumerate(lines):
      if line.strip().startswith("#include"):
        last_include_idx = i

    lines.insert(last_include_idx + 1, target_line + "\n")

    with open(pinmap_h_path, "w") as f:
      f.writelines(lines)
    return

  @property
  def shortName(self):
    return self._name.removeprefix("fujinet-")

  @property
  def name(self):
    return self._name

  @name.setter
  def name(self, value):
    if not hasattr(self, '_origName'):
      self._origName = value
    self._name = value
    return

  @staticmethod
  def macrotize(template):
    macro = re.sub(r'[^a-zA-Z0-9]', '_', template).upper()
    if macro and macro[0].isdigit():
        macro = '_' + macro
    return macro
