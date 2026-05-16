#!/usr/bin/env python3
import argparse
import os
import re
import configparser
from collections import defaultdict
import subprocess

def build_argparser():
  parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
  parser.add_argument("platforms", help="path to build-platforms folder with .ini files")
  parser.add_argument("pinmaps", help="path to .h files with MCU specific pinmaps")
  parser.add_argument("--flag", action="store_true", help="flag to do something")
  return parser

def extract_pinmap_define(build_flags):
  """Find the -D PINMAP_XXX macro."""
  matches = re.findall(r"-D\s+(PINMAP_\w+)", build_flags)
  return matches[0] if matches else None

def parse_ini_file(path):
  config = configparser.ConfigParser(interpolation=None)
  config.read(path)

  if 'fujinet' not in config:
    return None

  build_platform = config['fujinet'].get('build_platform', "").strip()
  build_bus = config['fujinet'].get('build_bus', "").strip()
  build_board = config['fujinet'].get('build_board', "").strip()

  mcu_info = {
    'mcu': build_board.removeprefix("fujinet-"),
    'build_platform': build_platform.lower().removeprefix("build_"),
    'pinmap_define': None
  }

  env_section = f"env:{build_board}"
  if env_section not in config:
    raise ValueError(f"{path} has no env section: {env_section}")

  build_flags_raw = config[env_section].get('build_flags', "")
  pinmap_define = extract_pinmap_define(build_flags_raw)
  if pinmap_define:
    mcu_info['pinmap_define'] = pinmap_define

  return build_bus, mcu_info

def parse_all_inis(dirpath):
  bus_to_mcus = defaultdict(list)

  for fname in os.listdir(dirpath):
    if fname.startswith("platformio-") and fname.endswith(".ini"):
      fullpath = os.path.join(dirpath, fname)
      result = parse_ini_file(fullpath)
      if result:
        bus, mcu_info = result
        if bus:
          bus_to_mcus[bus].append(mcu_info)

  return bus_to_mcus

def print_table(bus_to_mcus):
  mcu_width = 0
  for bus, mcu_list in sorted(bus_to_mcus.items()):
    for mcu in mcu_list:
      width = len(mcu['mcu'])
      if width > mcu_width:
        mcu_width = width

  for bus, mcu_list in sorted(bus_to_mcus.items()):
    print(f"\nBus: {bus}")
    print(f"{'MCU':<{mcu_width}} {'Platform':<20} {'Pinmap Define'}")
    print("-" * 60)
    for mcu in mcu_list:
      print(f"{mcu['mcu']:<{mcu_width}} {mcu['build_platform']:<20} {mcu['pinmap_define']}")

  return

def get_enabling_macro(header_path):
  with open(header_path) as f:
    for line in f:
      match = re.match(r'#\s*ifdef\s+(\w+)', line)
      if match:
        return match.group(1)
  return None

def get_macros_from_preprocess(file_path, cppflags):
  try:
    cmd = ['gcc', '-E', '-dM'] + cppflags.split() + [file_path]
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

def load_pinmaps(dirpath):
  pinmaps = {}
  for fname in os.listdir(dirpath):
    if fname.endswith(".h"):
      fullpath = os.path.join(dirpath, fname)
      pinmap_name = get_enabling_macro(fullpath)
      macros = get_macros_from_preprocess(fullpath, f"-D{pinmap_name}")
      pinmap_macros = {key: value for key, value in macros.items()
                       if value.startswith("GPIO_NUM_") and value != "GPIO_NUM_NC"}
      pinmaps[pinmap_name] = pinmap_macros
      #print(fname, pinmap_name, pinmap_macros)
  return pinmaps

def get_bus_unique_pins(buses, pinmaps, target_bus):
  from collections import defaultdict

  # Step 1: Build reverse mapping of pin_name → set of buses that use it
  pin_to_buses = defaultdict(set)

  for bus, mcu_list in buses.items():
    for mcu_info in mcu_list:
      pinmap_define = mcu_info.get('pinmap_define')
      pinmap = pinmaps.get(pinmap_define, {})
      for pin in pinmap.keys():
        pin_to_buses[pin].add(bus)

  # Step 2: Filter pins used only in the target bus
  unique_pins = {pin for pin, bus_set in pin_to_buses.items() if bus_set == {target_bus}}

  return unique_pins

def get_pins_common_to_all(buses, pinmaps):
  all_pinmaps = []

  # Step 1: Collect all pin dicts from all MCUs
  for mcu_list in buses.values():
    for mcu_info in mcu_list:
      pinmap_define = mcu_info.get('pinmap_define')
      if pinmap_define in pinmaps:
        all_pinmaps.append(pinmaps[pinmap_define])

  if not all_pinmaps:
    return {}

  # Step 2: Get intersection of all pin names
  common_pins = set(all_pinmaps[0].keys())
  for pinmap in all_pinmaps[1:]:
    common_pins.intersection_update(pinmap.keys())

  # Step 3: Check if values are identical across all
  result = {}
  for pin in sorted(common_pins):
    values = {pinmap[pin] for pinmap in all_pinmaps}
    if len(values) == 1:
      result[pin] = values.pop()

  return result

def get_common_pin_names(buses, pinmaps):
  all_pin_sets = []

  for mcu_list in buses.values():
    for mcu_info in mcu_list:
      pinmap_define = mcu_info.get('pinmap_define')
      if pinmap_define in pinmaps:
        pinmap = pinmaps[pinmap_define]
        all_pin_sets.append(set(pinmap.keys()))

  if not all_pin_sets:
    return set()

  # Return the intersection of all pin name sets
  return set.intersection(*all_pin_sets)

def print_bus_pin_summary(buses, pinmaps):
  from collections import defaultdict

  # Step 1: Build bus → list of (mcu, pinmap) mappings
  bus_to_mcus = defaultdict(list)
  for bus, mcu_info_list in buses.items():
    for mcu_info in mcu_info_list:
      mcu = mcu_info['mcu']
      pinmap_define = mcu_info.get('pinmap_define')
      pinmap = pinmaps.get(pinmap_define, {})
      bus_to_mcus[bus].append((mcu, pinmap_define, pinmap))

  # Step 2: Process each bus
  for bus, mcu_entries in sorted(bus_to_mcus.items()):
    print(f"\n=== Bus: {bus} ===")

    # Step 3: Determine common pins across all MCUs
    all_pin_sets = [set(pins.keys()) for _, _, pins in mcu_entries]
    common_pins = set.intersection(*all_pin_sets) if all_pin_sets else set()

    # Check that all values are the same across MCUs
    truly_common = {}
    for pin in sorted(common_pins):
      values = {pins.get(pin) for _, _, pins in mcu_entries}
      if len(values) == 1:
        truly_common[pin] = values.pop()

    # Print common pins table
    if truly_common:
      print(f"\nCommon pins (identical across all MCUs for this bus):")
      print(f"{'Pin Name':<30} {'Value':<20}")
      print("-" * 50)
      for pin, val in sorted(truly_common.items()):
        print(f"{pin:<30} {val:<20}")
    else:
      print("\n(No common pins across all MCUs for this bus)")

    # Step 4: Print MCU-specific differences
    for mcu, pinmap_define, pins in sorted(mcu_entries):
      print(f"\nMCU: {mcu} (pinmap: {pinmap_define})")
      print(f"{'Pin Name':<30} {'Value':<20} {'Note'}")
      print("-" * 70)

      for pin, val in sorted(pins.items()):
        if pin not in truly_common:
          note = "Not in common set"
        elif val != truly_common[pin]:
          note = f"Overrides common (was {truly_common[pin]})"
        else:
          continue  # part of truly common set

        print(f"{pin:<30} {val:<20} {note}")

def print_bus_matrix_table(buses, pinmaps):
  from collections import defaultdict

  bus_to_mcus = defaultdict(list)
  for bus, mcu_info_list in buses.items():
    for mcu_info in mcu_info_list:
      mcu = mcu_info['mcu']
      pinmap_define = mcu_info.get('pinmap_define')
      pinmap = pinmaps.get(pinmap_define, {})
      bus_to_mcus[bus].append((mcu, pinmap))

  for bus, mcu_list in sorted(bus_to_mcus.items()):
    print(f"\n=== Bus: {bus} ===")

    # Gather all pin names used by any MCU
    all_pins = set()
    for _, pinmap in mcu_list:
      all_pins.update(pinmap.keys())
    all_pins = sorted(all_pins)

    # Sort MCUs for consistent column order
    mcu_names = [mcu for mcu, _ in mcu_list]

    # Build table rows: pin name + one value per MCU
    rows = []
    for pin in all_pins:
      values = []
      unique_vals = set()
      for _, pinmap in mcu_list:
        val = pinmap.get(pin, '---')
        values.append(val)
        unique_vals.add(val)
      pin_label = pin + (" (common)" if len(unique_vals) == 1 and '---' not in unique_vals else "")
      rows.append((pin_label, values))

    # Print header
    col_width = max(len(mcu) for mcu in mcu_names) + 2
    pin_col_width = max(len(row[0]) for row in rows) + 2
    header = f"{'Pin Name':<{pin_col_width}}" + "".join(f"| {mcu:<{col_width}}" for mcu in mcu_names)
    print(header)
    print("-" * len(header))

    # Print each row
    for pin_label, values in rows:
      line = f"{pin_label:<{pin_col_width}}" + "".join(f"| {val:<{col_width}}" for val in values)
      print(line)

# def write_bus_matrix_html(buses, pinmaps, output_path="bus_pin_summary.html"):
#   from collections import defaultdict

#   bus_to_mcus = defaultdict(list)
#   for bus, mcu_info_list in buses.items():
#     for mcu_info in mcu_info_list:
#       mcu = mcu_info['mcu']
#       pinmap_define = mcu_info.get('pinmap_define')
#       pinmap = pinmaps.get(pinmap_define, {})
#       bus_to_mcus[bus].append((mcu, pinmap))

#   #common_pins = get_pins_common_to_all(buses, pinmaps)
#   common_pins = get_common_pin_names(buses, pinmaps)

#   with open(output_path, 'w') as f:
#     f.write("<!DOCTYPE html><html><head><meta charset='utf-8'>\n")
#     f.write("<style>\n")
#     f.write("table { border-collapse: collapse; margin: 1em 0; }\n")
#     f.write("th, td { border: 1px solid #ccc; padding: 4px 8px; }\n")
#     f.write("thead { background-color: #eee; }\n")
#     f.write("caption { font-weight: bold; margin: 1em 0; }\n")
#     f.write("</style>\n</head><body>\n")

#     for bus, mcu_list in sorted(bus_to_mcus.items()):
#       f.write(f"<table>\n<caption>Bus: {bus}</caption>\n<thead>\n<tr><th>Pin Name</th>")
#       mcu_names = [mcu for mcu, _ in mcu_list]
#       for mcu in mcu_names:
#         f.write(f"<th>{mcu}</th>")
#       f.write("</tr>\n</thead>\n<tbody>\n")

#       bus_pins = get_bus_unique_pins(buses, pinmaps, bus)

#       # Collect all unique pin names
#       all_pins = set()
#       for _, pinmap in mcu_list:
#         all_pins.update(pinmap.keys())
#       all_pins = sorted(all_pins)

#       for pin in all_pins:
#         values = []
#         unique_vals = set()
#         for _, pinmap in mcu_list:
#           val = pinmap.get(pin, '---')
#           values.append(val)
#           unique_vals.add(val)
#         pin_label = pin \
#           + (" (bus)" if pin in bus_pins else "") \
#           + (" (identical)" if len(unique_vals) == 1 and '---' not in unique_vals else "") \
#           + (" (common)" if pin in common_pins else "")
#         f.write(f"<tr><td>{pin_label}</td>")
#         for val in values:
#           f.write(f"<td>{val}</td>")
#         f.write("</tr>\n")

#       f.write("</tbody>\n</table>\n")

#     f.write("</body></html>\n")
#     print(f"HTML written to {output_path}")

def write_bus_matrix_html(buses, pinmaps, output_path="bus_pin_summary.html"):
  from collections import defaultdict

  def get_common_pin_names():
    all_pin_sets = []
    for mcu_list in buses.values():
      for mcu_info in mcu_list:
        pinmap_define = mcu_info.get('pinmap_define')
        if pinmap_define in pinmaps:
          all_pin_sets.append(set(pinmaps[pinmap_define].keys()))
    if not all_pin_sets:
      return set()
    return set.intersection(*all_pin_sets)

  def get_bus_common_pins(pinmaps_list):
    if not pinmaps_list:
      return set()
    common_pins = set(pinmaps_list[0].keys())
    for p in pinmaps_list[1:]:
      common_pins.intersection_update(p.keys())

    result = set()
    for pin in common_pins:
      values = {p[pin] for p in pinmaps_list}
      if len(values) == 1:
        result.add(pin)
    return result

  def get_bus_unique_pins(target_bus):
    pin_to_buses = defaultdict(set)
    for bus, mcu_list in buses.items():
      for mcu_info in mcu_list:
        pinmap_define = mcu_info.get('pinmap_define')
        pinmap = pinmaps.get(pinmap_define, {})
        for pin in pinmap.keys():
          pin_to_buses[pin].add(bus)
    return {pin for pin, bus_set in pin_to_buses.items() if bus_set == {target_bus}}

  # Precompute global common pins (by name only)
  common_global = get_common_pin_names()

  with open(output_path, 'w') as f:
    f.write("<!DOCTYPE html><html><head><meta charset='utf-8'>\n")
    f.write("<style>\n")
    f.write("table { border-collapse: collapse; margin: 1em 0; }\n")
    f.write("th, td { border: 1px solid #ccc; padding: 4px 8px; }\n")
    f.write("thead { background-color: #eee; }\n")
    f.write("caption { font-weight: bold; margin: 1em 0; }\n")
    f.write("</style>\n</head><body>\n")

    for bus, mcu_list in sorted(buses.items()):
      f.write(f"<table>\n<caption>Bus: {bus}</caption>\n<thead>\n<tr><th>Pin Name</th>")
      mcu_names = [mcu_info['mcu'] for mcu_info in mcu_list]
      for mcu in mcu_names:
        f.write(f"<th>{mcu}</th>")
      f.write("</tr>\n</thead>\n<tbody>\n")

      pinmaps_list = []
      for mcu_info in mcu_list:
        pinmap_define = mcu_info.get('pinmap_define')
        pinmap = pinmaps.get(pinmap_define, {})
        pinmaps_list.append(pinmap)

      # Collect all unique pin names for this bus
      all_pins = set()
      for p in pinmaps_list:
        all_pins.update(p.keys())

      # Classify pins
      bus_common_pins = get_bus_common_pins(pinmaps_list)
      bus_unique_pins = get_bus_unique_pins(bus)

      def sort_key(pin):
        if pin in common_global:
          return (0, pin)
        elif pin in bus_common_pins:
          return (1, pin)
        else:
          return (2, pin)

      for pin in sorted(all_pins, key=sort_key):
        label_parts = []
        if pin in common_global:
          label_parts.append("global")
        if pin in bus_common_pins:
          label_parts.append("common")
        if pin in bus_unique_pins:
          label_parts.append("bus")

        pin_label = pin
        if label_parts:
          pin_label += " (" + ", ".join(label_parts) + ")"

        f.write(f"<tr><td>{pin_label}</td>")
        for pinmap in pinmaps_list:
          value = pinmap.get(pin, "---")
          f.write(f"<td>{value}</td>")
        f.write("</tr>\n")

      f.write("</tbody>\n</table>\n")

    f.write("</body></html>\n")
    print(f"HTML written to {output_path}")

def main():
  args = build_argparser().parse_args()

  pinmaps = load_pinmaps(args.pinmaps)
  buses = parse_all_inis(args.platforms)

  # print_table(buses)
  #print_bus_pin_summary(buses, pinmaps)
  #print_bus_matrix_table(buses, pinmaps)
  write_bus_matrix_html(buses, pinmaps)

  return

if __name__ == "__main__":
  exit(main() or 0)
