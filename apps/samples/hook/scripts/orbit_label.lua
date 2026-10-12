-- Shows the Orbiter's Orbit, a component of the hook game module's C++, in this entity's label,
-- with the period from hook.period, a function the module binds into Lua.
local OrbitLabel = {}

function OrbitLabel:start()
  self.orbiter = world.findByName("Orbiter")
end

function OrbitLabel:update()
  local orbit = self.orbiter and self.orbiter:get("Orbit")
  if orbit == nil then
    return
  end
  local position = self.orbiter:get("Transform").position
  self.entity:set("GuiText", {
    text = string.format("radius %.1f  period %.2f s  x %.2f", orbit.radius, hook.period(orbit.speed), position.x),
  })
end

return OrbitLabel
