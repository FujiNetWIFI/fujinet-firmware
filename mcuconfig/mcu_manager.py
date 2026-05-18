import os
import json
from dataclasses import dataclass
import re

from pinmap_header import PinmapHeader
from mcu import MCU
from mcu_editor import MCUEditor
from bus_editor import BusEditor
from platformio_ini import PlatformIOIni
from webui_config import WebUIConfig

from tui import *

@dataclass
class BusConfig:
  name: str
  macro: str
  signals: list
  inis: list
  webui_conf: WebUIConfig

class MCUManager:
  def __init__(self, config):
    self.config = config

    with open(os.path.join(self.config.BOARDS_DIR, self.config.CAPS_FILE)) as f:
      self.capabilities = dict(sorted(json.load(f).items()))
    self.loadBuses(self.config.PLATFORMS_DIR, self.config.PINMAP_H_DIR)
    pinmaps, self.pinmapFiles = self.loadPinmaps(self.config.PINMAP_H_DIR)

    # make sure bus signals aren't shared with another bus or capability
    known_signals = set()
    for signals in self.capabilities.values():
      known_signals.update(signals)
    for bus in self.buses:
      signals = set(self.buses[bus].signals)
      if len(signals) != len(signals - known_signals):
        raise ValueError(f"Bus {bus} shares signals with capability")
      for obus in self.buses:
        if obus == bus:
          continue
        osignals = set(self.buses[obus].signals)
        if len(signals) != len(signals - osignals):
          raise ValueError(f"Bus {bus} shares signals with bus {obus}",
                           signals & osignals)

    self.mcus = {}
    for bus in self.buses.values():
      for ini in bus.inis:
        if ini.mcu not in self.mcus:
          self.mcus[ini.mcu] = MCU(ini.mcu)
        mcu = self.mcus[ini.mcu]
        mcu.gpioFromBus(bus, ini, pinmaps, self.capabilities)
    self.mcus = list(self.mcus.values())
    self.mcus.sort(key=lambda mcu: mcu.shortName)

    # Load WebUI config files
    # No need for one for every board, just one for each bus
    for mcu in self.mcus:
      for bus in mcu.buses:
        if self.buses[bus].webui_conf is not None:
          continue
        confPath = os.path.join(self.config.WEBUI_CONF_DIR, mcu.name + ".yaml")
        if not os.path.exists(confPath):
          continue
        self.buses[bus].webui_conf = WebUIConfig(confPath)

    return

  def loadBuses(self, iniPath, pinmapPath):
    platforms = self.loadPlatforms(iniPath)
    with open(os.path.join(self.config.BOARDS_DIR, self.config.BUS_FILE)) as f:
      signals = json.load(f)

    self.buses = {}
    busNames = set(x.bus for x in platforms)
    for name in sorted(busNames):
      busInis = [x for x in platforms if x.bus == name]
      macro = set(x.platform for x in busInis)
      if len(macro) != 1:
        raise ValueError(f"{name} macro mismatch: {macro}")
      self.buses[name] = BusConfig(name, list(macro)[0], set(signals.get(name, [])), busInis,
                                   None)

    return

  def loadPlatforms(self, dirpath):
    platforms = []
    for fname in os.listdir(dirpath):
      if fname.startswith(self.config.INI_PREFIX) and fname.endswith(".ini"):
        fullpath = os.path.join(dirpath, fname)
        platforms.append(PlatformIOIni(fullpath, self.config.INI_PREFIX))
    return platforms

  def loadPinmaps(self, dirpath):
    pinmaps = {}
    pinmapFiles = {}
    for fname in os.listdir(dirpath):
      if fname.endswith(".h"):
        fullpath = os.path.join(dirpath, fname)
        pmapH = PinmapHeader(fullpath)
        pmapH.loadMacros()
        if not pmapH.name:
          continue

        pinmaps[pmapH.name] = pmapH.pinmap
        pinmapFiles[pmapH.name] = fullpath

    # Validate that all signals are tied to either a bus or a capability
    known_signals = set()
    for signals in self.capabilities.values():
      known_signals.update(signals)
    for bus in self.buses:
      signals = self.buses[bus].signals
      known_signals.update(signals)

    used_signals = set()
    for signals in pinmaps.values():
      used_signals.update(signals.keys())
    unknown_signals = used_signals - known_signals
    if unknown_signals:
      raise ValueError("Unknown signals", sorted(unknown_signals))

    return pinmaps, pinmapFiles

  # def getEnablingMacro(self, headerPath):
  #   with open(headerPath) as f:
  #     for line in f:
  #       match = re.match(r'#\s*ifdef\s+(\w+)', line)
  #       if match:
  #         return match.group(1)
  #   return None

  # def getMacrosFromPreprocess(self, filePath, cppflags):
  #   try:
  #     cmd = ['gcc', '-E', '-dD'] + cppflags.split() + [filePath, ]
  #     output = subprocess.check_output(cmd, universal_newlines=True)
  #   except subprocess.CalledProcessError as e:
  #     print(f"Failed to preprocess {file_path}: {e}")
  #     return {}

  #   macros = {}
  #   for line in output.splitlines():
  #     if line.startswith('#define'):
  #       parts = line.split(maxsplit=2)
  #       if len(parts) == 2:
  #         macros[parts[1]] = ''
  #       elif len(parts) == 3:
  #         macros[parts[1]] = parts[2]
  #   return macros

  def mcuSignals(self, caps=None, buses=None):
    signals = []
    for cap in caps:
      signals.extend(self.capabilities.get(cap, []))
    for busName in buses:
      signals.extend(self.buses[busName].signals)
    return signals

  def mcuCapabilities(self, mcu):
    caps = []
    for cap in self.capabilities:
      if not set(self.capabilities[cap]).isdisjoint(mcu.gpioMap.keys()):
        caps.append(cap)
    return caps

  # def mcuBuses(self, mcu):
  #   buses = []
  #   for bus in self.buses:
  #     if not set(self.buses[bus].signals).isdisjoint(mcu.gpioMap.keys()):
  #       buses.append(bus)
  #   return buses

  def editMCU(self, mcu):
    editor = MCUEditor(self, mcu)
    editor.edit()
    if mcu.isDirty:
      mcu.save(self)
    return

  def editBus(self, bus):
    editor = BusEditor(self, bus)
    editor.edit()
    return

  def editCapability(self, cap):
    # self.screen.erase()
    # self.screen.addstr(5, 5, f"Edit capabilities {cap}")
    # self.screen.refresh()
    # key = self.screen.getch()
    return

  def interactive(self):
    self.screen = TUI()

    menuOptions = []
    menuOptions.append(MenuItem("FujiNets", heading=True))
    menuOptions.extend(MenuItem(x.shortName) for x in self.mcus)
    mcuRange = range(len(menuOptions) - len(self.mcus), len(menuOptions))
    menuOptions.append(MenuItem("New FujiNet..."))

    # menuOptions.append(MenuItem(None))
    # menuOptions.append(MenuItem("Buses", heading=True))
    # menuOptions.extend(MenuItem(x) for x in self.buses)
    # busRange = range(len(menuOptions) - len(self.buses), len(menuOptions))
    # menuOptions.append(MenuItem("New bus..."))

    # menuOptions.append(MenuItem(None))
    # menuOptions.append(MenuItem("Capabilities", heading=True))
    # menuOptions.extend(MenuItem(x) for x in self.capabilities)
    # capsRange = range(len(menuOptions) - len(self.capabilities), len(menuOptions))
    # menuOptions.append(MenuItem("New capability..."))

    menu = Menu(menuOptions)
    selected = 0
    while True:
      self.screen.erase()
      selected = menu.run(selected)
      if selected is None:
        break

      if selected in mcuRange:
        mcu = self.mcus[selected - mcuRange.start]
        self.editMCU(mcu)
        menuOptions[selected].label = mcu.shortName
      elif selected == max(mcuRange) + 1:
        mcu = MCU("untitled-mcu")
        self.mcus.append(mcu)
        self.editMCU(mcu)
        self.mcus.sort(key=lambda mcu: mcu.shortName)

      # elif selected in busRange:
      #   bus = self.buses[selected - busRange.start]
      #   self.editBus(bus)
      #   menuOptions[selected].label = bus.name
      # elif selected == max(mcuRange) + 1:
      #   raise ValueError("NEW BUS")

      # elif selected in capsRange:
      #   self.editCapability(menuOptions[selected].label)
      # elif selected == max(mcuRange) + 1:
      #   raise ValueError("NEW CAP")

      else:
        raise ValueError(menuOptions[selected].label)

    self.screen.cleanup()

    return

  def dump(self):
    for mcu in self.mcus:
      print(mcu.shortName)
      print("  Buses:")
      for idx, bus in enumerate(mcu.buses):
        if idx:
          print()
        mcuBus = self.buses[bus].mcus[mcu.name]
        print(f"    {bus} {mcuBus['bus_macro']}")
        print(f"      {mcuBus['pinmap_macro']}")
        print(f"      {mcuBus['ini']}")
        print(f"      {self.pinmapFiles[mcuBus['pinmap_macro']]}")

        signals = self.buses[bus].signals
        if signals:
          longest = max(len(x) for x in signals)
          for signal in sorted(signals):
            print(f"      {signal+':':<{longest+1}} {mcu.gpioMap.get(signal, '---')}")

      print()

      print("  Capabilities:")
      caps = self.mcuCapabilities(mcu)
      for idx, cap in enumerate(caps):
        if idx:
          print()

        print(f"    {cap}")
        signals = self.capabilities[cap]
        longest = max(len(x) for x in signals)
        for signal in sorted(signals):
          print(f"      {signal+':':<{longest+1}} {mcu.gpioMap.get(signal, '---')}")

      print()

    return

  def pathBaseForMCU(self, mcu, bus):
    fullname = mcu.name
    busNames = set([x.name.lower() for x in self.buses.values()]
                   + [x.macro.removeprefix("BUILD_").lower() for x in self.buses.values()])
    cleaned = fullname
    prefix = ""
    for name in busNames:
      cleaned = cleaned.replace(name, "")
    if cleaned.startswith("fuji"):
      match = re.search(r"\W", cleaned)
      if match:
        prefix = cleaned[:match.start()]
        cleaned = cleaned[match.start():]
      else:
        prefix = cleaned
        cleaned = ""
    cleaned = cleaned.strip("-")
    suffix = re.sub(r"(-)\1+", r"\1", cleaned)
    if prefix:
      prefix += "-"
    prefix += bus.lower()
    if suffix:
      prefix += "-"
    return prefix + suffix

  def iniPathForMCU(self, mcu, bus):
    for ini in self.buses[bus].inis:
      if ini.mcu == mcu.name:
        return ini.path
    base = self.pathBaseForMCU(mcu, bus)
    return os.path.join(self.config.PLATFORMS_DIR, f"{self.config.INI_PREFIX}{base}.ini")

  def webuiConfPathForMCU(self, mcu, bus):
    path = os.path.basename(self.iniPathForMCU(mcu, bus)).removeprefix(self.config.INI_PREFIX)
    return os.path.join(self.config.WEBUI_CONF_DIR, os.path.splitext(path)[0] + ".yaml")

  def pinmapPathForMCU(self, mcu, bus):
    headerPath = self.pinmapFiles.get(mcu.pinmapMacros[bus], None)
    if headerPath:
      return headerPath
    base = self.pathBaseForMCU(mcu, bus)
    return os.path.join(self.config.PINMAP_H_DIR, base + ".h")

  def sdkconfigPathForMCU(self, mcu, bus):
    return f"sdkconfig.{self.pathBaseForMCU(mcu, bus)}"

  def allESP32s(self):
    return list(set(ini.esp32 for bus in self.buses.values() for ini in bus.inis))
