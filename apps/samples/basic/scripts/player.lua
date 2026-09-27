-- The player's ball: W, A, S and D roll it across the floor, Space jumps when it stands on
-- something, and a finger held on the screen rolls it towards the point under the finger.
-- Falling off the world puts it back where it started.
local Player = {
  force = 25, -- newtons while a key or a finger is held
  jump = 9, -- newton-seconds, once per press
  fallLimit = -10,
  reach = 100, -- metres a finger's ray looks for the ground
  arrived = 0.3, -- metres from the finger's point at which the ball stops being pushed
}

function Player:start()
  self.home = self.entity:get("Transform").position
  log.info("W, A, S and D roll the ball, Space jumps, a held finger pulls it; click the viewport first")
end

function Player:grounded()
  -- From the ball's centre, skipping its own collider, a little further than its radius.
  return physics.raycast(self.entity:worldPosition(), vec3(0, -1, 0), 0.6, self.entity) ~= nil
end

-- Towards the point under the first finger held on the screen, flat along the ground, or nil.
function Player:towardsTouch()
  local touch = input.touches()[1]
  if touch == nil then
    return nil
  end
  local ray = camera.ray(touch.position)
  local hit = physics.raycast(ray.origin, ray.direction, self.reach, self.entity)
  if hit == nil then
    return nil
  end
  local offset = hit.point - self.entity:worldPosition()
  offset.y = 0
  if offset:length() < self.arrived then
    return nil
  end
  return offset:normalized()
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
  local touch = self:towardsTouch()
  if touch ~= nil then push = push + touch end
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
