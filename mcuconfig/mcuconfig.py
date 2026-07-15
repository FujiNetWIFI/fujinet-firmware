#!/usr/bin/env python3
import argparse
from types import SimpleNamespace

from mcu_manager import MCUManager

CONFIG = SimpleNamespace(
  PLATFORMS_DIR = "build-platforms",
  PINMAP_H_DIR = "include/pinmap",
  BOARDS_DIR = "boards",
  CAPS_FILE = "capabilities.json",
  BUS_FILE = "buses.json",
  INI_PREFIX = "platformio-",
  WEBUI_CONF_DIR = "data/webui/config",
)

def build_argparser():
  parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
  #parser.add_argument("file", help="input file")
  parser.add_argument("--dump", action="store_true", help="Print all known board configs")
  return parser

def main():
  args = build_argparser().parse_args()

  manager = MCUManager(CONFIG)
  if not args.dump:
    manager.interactive()
  else:
    manager.dump()
  return

if __name__ == "__main__":
  exit(main() or 0)
