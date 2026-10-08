-- A trigger that whatever it is meant for rolls into: it adds its value to the score and goes away.
-- One script serves every pickup; the inspector sets what each is worth and who may take it.
local score = require("score")

local Pickup = {
  properties = {
    value = { type = "integer", default = 1, min = 1, max = 100 },
    -- The name of what may pick it up; empty lets anything that touches it.
    collector = { type = "string", default = "Ball" },
  },
}

function Pickup:onTriggerEnter(other)
  if self.collector ~= "" and other:name() ~= self.collector then
    return
  end
  score.add(self.value)
  log.info(string.format("%s picked up %s: score %d", other:name(), self.entity:name(), score.total))
  self.entity:destroy()
end

return Pickup
