-- Moves its entity along `velocity` (metres per second) from where it starts, and holds still from
-- `stopAt` seconds on (0: never). The TAA harness's boxes and panes use it (docs/rendering.md, "TAA checks").
local Slide = {
  properties = {
    velocity = { type = "vec3", default = { x = 0, y = 0, z = 0 } },
    stopAt = { type = "number", default = 0 },
  },
}

function Slide:start()
  self.origin = self.entity:get("Transform").position
  self.time = 0
end

function Slide:update(dt)
  self.time = self.time + dt
  local t = self.time
  if self.stopAt > 0 then
    t = math.min(t, self.stopAt)
  end
  local transform = self.entity:get("Transform")
  transform.position = self.origin + self.velocity * t
  self.entity:set("Transform", transform)
end

return Slide
