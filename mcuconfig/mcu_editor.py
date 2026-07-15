from tui import *
from collections import OrderedDict

NO_CHOICE = "---"
def IS_CHOICE(val):
  return val is not None and val is not False and val != NO_CHOICE

class MCUEditor:
  class SignalChoices:
    def __init__(self, label, editor):
      self.label = label
      self.editor = editor
      return

    def available(self):
      return self.editor.availablePins(self.label)

    def __getitem__(self, idx):
      return self.available()[idx]

    def __len__(self):
      return len(self.available())

    def __contains__(self, item):
      avail = self.available()
      return item in [x.value for x in avail]

    def index(self, value):
      avail = self.available()
      return [x.value for x in avail].index(value)

    def count(self, value):
      return self.available().count(value)

  def __init__(self, manager, mcu):
    self.manager = manager
    self.mcu = mcu
    return

  def updateSignalMap(self):
    curMap = {field.label: field.value if IS_CHOICE(field.value) else None
              for field in self.signalGroup.fields}
    self.sigMap.update(curMap)
    return

  def busCapChange(self, field):
    self.updateSignalMap()
    newFields = self.signalFields()
    newLabels = set([x.label for x in newFields])
    oldLabels = set([x.label for x in self.signalGroup.fields])
    removed = oldLabels - newLabels

    for field in newFields:
      if not self.signalGroup.fieldWithLabel(field.label):
        self.signalGroup.addField(field)

    for label in removed:
      self.signalGroup.removeField(self.signalGroup.fieldWithLabel(label))

    self.signalGroup.fields.sort(key=lambda f: f.label)
    bounds = self.form.sizeToFit()
    self.form.resize(bounds.size)
    return

  def signalFields(self):
    caps = self.getCurrent(self.capGroup)
    buses = self.getCurrent(self.busGroup)
    signals = {x: None for x in self.manager.mcuSignals(caps, buses)}
    signals.update((k, v) for k, v in self.sigMap.items() if k in signals)
    signals = OrderedDict(sorted(signals.items()))
    return [FormField(key, "choice", value if value is not None else NO_CHOICE,
                      choices=MCUEditor.SignalChoices(key, self))
            for key, value in signals.items()]

  def edit(self):
    self.sigMap = self.mcu.signalAssignments
    self.gpioGroup = FormGroup("GPIO", layout="matrix", fields=[
      FormField(None, "string", x)
      for x in sorted(getattr(self.mcu, 'gpio', []))])
    mcuCaps = self.manager.mcuCapabilities(self.mcu)
    self.capGroup = FormGroup("Capabilities", layout="matrix", fields=[
      FormField(x, "checkbox", x in mcuCaps, callback=self.busCapChange)
      for x in self.manager.capabilities])
    self.busGroup = FormGroup("Buses", layout="matrix", fields=[
      FormField(x, "checkbox", x in self.mcu.buses, callback=self.busCapChange)
      for x in self.manager.buses])
    self.signalGroup = FormGroup("Signals", layout="matrix", fields=self.signalFields())

    fields = [
      FormField("Name", "string", self.mcu.name),
      self.capGroup,
      self.busGroup,
      self.signalGroup,
      self.gpioGroup,
      FormButton("Add GPIO", self.addGPIO),
      FormField("ESP32", "choice", getattr(self.mcu, 'esp32', "esp32"),
                choices=self.manager.allESP32s())
    ]
    self.form = Form(fields)
    result = self.form.run()

    caps = set(self.manager.mcuCapabilities(self.mcu))
    new_caps = set([key for key, value in result['Capabilities'].items() if value])
    if new_caps != caps:
      self.mcu.isDirty = True
      del_caps = caps - new_caps
      for cap in del_caps:
        for signal in self.manager.capabilities[cap]:
          self.mcu.removeSignal(signal)

      for cap in new_caps:
        for signal in self.manager.capabilities[cap]:
          self.mcu.assignSignal(signal, None)

    new_buses = set([key for key, value in result['Buses'].items() if value])
    if new_buses != self.mcu.buses:
      self.mcu.isDirty = True
      self.mcu.buses = new_buses

    signals = {key: val for key, val in result['Signals'].items() if IS_CHOICE(val)}
    if signals != self.mcu.signalAssignments:
      self.mcu.isDirty = True
      del self.mcu.gpio
      for key, val in signals.items():
        self.mcu.assignSignal(key, int(val))

    self.mcu.name = result['Name']
    if self.mcu.name != self.mcu._origName:
      self.mcu.isDirty = True

    if result['ESP32'] != getattr(self.mcu, 'esp32', None):
      self.mcu.isDirty = True
      self.mcu.esp32 = result['ESP32']

    if result['ESP32'] != getattr(self.mcu, 'esp32', None):
      self.mcu.isDirty = True
      self.mcu.esp32 = result['ESP32']

    return

  def availablePins(self, label):
    # Pin choices that are available are any GPIO that hasn't been
    # assigned *or* any signal name, except for the signal itself.
    signals = {field.label: field.value for field in self.signalGroup.fields
               if IS_CHOICE(field.value)}
    signals.pop(label, None)

    gpioUsed = {}
    for key, value in signals.items():
      gpioUsed.setdefault(value, []).append(key)

    gpioLabels = {NO_CHOICE: NO_CHOICE}
    gpio = [int(x) for x in set(self.getCurrent(self.gpioGroup))]
    gpioLabels.update({x: str(x) for x in sorted(gpio)})
    gpioLabels.update({key: f"{key} {' '.join(map(str, items))}".strip()
                       for key, items in gpioUsed.items()})
    return [MenuItem(val, value=key) for key, val in gpioLabels.items()]

  def getCurrent(self, group):
    return [field.value if field.label is None else field.label
            for field in group.fields if IS_CHOICE(field.value)]

  # def getCurrent(self, groupName):
  #   if not hasattr(self, 'form'):
  #     return None
  #   for group in self.form.fields:
  #     if group.label == groupName:
  #       return [field.label for field in group.fields if field.value]
  #   return []

  def addGPIO(self, field):
    self.gpioGroup.addField(FormField(None, "string", None))
    return
