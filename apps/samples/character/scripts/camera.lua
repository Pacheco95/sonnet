-- The sample's follow camera (issue #496), on the entity with the Camera, outside the character.
--
-- It places itself in `lateUpdate`, after physics has interpolated the character to where this frame
-- draws it (ADR-0038). The mouse's motion is a displacement, so it turns the view as it is, never
-- scaled by dt nor read per fixed step. It asks for the pointer to be locked to the game; until the
-- application grants that (issue #495), dragging with either button looks around.
--
-- The view looks at a pivot above the character's feet, which follows them with smoothing that
-- does not depend on the frame rate. Two sphere sweeps keep it out of the geometry, ignoring the
-- character (triggers are never hit): one from the character's chest up to the pivot, so a pivot
-- under a low ceiling or in a wall starts from inside the capsule instead; and one from there back
-- to the camera. The sphere holds the near plane's corners, so nothing is clipped. A hit pulls the
-- camera in at once; when the way clears, it eases back out, never further than the sweep allows.
-- Close to the pivot the character fades out, dithered by its MeshRenderer's alpha, so it never
-- pops; it keeps casting its shadow.
local FollowCamera = {
  properties = {
    target = { type = "entity" }, -- the character; the scene's "Character" when unset
    height = { type = "number", default = 1.55, min = 0, max = 5 }, -- m, the pivot above the feet
    distance = { type = "number", default = 4, min = 0.5, max = 20 }, -- m, from the pivot
    minDistance = { type = "number", default = 1.5, min = 0.5, max = 20 }, -- m, zoomed in
    maxDistance = { type = "number", default = 7, min = 0.5, max = 20 }, -- m, zoomed out
    yaw = { type = "number", default = 0, min = -180, max = 180 }, -- degrees at the start, 0 north
    pitch = { type = "number", default = -12, min = -89, max = 89 }, -- degrees at the start
    pitchMin = { type = "number", default = -70, min = -89, max = 89 }, -- degrees, looking down
    pitchMax = { type = "number", default = 40, min = -89, max = 89 }, -- degrees, looking up
    sensitivity = { type = "number", default = 0.15, min = 0.001, max = 5 }, -- degrees per pixel
    follow = { type = "number", default = 12, min = 0, max = 100 }, -- 1/s, horizontal follow
    rise = { type = "number", default = 6, min = 0, max = 100 }, -- 1/s, vertical follow
    ease = { type = "number", default = 3, min = 0.1, max = 100 }, -- 1/s, back out after a hit
    margin = { type = "number", default = 0.05, min = 0, max = 1 }, -- m beyond the near plane
    aspect = { type = "number", default = 2, min = 1, max = 4 }, -- the widest view the probe covers
    orbit = { type = "number", default = 0, min = -360, max = 360 }, -- degrees per second, for captures
    trail = { type = "number", default = 0, min = 0, max = 20 }, -- 1/s, swing behind a moving character
    fadeStart = { type = "number", default = 1.0, min = 0, max = 5 }, -- m: the character starts to fade
    fadeEnd = { type = "number", default = 0.6, min = 0, max = 5 }, -- m: and is gone; never inside its head
  },
}

local function clamp(x, lo, hi)
  return math.max(lo, math.min(hi, x))
end

local function smooth(current, target, sharpness, dt)
  return target + (current - target) * math.exp(-sharpness * dt)
end

-- Every MeshRenderer at and under an entity, with its colour, to fade the character as the camera
-- closes in.
local function renderers(entity, out)
  if entity:has("MeshRenderer") then
    table.insert(out, { entity = entity, color = entity:get("MeshRenderer").color })
  end
  for _, child in ipairs(entity:children()) do
    renderers(child, out)
  end
  return out
end

-- A sweep that stops where the probe would first touch something, or goes all the way.
function FollowCamera:sweep(from, to)
  local offset = to - from
  local length = offset:length()
  if length < 1e-4 then
    return to, length
  end
  local hit = physics.sphereCast(from, self.radius, offset, length, self.target)
  if hit == nil then
    return to, length
  end
  return from + offset * (hit.distance / length), hit.distance
end

function FollowCamera:start()
  self.target = self.target or world.findByName("Character")
  local camera = self.entity:get("Camera") or { fovY = math.rad(55), nearPlane = 0.1 }
  -- The sphere through the near plane's corners at the widest aspect, plus a margin.
  local halfHeight = camera.nearPlane * math.tan(camera.fovY / 2)
  local halfWidth = halfHeight * self.aspect
  self.radius = math.sqrt(camera.nearPlane ^ 2 + halfHeight ^ 2 + halfWidth ^ 2) + self.margin
  self.yawAngle = math.rad(self.yaw)
  self.pitchAngle = math.rad(clamp(self.pitch, self.pitchMin, self.pitchMax))
  self.wanted = clamp(self.distance, self.minDistance, self.maxDistance)
  self.current = self.wanted
  self.pivot = nil
  self.meshes = self.target ~= nil and renderers(self.target, {}) or {}
  -- Gone by fadeEnd, kept where the near plane's sphere still clears the character's capsule, so
  -- the near plane never cuts into a character that is still drawn.
  local controller = self.target ~= nil and self.target:get("CharacterController") or nil
  local clearance = self.radius + (controller ~= nil and controller.radius or 0.3)
  self.fadeTo = math.max(self.fadeEnd, clearance)
  self.fadeFrom = math.max(self.fadeStart, self.fadeTo)
  self.fade = 1
  input.setMouseLocked(true)
end

function FollowCamera:lateUpdate(dt)
  if self.target == nil or not self.target:isValid() then
    return
  end
  -- Looking around: the mouse's motion this frame, while locked or dragging.
  local looking = input.mouseLocked() or input.mouseDown("Left") or input.mouseDown("Right")
  if looking then
    local delta = input.mouseDelta()
    self.yawAngle = self.yawAngle - math.rad(delta.x * self.sensitivity)
    self.pitchAngle = self.pitchAngle - math.rad(delta.y * self.sensitivity)
  end
  -- Trailing, for captures and routes: the yaw swings behind the character's motion over the ground.
  local state = self.trail > 0 and not looking and physics.character(self.target) or nil
  if state ~= nil and state.grounded then
    local v = state.velocity - state.supportVelocity
    local speed = math.sqrt(v.x * v.x + v.z * v.z)
    if speed > 1 then
      local behind = math.atan(-v.x, -v.z)
      local delta = (behind - self.yawAngle + math.pi) % (2 * math.pi) - math.pi
      self.yawAngle = self.yawAngle + delta * (1 - math.exp(-self.trail * dt)) * math.min(speed / 3, 1)
    end
  end
  self.yawAngle = (self.yawAngle + math.rad(self.orbit) * dt + math.pi) % (2 * math.pi) - math.pi
  self.pitchAngle = clamp(self.pitchAngle, math.rad(self.pitchMin), math.rad(self.pitchMax))
  local wheel = input.wheel().y
  if wheel ~= 0 then
    self.wanted = clamp(self.wanted - wheel * 0.5, self.minDistance, self.maxDistance)
  end

  -- Following: a pivot above the feet as drawn; a respawn or a teleport moves it at once.
  local feet = self.target:worldPosition()
  local focus = feet + vec3(0, self.height, 0)
  if self.pivot == nil or (focus - self.pivot):length() > 3 then
    self.pivot = focus
    self.current = self.wanted
  else
    self.pivot = vec3(smooth(self.pivot.x, focus.x, self.follow, dt), smooth(self.pivot.y, focus.y, self.rise, dt),
      smooth(self.pivot.z, focus.z, self.follow, dt))
  end

  -- Collision: from inside the capsule to the pivot, then back towards the camera.
  local chest = feet + vec3(0, math.min(1.0, self.height), 0)
  local pivot = self:sweep(chest, self.pivot)
  local rotation = quat.euler(self.pitchAngle, self.yawAngle, 0)
  local back = rotation * vec3(0, 0, 1)
  local _, allowed = self:sweep(pivot, pivot + back * self.wanted)
  if allowed < self.current then
    self.current = allowed -- in at once
  else
    self.current = math.min(smooth(self.current, allowed, self.ease, dt), allowed) -- out gently
  end
  self.entity:set("Transform", { position = pivot + back * self.current, rotation = rotation })

  -- Pressed against a wall, the camera would be inside the character's head: fade it out on the way,
  -- through the MeshRenderer's alpha, which dithers an opaque draw and leaves its shadow whole.
  local fade = 1
  if self.current < self.fadeFrom then
    local band = self.fadeFrom - self.fadeTo
    fade = band > 0 and clamp((self.current - self.fadeTo) / band, 0, 1) or 0
  end
  if fade ~= self.fade then
    self.fade = fade
    for _, mesh in ipairs(self.meshes) do
      if mesh.entity:isValid() then
        local c = mesh.color
        mesh.entity:set("MeshRenderer", { color = { x = c.x, y = c.y, z = c.z, w = c.w * fade } })
      end
    end
  end
end

return FollowCamera
