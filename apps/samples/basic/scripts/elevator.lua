-- A kinematic platform that rises and sinks, carrying what rests on it.
local Elevator = {
  height = 3, -- metres
  period = 6, -- seconds for a round trip
}

function Elevator:start()
  self.base = self.entity:get("Transform").position
  self.time = 0
end

function Elevator:update(dt)
  self.time = self.time + dt
  local lift = (1 - math.cos(self.time / self.period * 2 * math.pi)) * 0.5
  local transform = self.entity:get("Transform")
  transform.position = self.base + vec3(0, self.height * lift, 0)
  self.entity:set("Transform", transform)
end

return Elevator
