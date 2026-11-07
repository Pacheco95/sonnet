-- A kinematic platform that shuttles between where it starts and `travel` metres away, easing in and
-- out and resting `pause` seconds at each end. It moves in fixedUpdate, once per physics step, so the
-- body follows it with MoveKinematic and carries what stands on it (docs/physics.md).
local Platform = {
  properties = {
    travel = { type = "vec3", default = { x = 0, y = 0, z = -3 } }, -- metres from the start to the far end
    period = { type = "number", default = 8, min = 1, max = 60 }, -- seconds for a round trip, pauses included
    pause = { type = "number", default = 1, min = 0, max = 30 }, -- seconds resting at each end
  },
}

function Platform:start()
  self.origin = self.entity:get("Transform").position
  self.time = 0
end

-- 0 at the start, 1 at the far end: a pause, an eased leg out, a pause, an eased leg back.
function Platform:progress()
  local leg = math.max(self.period * 0.5 - self.pause, 0.001)
  local t = self.time % self.period
  if t < self.pause then
    return 0
  end
  t = t - self.pause
  if t < leg then
    return (1 - math.cos(t / leg * math.pi)) * 0.5
  end
  t = t - leg
  if t < self.pause then
    return 1
  end
  t = t - self.pause
  return (1 + math.cos(math.min(t / leg, 1) * math.pi)) * 0.5
end

function Platform:fixedUpdate(dt)
  self.time = self.time + dt
  local transform = self.entity:get("Transform")
  transform.position = self.origin + self.travel * self:progress()
  self.entity:set("Transform", transform)
end

return Platform
