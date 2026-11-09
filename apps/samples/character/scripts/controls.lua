-- The controls overlay (issue #496), on its canvas, which stays enabled: H hides and shows the panel.
-- The mouse line says how looking works now, which depends on whether the application has locked
-- the pointer (issue #495) or the camera falls back to dragging; with a gamepad connected the
-- lines are its controls instead (issue #498), as many lines, so the panel holds them.
local Controls = {}

local KEYS = "W A S D or arrows    move\nShift    walk\nSpace    jump\n%s\nWheel    zoom\nH    hide these controls"
local LOCKED = "Mouse    look"
local DRAGGING = "Drag with a mouse button    look"
local GAMEPAD = "Left stick    move\nPush it part way    walk\nA or Cross    jump\nRight stick    look\n"
  .. "Wheel    zoom\nH    hide these controls"

function Controls:start()
  self.panel = self.entity:child("Panel")
  self.label = self.entity:child("Panel/Label")
  self.text = nil
end

function Controls:update()
  if input.keyPressed("H") and self.panel ~= nil then
    if self.panel:has("Disabled") then
      self.panel:remove("Disabled")
    else
      self.panel:add("Disabled")
    end
  end
  local text = input.gamepadConnected() and GAMEPAD
    or string.format(KEYS, input.mouseLocked() and LOCKED or DRAGGING)
  if text ~= self.text and self.label ~= nil then
    self.text = text
    self.label:set("GuiText", { text = text })
  end
end

return Controls
