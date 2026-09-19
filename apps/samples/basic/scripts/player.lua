-- The player's ball: W, A, S and D roll it across the floor, Space jumps when it stands on
-- something. Falling off the world puts it back where it started.
local Player = {
  force = 25, -- newtons while a key is held
  jump = 9, -- newton-seconds, once per press
  fallLimit = -10,
}

function Player:start()
  self.home = self.entity:get("Transform").position
  log.info("W, A, S and D roll the ball, Space jumps; click the viewport first")
end

function Player:grounded()
  -- From the ball's centre, skipping its own collider, a little further than its radius.
  return physics.raycast(self.entity:worldPosition(), vec3(0, -1, 0), 0.6, self.entity) ~= nil
end

function Player:update(dt)
  -- A press lasts one frame, which may fall between fixed steps: keep it for the next one.
  if input.keyPressed("Space") then
    self.jumpRequested = true
  end
  local transform = self.entity:get("Transform")
  if transform.position.y < self.fallLimit then
    transform.position = self.home
    self.entity:set("Transform", transform)
    physics.setLinearVelocity(self.entity, vec3(0, 0, 0))
    physics.setAngularVelocity(self.entity, vec3(0, 0, 0))
  end
end

function Player:fixedUpdate(dt)
  local push = vec3(0, 0, 0)
  if input.keyDown("W") then push = push + vec3(0, 0, -1) end
  if input.keyDown("S") then push = push + vec3(0, 0, 1) end
  if input.keyDown("A") then push = push + vec3(-1, 0, 0) end
  if input.keyDown("D") then push = push + vec3(1, 0, 0) end
  if push:length() > 0 then
    physics.addForce(self.entity, push:normalized() * self.force)
  end
  if self.jumpRequested then
    self.jumpRequested = false
    if self:grounded() then
      physics.addImpulse(self.entity, vec3(0, self.jump, 0))
    end
  end
end

return Player
