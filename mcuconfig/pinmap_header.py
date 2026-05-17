import re
import subprocess
from collections import defaultdict

GPIO_PREFIX = "GPIO_NUM_"
PIN_PREFIX = "PIN_"

class PinmapHeader:
  def __init__(self, path):
    self.path = path
    return

  def loadMacros(self):
    self.name = self.getEnablingMacro(self.path)
    if self.name is None:
      self.unprefixed = []
      return

    macros = self.getMacrosFromPreprocess(self.path, f"-D{self.name}")
    expandedMacros = macros.copy()
    for key, value in expandedMacros.items():
      if not value.startswith(GPIO_PREFIX) and value in expandedMacros:
        while value in expandedMacros:
          value = expandedMacros[value]
        expandedMacros[key] = value

    pinNames = [key for key, value in expandedMacros.items()
                if value.startswith(GPIO_PREFIX)]
    self.unprefixed = [key.removeprefix(PIN_PREFIX) for key in pinNames
                       if not key.startswith(PIN_PREFIX)]
    self.pinmap = {}
    for key in pinNames:
      gpio = macros[key]
      if gpio.startswith(GPIO_PREFIX):
        gpio = gpio.removeprefix(GPIO_PREFIX)
        if gpio == "NC":
          gpio = None
        else:
          gpio = int(gpio)
      else:
        gpio = gpio.removeprefix(PIN_PREFIX)
      if gpio is not None and not isinstance(gpio, int):
        gpio = None
      self.pinmap[key.removeprefix(PIN_PREFIX)] = gpio

    return

  def getMacrosFromPreprocess(self, filePath, cppflags):
    try:
      cmd = ['gcc', '-E', '-dD'] + cppflags.split() + [filePath, ]
      output = subprocess.check_output(cmd, universal_newlines=True)
    except subprocess.CalledProcessError as e:
      print(f"Failed to preprocess {file_path}: {e}")
      return {}

    macros = {}
    for line in output.splitlines():
      if line.startswith('#define'):
        parts = line.split(maxsplit=2)
        if len(parts) == 2:
          macros[parts[1]] = ''
        elif len(parts) == 3:
          macros[parts[1]] = parts[2]
    return macros

  def getEnablingMacro(self, headerPath):
    try:
      with open(headerPath) as f:
        for line in f:
          match = re.match(r'#\s*ifdef\s+(\w+)', line)
          if match:
            return match.group(1)
    except FileNotFoundError:
      # Header file may not exist yet
      pass
    return None

  def loadSections(self):
    try:
      lines = open(self.path).read().splitlines()
    except FileNotFoundError:
      # Header file may not exist yet
      self.header = []
      self.body = []
      self.footer = []
      return

    # Split file into header, body, footer

    # Patterns for any preprocessor conditional line
    ifdef_re = re.compile(r"^\s*#\s*if")   # #if, #ifdef, #ifndef
    endif_re = re.compile(r"^\s*#\s*endif")

    body_start = None
    body_end = None
    depth = 0

    for idx, line in enumerate(lines):
      if body_start is None:
        # Still looking for the opening line that contains our macro name
        if ifdef_re.match(line) and self.name in line:
          body_start = idx
          depth = 1

      else:
        # Inside the block — track nesting to find the matching #endif
        if ifdef_re.match(line):
          depth += 1
        elif endif_re.match(line):
          depth -= 1
          if depth == 0:
            body_end = idx
            break

    if body_start is None:
      raise ValueError(f"No #ifdef block containing '{self.name}' found in {self.path}")
    if body_end is None:
      raise ValueError(f"Unclosed #ifdef block for '{self.name}' in {self.path}")

    self.header = lines[:body_start]
    self.body = lines[body_start+1:body_end]
    self.footer = lines[body_end+1:]

    return

  def write(self, bus, mcu, manager):
    # FIXME - why split each bus into its own pinmap.h file? It's still the same board.

    # Walk through body and remove all pin definitions and capability
    # headers, keeping pin comments
    pin_comments = {}
    define_re = re.compile(r"^\s*#\s*define\s+"
                           r"(?P<name>\w+)\s+(?P<value>\S+)(?:\s+(?P<comment>.*))?")
    common_re = re.compile(r'^\s*#\s*include\s+".*common.h"')

    bodyFiltered = []
    for line in self.body:
      if match := define_re.match(line):
        pin_name = match.group('name')
        short_name = pin_name.removeprefix("PIN_")
        if match.group('comment'):
          pin_comments[short_name] = match.group('comment')
        continue

      elif common_re.match(line):
        continue

      bodyFiltered.append(line)

    gpioMap = mcu.gpioMap.copy()
    pin_defs = defaultdict(dict)

    capabilities = manager.mcuCapabilities(mcu)
    for cap in capabilities:
      signals = manager.capabilities[cap]
      if signals:
        for signal in signals:
          gpio_num = gpioMap.pop(signal, None)
          if gpio_num is None:
            gpio_num = GPIO_PREFIX + "NC"
          elif isinstance(gpio_num, int):
            gpio_num = GPIO_PREFIX + str(gpio_num)
          pin_defs[cap][signal] = (gpio_num, pin_comments.get(signal, None))

    busPins = manager.buses[bus].signals
    for signal in busPins:
      gpio_num = gpioMap.get(signal)
      if gpio_num is None:
        gpio_num = GPIO_PREFIX + "NC"
      elif isinstance(gpio_num, int):
        gpio_num = GPIO_PREFIX + str(gpio_num)
      pin_defs[""][signal] = (gpio_num, pin_comments.get(signal, None))

    pin_names = [pin if pin in self.unprefixed else f"PIN_{pin}"
                 for section in pin_defs
                 for pin in pin_defs[section]]
    longest_pin_name = max(len(name) for name in pin_names)
    deflen = len("#define ")
    column = deflen + longest_pin_name + 1
    column = ((column + 7) // 8) * 8
    padding = column - deflen

    with open(self.path, "w") as f:
      for line in self.header:
        print(line, file=f)

      print(f"#ifdef {self.name}", file=f)
      print(file=f)

      for section in pin_defs.keys():
        # if section:
        #   print(f"/* {section} */", file=f)
        for pin, value in pin_defs[section].items():
          comment = f" {value[1]}" if value[1] else ""
          pin_name = pin if pin in self.unprefixed else f"PIN_{pin}"
          print(f"#define {pin_name:<{padding}}{value[0]}{comment}", file=f)
        print(file=f)

      body_it = iter(bodyFiltered)

      prev_blank = True
      for line in body_it:
        if line.strip():
          print(line, file=f)
          prev_blank = False
          break

      for line in body_it:
          is_blank = not line.strip()
          if is_blank and prev_blank:
            continue
          print(line, file=f)
          prev_blank = is_blank

      if not prev_blank:
        print(file=f)

      print(f"#endif /* {self.name} */", file=f)

      for line in self.footer:
        print(line, file=f)

    return
