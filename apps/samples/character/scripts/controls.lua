-- The controls overlay (issue #496), on its canvas, which stays enabled: H hides and shows the panel,
-- and the mouse line says how looking works now, which depends on whether the application has
-- locked the pointer (issue #495) or the camera falls back to dragging.
local Controls = {}

local TEXT = "W A S D or arrows    move\nShift    walk\nSpace    jump\n%s\nWheel    zoom\nH    hide these controls"
local LOCKED = "Mouse    look"
local DRAGGING = "Drag with a mouse button    look"

function Controls:start()
  self.panel = self.entity:child("Panel")
  self.label = self.entity:child("Panel/Label")
  self.locked = nil
end

function Controls:update()
  if input.keyPressed("H") and self.panel ~= nil then
    if self.panel:has("Disabled") then
      self.panel:remove("Disabled")
    else
      self.panel:add("Disabled")
    end
  end
  local locked = input.mouseLocked()
  if locked ~= self.locked and self.label ~= nil then
    self.locked = locked
    self.label:set("GuiText", { text = string.format(TEXT, locked and LOCKED or DRAGGING) })
  end
end

return Controls
