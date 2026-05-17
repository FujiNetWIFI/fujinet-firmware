import yaml

class WebUIConfig:
  def __init__(self, path):
    self.path = path
    with open(self.path, "r") as f:
      self.config = yaml.safe_load(f)
    return

  def write(self):
    with open(self.path, "w") as f:
      yaml.safe_dump(self.config, f, default_flow_style=False, sort_keys=False)
    return
