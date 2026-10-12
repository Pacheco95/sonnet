-- A HUD label that shows the running score that pickup.lua keeps in score.lua.
local score = require("score")

local ScoreLabel = {}

function ScoreLabel:update()
  if self.shown ~= score.total then
    self.shown = score.total
    self.entity:set("GuiText", { text = "Score " .. score.total })
  end
end

return ScoreLabel
