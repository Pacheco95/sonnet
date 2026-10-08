-- Stands, then walks, then stands again, crossfading between the two clips, and counts its
-- footsteps: the clips are properties because a script only sees assets by identity, and the
-- "footstep" events are in the Walk clip's data, not in this file.
local Walker = {
  properties = {
    idle = { type = "asset" },
    walk = { type = "asset" },
    period = { type = "number", default = 3, min = 0.5, max = 20 }, -- seconds in each clip
    fade = { type = "number", default = 0.4, min = 0, max = 5 }, -- seconds of crossfade
  },
}

function Walker:start()
  self.timer = 0
  self.walking = false
  self.steps = 0
end

function Walker:update(dt)
  self.timer = self.timer + dt
  if self.timer < self.period then
    return
  end
  self.timer = self.timer - self.period
  self.walking = not self.walking
  -- Assigning a clip while `fade` is above zero is a crossfade from the one it replaces.
  self.entity:set("Animator", { clip = self.walking and self.walk or self.idle, fade = self.fade })
end

function Walker:onAnimationEvent(name, argument)
  if name == "footstep" then
    self.steps = self.steps + 1
    log.info(string.format("%s: %s footstep %d", self.entity:name(), argument, self.steps))
  end
end

return Walker
