-- Drops a physics crate from the spawner every few seconds and keeps the newest few.
local Spawner = {
  interval = 1.5, -- seconds
  limit = 12,
  spread = 1.5, -- metres of jitter around the spawner
}

function Spawner:start()
  self.timer = 0
  self.crates = {}
end

function Spawner:update(dt)
  self.timer = self.timer + dt
  if self.timer < self.interval then
    return
  end
  self.timer = self.timer - self.interval

  local crate = world.instantiate("Physics crate")
  local transform = crate:get("Transform")
  local jitter = vec3((math.random() - 0.5) * self.spread, 0, (math.random() - 0.5) * self.spread)
  transform.position = self.entity:worldPosition() + jitter
  transform.rotation = quat.euler(math.random() * math.pi, math.random() * math.pi, 0)
  crate:set("Transform", transform)
  table.insert(self.crates, crate)
  if #self.crates > self.limit then
    local oldest = table.remove(self.crates, 1)
    if oldest:isValid() then
      oldest:destroy()
    end
  end
end

return Spawner
