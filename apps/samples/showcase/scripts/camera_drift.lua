-- A slow dolly down the pool towards the gate, swaying a little, for the showcase's play mode.
local CameraDrift = {
  speed = 0.32,  -- metres per second
  fromZ = 12.5,   -- z at t = 0
  toZ = 0.5,       -- z the dolly stops at
}

function CameraDrift:start()
  self.time = 0
end

function CameraDrift:update(dt)
  self.time = self.time + dt
  local transform = self.entity:get("Transform")
  local z = math.max(self.fromZ - self.speed * self.time, self.toZ)
  local sway = math.sin(self.time * 0.11)
  transform.position = vec3(1.6 * sway, 1.75 + 0.18 * math.sin(self.time * 0.23), z)
  transform.rotation = quat.euler(0.045 + 0.02 * math.sin(self.time * 0.17), -0.16 * sway, 0)
  self.entity:set("Transform", transform)
end

return CameraDrift
