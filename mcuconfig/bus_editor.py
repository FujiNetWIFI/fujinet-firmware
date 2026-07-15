class BusEditor:
  def __init__(self, manager, bus):
    self.manager = manager
    self.bus = bus
    return

  def edit(self):
    fields = [
      FormField("Name", "string", self.bus.name),
      FormGroup("Signals", layout="matrix", fields=[
        FormField(None, "string", x) for x in self.bus.signals]),
      FormField("Add Signal", "button", None, group="Signals",
                         callback=self.addSignal),
    ]
    form = Form(fields, self.manager.screen)
    result = form.run()
    return

  def addSignal(self, form, field):
    return

