import configparser
import os
import re
import sys

PINMAP_RE = r"-D\s+(PINMAP_\w+)"

class PlatformIOIni:
  def __init__(self, path, INI_PREFIX):
    self.INI_PREFIX = INI_PREFIX
    self.path = path
    self.config = configparser.ConfigParser(interpolation=None)
    self.config.read(self.path)
    return

  def write(self):
    self.setupEnvSection()
    with open(self.path, "w") as f:
      self.config.write(f)
    return

  def setupEnvSection(self):
    if not self.config.has_section(self.envSectionName):
      self.config.add_section(self.envSectionName)
      self.config[self.envSectionName]['build_type'] = "debug"
      self.config[self.envSectionName]['build_flags'] = "\n" \
        "${env.build_flags}\n" \
        "-D PINMAP_UNKNOWN"
    return

  def fixupEnvSectionName(self):
    if self.config.has_section(self.envSectionName):
      return False
    if not self.config.has_section('fujinet'):
      self.config.add_section('fujinet')
    build_board = self.config['fujinet'].get('build_board', "").strip()
    if not build_board or build_board == self.mcu:
      return False

    # This ini was probably copied from another board, fix the section name
    oldSection = f"env:{build_board}"
    if self.config.has_section(oldSection):
      self.config.add_section(self.envSectionName)
      for key, value in self.config.items(oldSection):
        self.config.set(self.envSectionName, key, value)
      self.config.remove_section(oldSection)
    self.config.remove_option('fujinet', 'build_board')
    return True

  @property
  def envSectionName(self):
    return f"env:{self.mcu}"

  @property
  def bus(self):
    return self.config['fujinet'].get('build_bus', "").strip().lower()

  @bus.setter
  def bus(self, value):
    if not self.config.has_section('fujinet'):
      self.config.add_section('fujinet')
    self.config['fujinet']['build_bus'] = value.upper()
    return

  @property
  def platform(self):
    return self.config['fujinet'].get('build_platform',
                                      f"BUILD_{self.bus.upper()}").strip()

  @platform.setter
  def platform(self, value):
    if not self.config.has_section('fujinet'):
      self.config.add_section('fujinet')
    self.config['fujinet']['build_platform'] = value
    return

  # The 'build_board' setting is not used by
  # build.sh/create-platformio-ini.py. Instead it uses the filename.
  @property
  def mcu(self):
    return os.path.splitext(os.path.basename(self.path).removeprefix(self.INI_PREFIX))[0]

  # @property
  # def mcu(self):
  #   return self.config['fujinet'].get('build_board', "").strip()

  # @mcu.setter
  # def mcu(self, value):
  #   if not self.config.has_section('fujinet'):
  #     self.config.add_section('fujinet')
  #   self.config['fujinet']['build_board'] = value
  #   return

  @property
  def pinmapMacro(self):
    if self.envSectionName not in self.config:
      raise ValueError(f"{self.path} has no env section: {self.envSectionName}")

    build_flags_raw = self.config[self.envSectionName].get('build_flags', "")
    matches = re.findall(PINMAP_RE, build_flags_raw)
    if matches:
      return matches[0]
    return None

  @pinmapMacro.setter
  def pinmapMacro(self, value):
    self.setupEnvSection()
    build_flags_raw = self.config[self.envSectionName].get('build_flags', "")
    self.config[self.envSectionName]['build_flags'] = re.sub(PINMAP_RE, f"-D {value}", build_flags_raw)
    return

  @property
  def esp32(self):
    platform = self.config[self.envSectionName].get('platform', "esp32")
    matches = re.findall(r"fujinet[.]([^_]+)", platform)
    return matches[0]

  @esp32.setter
  def esp32(self, value):
    self.setupEnvSection()
    self.config[self.envSectionName]['platform'] = \
      f"espressif32@${{fujinet.{value}_platform_version}}"
    self.config[self.envSectionName]['platform_packages'] = \
      f"${{fujinet.{value}_platform_packages}}"

    if value == "esp32s3":
      self.config[self.envSectionName]['board'] = "esp32-s3-wroom-1-n16r8"
    else:
      self.config[self.envSectionName]['board'] = "fujinet-v1-8mb"

    return
