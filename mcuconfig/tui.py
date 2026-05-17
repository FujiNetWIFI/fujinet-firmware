# Copyright 2025 by Chris Osborn <fozztexx@fozztexx.com>
#
# This library is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License at <http://www.gnu.org/licenses/> for
# more details.

import curses
from curses import textpad
from collections import defaultdict
from dataclasses import dataclass
from typing import Any

class TUI:
  bounds = None

  def __new__(cls, *args, **kwargs):
    if not hasattr(cls, '_instance'):
      cls._instance = super().__new__(cls)
    return cls._instance

  def __init__(self):
    if hasattr(self, "_initialized"):
      return

    self.screen = curses.initscr()
    curses.noecho()
    curses.cbreak()
    self.screen.keypad(True)
    curses.curs_set(0)
    curses.set_escdelay(5)

    h, w = self.screen.getmaxyx()
    TUI.bounds = Rect(Coord(0, 0), Size(w, h))

    self.wallpaper = curses.newwin(h, w, 0, 0)
    self.wallpaper.erase()
    self.wallpaper.refresh()
    self.windows = []
    self._initialized = True
    return

  def cleanup(self):
    # Always reset terminal to normal state
    curses.nocbreak()
    self.screen.keypad(False)
    curses.echo()
    curses.endwin()
    return

  def disable():
    curses.endwin()
    return

  def enable():
    curses.reset_prog_mode()
    return

  def addWindow(self, win):
    self.windows.append(win)
    return

  def removeWindow(self, win):
    self.windows.remove(win)
    return

  def erase(self):
    self.screen.erase()
    return

  def refresh(self):
    self.wallpaper.touchwin()
    self.wallpaper.noutrefresh()
    for win in self.windows:
      win.noutrefresh()
    self.doupdate()
    return

  def doupdate(self):
    curses.doupdate()
    return

  def touchwin(self):
    self.screen.touchwin()
    return

@dataclass
class MenuItem:
  label: str
  heading: bool = False
  value: Any = None

@dataclass
class Coord:
  x: int
  y: int

  def __add__(self, other):
    return Coord(self.x + other.x, self.y + other.y)

  def __sub__(self, other):
    return Coord(self.x - other.x, self.y - other.y)

@dataclass
class Size:
  width: int
  height: int

  def __add__(self, other):
    return Size(self.width + other.width, self.height + other.height)

  def __sub__(self, other):
    return Size(self.width - other.width, self.height - other.height)

class Rect:
  def __init__(self, origin, size=None, opposite=None):
    self.origin = origin
    self.size = size
    if self.size is None:
      self.size = Size(opposite.x - self.origin.x, opposite.y - self.origin.y)
    return

  def intersection(self, aRect):
    x1 = max(self.origin.x, aRect.origin.x)
    y1 = max(self.origin.y, aRect.origin.y)
    x2 = min(self.opposite.x, aRect.opposite.x)
    y2 = min(self.opposite.y, aRect.opposite.y)
    if x1 <= x2 and y1 <= y2:
      return self.__class__(origin=Coord(x1, y1), opposite=Coord(x2, y2))
    return None

  def isWithin(self, coord):
    if coord.x < self.origin.x or coord.y < self.origin.y or \
       coord.x >= self.opposite.x or coord.y >= self.opposite.y:
      return False
    return True

  @property
  def x(self):
    return self.origin.x

  @property
  def y(self):
    return self.origin.y

  @property
  def width(self):
    return self.size.width

  @property
  def height(self):
    return self.size.height

  @property
  def opposite(self):
    return Coord(self.origin.x + self.size.width, self.origin.y + self.size.height)

  def __repr__(self):
    return "Rect: %s %s" % (str(self.origin), str(self.size))

class ChoiceSelector:
  def __init__(self, choices):
    self.choices = choices
    self.choiceIndex = [idx for idx, item in enumerate(self.choices)
                        if self.isSelectable(item)]
    return

  @staticmethod
  def isSelectable(item):
    if not isinstance(item, MenuItem):
      return True
    return item.label and not item.heading

  def itemLabel(self, item):
    val = item
    if isinstance(self.choices, dict):
      val = self.choices[item]
    elif isinstance(item, MenuItem):
      val = item.label
    return str(val) if val is not None else val

  @staticmethod
  def isHeading(item):
    if not isinstance(item, MenuItem):
      return False
    return item.heading

  def closestSelectable(self, choice):
    for idx, val in enumerate(self.choiceIndex):
      if val >= choice:
        return idx
    return 0

  def run(self, parent_win, selected=0, allowWrap=True, origin=None):
    if origin is None:
      origin = Coord(1, 1)
    bounds = Rect(origin,
                  Size(max(len(str(self.itemLabel(c))) for c in self.choices) + 6,
                       len(self.choices) + 2))
    win = Window(bounds, border=True)

    selected = self.closestSelectable(selected)

    while True:
      win.erase()
      for idx, choice in enumerate(self.choices):
        if self.itemLabel(choice) is None:
          continue
        attr = curses.A_REVERSE if self.choiceIndex[selected] == idx else curses.A_NORMAL
        win.paint(Coord(2 + (2 if not self.isHeading(choice) else 0), 1 + idx),
                  self.itemLabel(choice), attr)
      margin = 2
      coordUp = Coord(0, self.choiceIndex[selected] + 1 - margin)
      coordDown = Coord(0, self.choiceIndex[selected] + 1 + margin)
      if not win.isCoordWithinScreen(coordUp) \
         or not win.isCoordWithinScreen(coordDown):
        y = max(margin, win.bounds.origin.y + self.choiceIndex[selected] + 1)
        y = min(TUI.bounds.height - 1 - margin, y)
        y -= self.choiceIndex[selected] + 1
        win.move(Coord(win.bounds.origin.x, y))
      win.refresh()

      key = win.getch()
      if key in (curses.KEY_UP, 16, ord('k')):
        if selected > 0 or allowWrap:
          selected = (selected - 1) % len(self.choiceIndex)

      elif key in (curses.KEY_DOWN, 17, ord('j')):
        if selected < (len(self.choiceIndex) - 1) or allowWrap:
          selected = (selected + 1) % len(self.choiceIndex)

      elif key in (curses.KEY_ENTER, 10, 13):
        break

      elif key == 27:
        selected = None
        break

    win.close()
    if selected is not None:
      selected = self.choiceIndex[selected]
    return selected

class Menu:
  def __init__(self, choices):
    self.choices = choices
    return

  def run(self, selected=0, allowWrap=False):
    chooser = ChoiceSelector(self.choices)
    return chooser.run(TUI, selected, allowWrap=allowWrap)

class FormNode:
  def __init__(self):
    self.parent = None
    self.focus = False
    return

  def getSibling(self, direction):
    if not self.parent:
      return None
    return self.parent.getSiblingOf(self, direction)

  def getFirst(self):
    return self

  def getLast(self):
    return self

  @property
  def form(self):
    if isinstance(self.parent, Form):
      return self.parent
    return self.parent.form

class FormButton(FormNode):
  def __init__(self, label, callback):
    super().__init__()
    self.label = label
    self.callback = callback
    return

  def renderLabel(self, maxWidth):
    return f"[ {self.label[:maxWidth - 4]} ]"

  def draw(self, left, top, maxWidth, labelWidth=None):
    win = self.form.win
    labelStr = self.renderLabel(maxWidth)

    attr = curses.A_REVERSE if self.focus else curses.A_NORMAL
    try:
      win.paint(Coord(left, top), labelStr, attr)
    except curses.error:
      pass  # In case of edge-of-screen glitches
    return 1

  def edit(self, win):
    self.activate()
    return

  def activate(self):
    self.callback(self)
    return

  def width(self, maxWidth=None):
    return len(self.renderLabel(maxWidth))

  def height(self, maxWidth=None):
    return 1

class FormField(FormNode):
  def __init__(self, label, type_, value, *, choices=None, callback=None):
    super().__init__()
    self.label = label
    self.type = type_  # "string", "checkbox", "choice"
    self.value = value
    self.choices = choices if choices is not None else []
    self.callback = callback
    return

  def labelWidth(self):
    return len(self.label) + 2 if self.label else 0

  def width(self, maxWidth=None):
    w = self.labelWidth() + len(self.renderValue())
    return min(maxWidth, w) if maxWidth else w

  def height(self, maxWidth=None):
    return 1

  def draw(self, left, top, maxWidth, labelWidth=None):
    win = self.form.win
    labelStr = f"{self.label}: " if self.label else ""
    valStr = self.renderValue()

    if self.focus:
      margin = 2
      coordUp = Coord(0, top - margin)
      coordDown = Coord(0, top + margin)
      if not win.isCoordWithinScreen(coordUp) \
         or not win.isCoordWithinScreen(coordDown):
        y = max(margin, win.bounds.origin.y + top)
        y = min(TUI.bounds.height - 1 - margin, y)
        y -= top
        win.move(Coord(win.bounds.origin.x, y))

    xoffset = 0
    displayStr = labelStr + valStr
    if labelWidth is not None and (self.type == "checkbox" or self.type == "choice"):
      xoffset = labelWidth - len(labelStr)

    attr = curses.A_REVERSE if self.focus else curses.A_NORMAL
    self.valueXY = (left + xoffset + len(displayStr) - len(valStr), top)
    self.valueWidth = maxWidth - len(labelStr)
    try:
      win.paint(Coord(left + xoffset, top), displayStr[:maxWidth], attr)
    except curses.error:
      pass  # In case of edge-of-screen glitches
    return 1

  def renderValue(self):
    if self.type == "checkbox":
      return "[X]" if self.value else "[ ]"
    elif self.type == "string":
      if self.value is None:
        return "--"
    return str(self.value)

  def toggle(self):
    self.value = not self.value
    if self.callback:
      self.callback(self)
    return

  def edit(self, win):
    if self.type == "checkbox":
      return

    if self.type == "string":
      self._editString(win)
    elif self.type == "choice":
      self._editChoice(win)
    return

  def _editString(self, win, maxlen=40):
    curses.curs_set(1)
    buf = list(str(self.value) if self.value is not None else "")
    pos = len(buf)
    scroll = max(0, pos - self.valueWidth + 1)
    x, y = self.valueXY

    while True:
      # Draw visible portion
      win.moveCursor(Coord(x, y))
      visible = ''.join(buf[scroll:scroll + self.valueWidth])
      win.paint(Coord(x, y), visible.ljust(self.valueWidth))
      win.moveCursor(Coord(x + pos - scroll, y))
      win.refresh()

      key = win.getch()

      if key == 27:
        break
      elif key in (curses.KEY_ENTER, 10, 13):
        self.value = ''.join(buf)
        if self.callback:
          self.callback(self)
        break
      elif key in (8, 127, curses.KEY_BACKSPACE):
        if pos > 0:
          del buf[pos - 1]
          pos -= 1
      elif key in (curses.KEY_LEFT, 2):
        if pos > 0:
          pos -= 1
      elif key in (curses.KEY_RIGHT, 6):
        if pos < len(buf):
          pos += 1
      elif key == 1:
        pos = 0
      elif key == 5:
        pos = len(buf)
      elif 32 <= key <= 126 and len(buf) < maxlen:
        buf.insert(pos, chr(key))
        pos += 1

      scroll = max(0, pos - self.valueWidth + 1)

    curses.curs_set(0)
    return

  def _editChoice(self, parent_win):
    idx = self.choices.index(self.value) if self.value in self.choices else 0
    origin = Coord(parent_win.bounds.x + self.valueXY[0] - 4,
                   parent_win.bounds.y + self.valueXY[1] - idx - 1)
    chooser = ChoiceSelector(self.choices)
    selected = self.choices.index(self.value) if self.value in self.choices else 0
    idx = chooser.run(TUI, selected=selected, origin=origin)
    if idx is not None:
      item = self.choices[idx]
      if isinstance(item, MenuItem):
        item = item.value
      self.value = item
    return

  # def _editChoice(self, parent_win):
  #   idx = self.choices.index(self.value) if self.value in self.choices else 0
  #   bounds = Rect(Coord(parent_win.bounds.x + self.valueXY[0] - 2,
  #                  parent_win.bounds.y + self.valueXY[1] - idx - 1),
  #                 Size(max(len(str(c)) for c in self.choices) + 4,
  #               len(self.choices) + 2))
  #   win = Window(bounds, border=True)

  #   while True:
  #     win.erase()
  #     for i, choice in enumerate(self.choices):
  #       attr = curses.A_REVERSE if i == idx else curses.A_NORMAL
  #       win.paint(Coord(2, 1 + i), str(choice), attr)
  #     margin = 2
  #     coordUp = Coord(0, idx + 1 - margin)
  #     coordDown = Coord(0, idx + 1 + margin)
  #     if not win.isCoordWithinScreen(coordUp) \
  #        or not win.isCoordWithinScreen(coordDown):
  #       y = max(margin, win.bounds.origin.y + idx + 1)
  #       y = min(TUI.bounds.height - 1 - margin, y)
  #       y -= idx + 1
  #       win.move(Coord(win.bounds.origin.x, y))
  #     win.refresh()

  #     key = win.getch()
  #     if key in (curses.KEY_UP, ord('k')):
  #       idx = (idx - 1) % len(self.choices)
  #     elif key in (curses.KEY_DOWN, ord('j')):
  #       idx = (idx + 1) % len(self.choices)
  #     elif key in (curses.KEY_ENTER, 10, 13):
  #       self.value = self.choices[idx]
  #       break
  #     elif key == 27:
  #       break

  #   win.close()
  #   return

class FormGroup(FormNode):
  def __init__(self, label, layout="list", maxWidth=None, fields=None):
    super().__init__()
    self.label = label
    self.layout = layout  # "list" or "matrix"
    self.maxWidth = maxWidth
    self.horizMargin = 2

    self.fields = []
    if fields:
      for field in fields:
        self.addField(field)

    return

  def addField(self, field):
    field.parent = self
    self.fields.append(field)
    return

  def removeField(self, field):
    if field in self.fields:
      field.parent = None
      self.fields.remove(field)
    return

  def fieldWithLabel(self, label):
    for field in self.fields:
      if field.label == label:
        return field
    return None

  def getFirst(self):
    if not self.fields:
      return None

    first = self.fields[0]
    return first.getFirst() if isinstance(first, FormGroup) else first

  def getLast(self):
    if not self.fields:
      return None

    last = self.fields[-1]
    return last.getLast() if isinstance(last, FormGroup) else last

  def getSiblingOf(self, field, direction):
    try:
      idx = self.fields.index(field)
    except ValueError:
      return None

    if self.layout == 'matrix':
      if direction == "right":
        offset = 1
      elif direction == "left":
        offset = -1
      elif direction == "down":
        offset = self.numCols
      elif direction == "up":
        offset = -self.numCols

    else:
      if direction != "up" and direction != "down":
        return None
      offset = 1 if direction == "down" else -1

    idx += offset
    if 0 <= idx < len(self.fields):
      candidate = self.fields[idx]
      if isinstance(candidate, FormGroup):
        return candidate.getFirst() if offset > 0 else candidate.getLast()
      return candidate
    if isinstance(self, FormNode):
      return self.getSibling(direction)
    raise NotImplementedError(idx)

  def computeMatrixLayout(self, maxWidth):
    self._maxWidth = maxWidth
    if not self.fields:
      self.numCols = 0
      self.numRows = 0
      self.fieldWidth = 0
      self.labelWidth = 0
      return

    fieldWidth = max([f.width(maxWidth) for f in self.fields]) + 1
    labelWidth = max([f.labelWidth() for f in self.fields])
    availableWidth = self.maxWidth or (maxWidth - self.horizMargin * 2)
    if self.layout == 'matrix':
      colPad = 1
      numCols = max(1, (availableWidth + colPad) // (fieldWidth + colPad))

      # # Make things a little more rectangular
      # numRows = (len(self.fields) + numCols - 1) // numCols
      # numCols = (len(self.fields) + numRows - 1) // numRows

    else:
      numCols = 1

    numRows = (len(self.fields) + numCols - 1) // numCols

    self.numCols = numCols
    self.numRows = numRows
    self.fieldWidth = fieldWidth
    self.labelWidth = labelWidth
    return

  def width(self, maxWidth=None):
    self.computeMatrixLayout(maxWidth)
    return self.numCols * (self.fieldWidth + 1) - 1 + self.horizMargin * 2

  def height(self, maxWidth):
    if self.layout == 'matrix':
      self.computeMatrixLayout(maxWidth)
      height = self.numRows
    else:
      height = len(self.fields)
    height += 1 + (1 if self.label else 0)
    return height

  def draw(self, left, top, maxWidth):
    win = self.form.win
    height = 1
    top += 1
    self.computeMatrixLayout(maxWidth)
    if self.label:
      win.paint(Coord(self.horizMargin, top), f"== {self.label} ==")
      top += 1
      height += 1

    idx = 0
    for row in range(self.numRows):
      rowHeight = 0
      for col in range(self.numCols):
        if idx >= len(self.fields):
          break
        field = self.fields[idx]
        y = top
        x = col * self.fieldWidth
        h = field.draw(x + self.horizMargin, y, self.fieldWidth, self.labelWidth)
        rowHeight = max(h, rowHeight)
        idx += 1
      top += rowHeight
      height += rowHeight

    return height

  @property
  def value(self):
    return {x.label: x.value for x in self.fields}

class Form:
  def __init__(self, fields, selField=None):
    self.horizMargin = 1
    self.vertMargin = 1

    self.fields = []
    if fields:
      for field in fields:
        self.addField(field)

    self.selected = selField if selField else self.getFirst()
    self.selected.focus = True
    return

  def addField(self, field):
    field.parent = self
    self.fields.append(field)
    return

  def getFirst(self):
    if not self.fields:
      return None

    first = self.fields[0]
    return first.getFirst() if isinstance(first, FormGroup) else first

  def getLast(self):
    if not self.fields:
      return None

    last = self.fields[-1]
    return last.getLast() if isinstance(last, FormGroup) else last

  def getSiblingOf(self, field, direction):
    if direction != "up" and direction != "down":
      return None

    try:
      idx = self.fields.index(field)
    except ValueError:
      return None

    offset = 1 if direction == "down" else -1
    idx += offset
    if 0 <= idx < len(self.fields):
      candidate = self.fields[idx]
      if isinstance(candidate, FormGroup):
        result = candidate.getFirst() if offset > 0 else candidate.getLast()
        if result:
          return result
        return candidate.getSibling(direction)
      return candidate
    return None

  def _draw(self):
    self.win.erase()
    pos = 1
    for field in self.fields:
      pos += field.draw(self.horizMargin, pos,
                        self.win.bounds.width - self.horizMargin * 2)
    self.win.refresh()
    return

  def _format_value(self, field):
    if field.type == "checkbox":
      return "[x]" if field.value else "[ ]"
    elif field.type == "choice":
      return f"> {field.value}"
    return str(field.value)

  def run(self):
    bounds = self.sizeToFit()
    self.win = Window(bounds, border=True)

    while True:
      self._draw()
      key = self.win.getch()
      if key in (curses.KEY_UP, ord('k'), 16):
        self._moveSelection("up")
      elif key in (curses.KEY_DOWN, ord('j'), 14):
        self._moveSelection("down")
      elif key in (curses.KEY_LEFT, ord('h'), 2):
        self._moveSelection("left")
      elif key in (curses.KEY_RIGHT, ord('l'), 6):
        self._moveSelection("right")
      elif key in (curses.KEY_ENTER, 10, 13):
        self.selected.edit(self.win)
      elif key == ord(' '):
        if isinstance(self.selected, FormButton):
          self.selected.activate()
        elif self.selected.type == "checkbox":
          self.selected.toggle()
      elif key in (ord('q'), 27):  # ESC or q
        break

    self.win.close()
    return {x.label: x.value for x in self.fields if hasattr(x, 'value')}

  def _moveSelection(self, direction):
    newSel = self.selected.getSibling(direction)
    if newSel:
      self.selected.focus = False
      self.selected = newSel
      self.selected.focus = True
    return

  def sizeToFit(self):
    max_width = TUI.bounds.width - (self.horizMargin * 2) - 2
    heights = [x.height(max_width) for x in self.fields]
    width = max([x.width(max_width) for x in self.fields])
    height = sum(heights)

    width += self.horizMargin * 2
    height += self.vertMargin * 2

    start_y = (TUI.bounds.height - height) // 2
    start_x = (TUI.bounds.width - width) // 2

    bounds = Rect(Coord(start_x, start_y), Size(width, height))
    return bounds

  def resize(self, size):
    self.win.resize(size)
    return

class Window:
  def __init__(self, bounds, parent=None, border=False):
    self.bounds = bounds
    self.border = border
    self.content = None
    self.tui = TUI()

    self.frame = self.bounds
    if self.border:
      self.frame = Rect(Coord(self.bounds.origin.x - 1, self.bounds.origin.y - 1),
                        Size(self.bounds.width + 2, self.bounds.height + 2))
    self.win = curses.newpad(self.frame.height, self.frame.width)
    self.win.keypad(True)
    self.tui.addWindow(self)
    return

  def paint(self, coord, text, attr=0):
    offset = self.bounds.origin - self.frame.origin
    loc = coord + offset
    self.win.addstr(loc.y, loc.x, text, attr)
    return

  def erase(self):
    self.win.erase()
    if self.border:
      self.win.box()
    return

  def noutrefresh(self):
    intersect = TUI.bounds.intersection(self.frame)
    padOrigin = intersect.origin - self.frame.origin
    screenOrigin = intersect.origin
    screenOpposite = intersect.opposite - Coord(1, 1)
    self.win.touchwin()
    self.win.noutrefresh(padOrigin.y, padOrigin.x, screenOrigin.y, screenOrigin.x,
                         screenOpposite.y, screenOpposite.x)
    return

  def refresh(self):
    self.tui.refresh()
    return

  def getch(self):
    return self.win.getch()

  def close(self):
    self.tui.removeWindow(self)
    return

  def move(self, coord):
    offset = self.frame.origin - self.bounds.origin
    self.bounds.origin = coord
    self.frame.origin = self.bounds.origin + offset
    return

  def isCoordWithinScreen(self, coord):
    intersect = TUI.bounds.intersection(self.bounds)
    coord += self.bounds.origin
    return intersect.isWithin(coord)

  def moveCursor(self, coord):
    offset = self.bounds.origin - self.frame.origin
    loc = coord + offset
    self.win.move(loc.y, loc.x)
    return

  def resize(self, size):
    padding = self.frame.size - self.bounds.size
    self.bounds.size = size
    self.frame.size = self.bounds.size + padding
    self.win.resize(self.frame.size.height, self.frame.size.width)
    return
