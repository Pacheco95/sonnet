-- A kinematic arm turning about the vertical axis, shoving whatever it meets.
local Sweeper = {
  properties = {
    speed = { type = "number", default = 1.2, min = -6, max = 6 }, -- radians per second
  },
}

function Sweeper:start()
  self.angle = 0
end

function Sweeper:update(dt)
  self.angle = self.angle + self.speed * dt
  local transform = self.entity:get("Transform")
  transform.rotation = quat.axisAngle(vec3(0, 1, 0), self.angle)
  self.entity:set("Transform", transform)
end

return Sweeper
