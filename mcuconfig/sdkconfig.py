import kconfiglib

class SDKConfig:
  def __init__(self, path):
    self.path = path
    self.read()
    return

  def read(self):
    self.config = {}
    try:
      lines = open(self.path).read().splitlines()
    except FileNotFoundError:
      return

    for line in lines:
      line = line.strip()
      if not line:
        continue

      if line.startswith("#") and line.endswith(" is not set"):
        # "# CONFIG_FOO is not set" -> {"CONFIG_FOO": None}
        key = line[1:-11].strip()
        self.config[key] = None

      elif "=" in line:
        key, value = line.split("=", 1)
        self.config[key] = value

    return

  def write(self):
    lines = []

    for key in sorted(self.config):
      value = self.config[key]

      if value is None:
        lines.append(f"# {key} is not set")
      else:
        lines.append(f"{key}={value}")

    with open(self.path, "w") as f:
      f.write("\n".join(lines) + "\n")

    return

  def enableESP32S3(self):
    self.config['CONFIG_SPIRAM'] = "y"
    self.config['CONFIG_SPIRAM_MODE_OCT'] = "y"
    return
