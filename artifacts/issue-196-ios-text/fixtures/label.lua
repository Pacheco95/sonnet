
local Label = {}
function Label:start()
  self.frames = 0
end
function Label:update(dt)
  self.frames = self.frames + 1
  if self.frames == 12 then
    self.entity:set("GuiText", { text = "Score 12" })
  end
end
return Label
