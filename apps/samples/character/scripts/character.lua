-- The sample's third-person character (issue #496). It sits on the root that carries the
-- CharacterController, which owns the capsule; the "Model" child is the humanoid, which this script
-- turns to face where it goes and whose Animator it drives. The camera is a separate entity
-- (camera.lua), whose yaw turns the input.
--
-- The frame (ADR-0038): `update` notes a jump press, which lasts one rendered frame, and keeps it
-- for `jumpBuffer` seconds; `fixedUpdate` reads the keys, which are state, accelerates the intended
-- velocity, hands it and any jump to the controller, and sets the animation weights from what the
-- step just did, so the same frame's playback applies them. Facing is turned per rendered frame.
--
-- Animation follows the measured motion, not the keys: the ground speed is the controller's
-- velocity less its support's, so walking into a wall slows the feet and a platform carries a
-- character that stands. The Animator plays Walk as its clip and Run synchronised to it; the
-- playback rate is the ground speed over the blended stride, so a foot on the ground stays put.
-- Idle, the jump, the fall and the two landings are unsynchronised layers whose weights fade.
--
-- Turning on the spot (issue #528): from standing, an input more than `turnThreshold` off the way
-- the model faces plays a turn clip instead of swinging the idle round on its feet. The clips have
-- their turn taken out at a constant rate, so the model turns by the clip's measured angle over
-- its length, with nothing on top, and the feet stay where the clip puts them. The character waits
-- for the turn before it moves off; if it moves off first, the clip fades into the walk or run and
-- the usual facing finishes the turn. The capsule never turns, and root motion stays off.
--
-- With `route` set, the route's children, in order, steer the character instead of the keys,
-- through the same intent, acceleration and jump buffer (see the README).
local Character = {
  properties = {
    camera = { type = "entity" }, -- whose yaw turns the keys; the scene's "Camera" when unset
    route = { type = "entity" }, -- waypoints to follow instead of the keys; none by default
    runSpeed = { type = "number", default = 4.8, min = 0, max = 10 }, -- m/s
    walkSpeed = { type = "number", default = 1.5, min = 0, max = 10 }, -- m/s, with Shift held
    acceleration = { type = "number", default = 12, min = 0.1, max = 100 }, -- m/s², on the ground
    braking = { type = "number", default = 16, min = 0.1, max = 100 }, -- m/s², slowing or turning back
    airControl = { type = "number", default = 0.3, min = 0, max = 1 }, -- of the acceleration, airborne
    facingRate = { type = "number", default = 540, min = 1, max = 3600 }, -- degrees per second at most
    jumpSpeed = { type = "number", default = 5.0, min = 0.1, max = 20 }, -- m/s up: 1.27 m
    jumpBuffer = { type = "number", default = 0.15, min = 0, max = 1 }, -- s a press waits for ground
    coyoteTime = { type = "number", default = 0.12, min = 0, max = 1 }, -- s after leaving the ground
    walkStride = { type = "number", default = 1.505, min = 0.1, max = 10 }, -- m per Walk cycle (#505)
    runStride = { type = "number", default = 3.235, min = 0.1, max = 10 }, -- m per Run cycle (#505)
    hardLanding = { type = "number", default = 7, min = 0, max = 50 }, -- m/s down: HardLand above it
    routeTolerance = { type = "number", default = 0.4, min = 0.05, max = 5 }, -- m to count as arrived
    turnThreshold = { type = "number", default = 60, min = 0, max = 180 }, -- degrees; 180 turns no clip
    turnSpeed = { type = "number", default = 1.25, min = 0.1, max = 4 }, -- the turn clips' playback rate
    turnHold = { type = "number", default = 0.6, min = 0, max = 5 }, -- s a turn may hold the move back
  },
}

-- The humanoid's clips (assets/character/humanoid.glb), by their identities in its sidecar, and
-- their lengths in seconds.
local CLIPS = {
  idle = "fa9b9da2-02b9-8a6b-877d-c70e4c2597f8",
  walk = "479a9da2-02b8-8a6b-ba7e-c70e4c2697f8",
  run = "94989da2-02b7-8a6b-ad80-c70e4c2797f8",
  jumpStart = "e1969da2-02b6-8a6b-8874-c70e4c2097f8",
  fall = "2e959da2-02b5-8a6b-bb76-c70e4c2197f8",
  land = "7b939da2-02b4-8a6b-ae78-c70e4c2297f8",
  hardLand = "c8919da2-02b3-8a6b-a179-c70e4c2397f8",
  turnLeft90 = "15909da2-02b2-8a6b-bc6d-c70e4c1c97f8",
  turnRight90 = "628e9da2-02b1-8a6b-af6f-c70e4c1d97f8",
  turnLeft180 = "3eb8cf51-7a29-84fd-b531-fa1c3dd75130",
  turnRight180 = "f1b9cf51-7a2a-84fd-822f-fa1c3dd65130",
}
local DURATION = { walk = 7 / 6, run = 2 / 3, jumpStart = 17 / 30, land = 0.8, hardLand = 22 / 30 }

-- The turn clips' measured angles (radians about +Y, positive to the left) and lengths (seconds),
-- in the order startTurn picks them: left and right quarter turns, then half turns (issue #528).
local TURNS = {
  { clip = CLIPS.turnLeft90, angle = math.rad(92.4), duration = 46 / 30 },
  { clip = CLIPS.turnRight90, angle = math.rad(-90.1), duration = 42 / 30 },
  { clip = CLIPS.turnLeft180, angle = math.rad(180.0), duration = 89 / 30 },
  { clip = CLIPS.turnRight180, angle = math.rad(-176.4), duration = 88 / 30 },
}

-- The Animator's layers, in order. Walk is the Animator's own clip.
local IDLE, RUN, JUMP, FALL, LAND, HARD, TURN = 1, 2, 3, 4, 5, 6, 7

-- A turn on the spot lets the character move off once the model faces within this of the input.
local TURN_RELEASE = math.rad(45)
-- An error at least this large picks a half turn rather than a quarter turn.
local HALF_TURN = math.rad(135)

-- Where JumpStart's feet leave the ground (frame 33 of its 18 to 35): the jump starts the clip a
-- little before it, so the push-off shows without the crouch that would come before the jump.
local TAKEOFF = 0.4

-- The ground speed blends idle into walking up to this (m/s), below the slowest the clips survive.
local MOVING = 0.9

local function clamp(x, lo, hi)
  return math.max(lo, math.min(hi, x))
end

local function smoothstep(lo, hi, x)
  local t = clamp((x - lo) / (hi - lo), 0, 1)
  return t * t * (3 - 2 * t)
end

-- Moves `current` towards `target` by at most `step`.
local function approach(current, target, step)
  if current < target then
    return math.min(current + step, target)
  end
  return math.max(current - step, target)
end

-- Exponential smoothing that does not depend on the frame rate; dt 0 changes nothing.
local function smooth(current, target, sharpness, dt)
  return target + (current - target) * math.exp(-sharpness * dt)
end

local function wrapAngle(a)
  return (a + math.pi) % (2 * math.pi) - math.pi
end

local function horizontal(v)
  return vec3(v.x, 0, v.z)
end

-- The yaw (about +Y) whose forward, -Z turned by it, points along a horizontal direction.
local function yawOf(direction)
  return math.atan(-direction.x, -direction.z)
end

-- A waypoint's cues are words in its name: "walk", "jump", "wait <seconds>", "ride", "turn".
local function parseCues(name)
  local cues = {}
  for word in name:lower():gmatch("%S+") do
    if word == "walk" or word == "jump" or word == "ride" or word == "turn" then
      cues[word] = true
    end
  end
  cues.wait = tonumber(name:lower():match("wait%s+([%d%.]+)"))
  return cues
end

function Character:start()
  self.model = self.entity:child("Model")
  self.cameraEntity = self.camera or world.findByName("Camera")
  self.clock = 0
  self.waypoints = {}
  if self.route ~= nil then
    for _, point in ipairs(self.route:children()) do
      table.insert(self.waypoints, { position = point:worldPosition(), cues = parseCues(point:name()), name = point:name() })
    end
  end
  self:restart()
end

-- Everything a respawn, a teleport or the start of play begins from.
function Character:restart()
  self.velocity = vec3(0, 0, 0) -- the intended horizontal velocity, m/s
  self.jumpPressedAt = nil
  self.lastGroundedAt = -math.huge
  self.jumped = false
  self.jumpedAt = -math.huge
  self.leftGround = false
  self.lastPosition = self.entity:worldPosition()
  self.facing = self:rootYaw() -- where the Model faces, in the world
  self.forward = vec3(0, 0, -1)
  self.airTime = 0
  self.lastAirSpeed = 0
  self.groundSpeed = 0
  self.gait = 0
  self.air = 0
  self.jumpWeight = 0
  self.landWeight = { [LAND] = 0, [HARD] = 0 }
  self.landing = nil
  self.restartLayer = {} -- layer index to the time a one-shot clip starts from, this step
  self.wasAirborne = false
  self.playback = 1
  self.turning = nil -- the turn on the spot playing, if any
  self.turnWeight = 0
  self.settle = nil -- the yaw a finished turn fell short of, turned to smoothly while standing
  self.turnFirst = false -- a route's "turn" cue: stand until the turn has played
  self.waypoint = 1
  self.waitUntil = nil
  self.riding = nil
  self.routeDone = false
  if self.model ~= nil then
    self.model:set("Transform", { rotation = quat.identity() })
    self.model:set("Animator", {
      clip = CLIPS.walk,
      time = 0,
      speed = 1,
      weight = 0,
      playing = true,
      loop = true,
      fade = 0,
      rootMotion = false,
      layers = {
        { clip = CLIPS.idle, weight = 1 },
        { clip = CLIPS.run, weight = 0, sync = true },
        { clip = CLIPS.jumpStart, weight = 0, loop = false, playing = false },
        { clip = CLIPS.fall, weight = 0 },
        { clip = CLIPS.land, weight = 0, loop = false, playing = false },
        { clip = CLIPS.hardLand, weight = 0, loop = false, playing = false },
        { clip = TURNS[1].clip, weight = 0, loop = false, playing = false, speed = self.turnSpeed },
      },
    })
  end
end

function Character:update(dt)
  if #self.waypoints == 0 and input.keyPressed("Space") then
    self.jumpPressedAt = self.clock
  end
  self:turn(dt)
end

-- The camera's yaw as a forward direction on the ground, kept while the camera looks straight down.
function Character:cameraForward()
  if self.cameraEntity ~= nil and self.cameraEntity:isValid() then
    local transform = self.cameraEntity:get("Transform")
    local f = horizontal(transform.rotation * vec3(0, 0, -1))
    if f:length() > 1e-3 then
      self.forward = f:normalized()
    end
  end
  return self.forward
end

-- The keys as a world-space direction no longer than 1, and whether to walk.
function Character:keyIntent()
  local function key(a, b)
    return (input.keyDown(a) or input.keyDown(b)) and 1 or 0
  end
  local x = key("D", "Right") - key("A", "Left")
  local y = key("W", "Up") - key("S", "Down")
  local length = math.sqrt(x * x + y * y)
  if length > 1 then -- a diagonal is as fast as a straight line
    x, y = x / length, y / length
  end
  local forward = self:cameraForward()
  local right = vec3(-forward.z, 0, forward.x)
  return forward * y + right * x, input.keyDown("LeftShift") or input.keyDown("RightShift")
end

-- Whether the ground under a point is there and still: a ray straight down, and the body it hits
-- not having moved since the last step.
function Character:groundStill(point)
  local hit = physics.raycast(point + vec3(0, 1, 0), vec3(0, -1, 0), 1.6, self.entity)
  if hit == nil then
    self.riding.last = nil
    return false
  end
  local at = hit.entity:worldPosition()
  local last = self.riding.last
  self.riding.last = { entity = hit.entity, position = at }
  return last ~= nil and last.entity == hit.entity and (at - last.position):length() < 1e-4
end

-- The route as the same intent the keys give: towards the next waypoint, slowing into one that
-- stops, and a jump press on reaching one that says so.
function Character:routeIntent()
  if self.routeDone then
    return vec3(0, 0, 0), false
  end
  if self.waitUntil ~= nil then
    if self.clock < self.waitUntil then
      return vec3(0, 0, 0), false
    end
    self.waitUntil = nil
  end
  if self.riding ~= nil then
    -- Stand until the ground under the next waypoint has been away or moving, and is now there and
    -- still: a platform that has just come to rest there.
    local still = self:groundStill(self.waypoints[self.waypoint].position)
    self.riding.seenAway = self.riding.seenAway or not still
    if not (still and self.riding.seenAway) then
      return vec3(0, 0, 0), false
    end
    self.riding = nil
  end
  local point = self.waypoints[self.waypoint]
  local to = horizontal(point.position - self.entity:worldPosition())
  local distance = to:length()
  if distance <= self.routeTolerance then
    if point.cues.jump then
      self.jumpPressedAt = self.clock
    end
    self.waypoint = self.waypoint + 1
    if self.waypoint > #self.waypoints then
      self.routeDone = true
      log.info(self.entity:name(), "finished its route")
      return vec3(0, 0, 0), false
    end
    if point.cues.wait ~= nil then
      self.waitUntil = self.clock + point.cues.wait
      return vec3(0, 0, 0), false
    end
    if point.cues.ride then
      self.riding = { seenAway = false }
      return vec3(0, 0, 0), false
    end
    self.turnFirst = point.cues.turn == true
    return self:routeIntent()
  end
  -- Slow into a waypoint where it stands, and into one where the route turns by more than 60
  -- degrees, so the turn is not run wide.
  local slows = point.cues.wait ~= nil or point.cues.ride or self.waypoint == #self.waypoints
  local after = self.waypoints[self.waypoint + 1]
  if not slows and after ~= nil then
    local leg = horizontal(after.position - point.position)
    slows = leg:length() > 1e-3 and leg:normalized():dot(to / distance) < 0.5
  end
  local magnitude = slows and clamp(distance / 1.5, 0.3, 1) or 1
  return to * (magnitude / distance), point.cues.walk == true
end

function Character:fixedUpdate(dt)
  self.clock = self.clock + dt
  local state = physics.character(self.entity)
  if state == nil then
    return
  end
  -- A jump of more than a few metres in one step is a teleport: a respawn or a write to the
  -- Transform. Start over from where it put the character.
  local position = self.entity:worldPosition()
  if (position - self.lastPosition):length() > 2.5 then
    self:restart()
  end
  self.lastPosition = position

  local grounded = state.grounded
  if grounded then
    self.lastGroundedAt = self.clock
  end
  local ground = horizontal(state.velocity - state.supportVelocity):length()

  -- Movement intent.
  local direction, walking
  if #self.waypoints > 0 then
    direction, walking = self:routeIntent()
  else
    direction, walking = self:keyIntent()
  end
  self:startTurn(direction, grounded)
  local desired = direction * (walking and self.walkSpeed or self.runSpeed)
  if self:turnHolds() then
    desired = vec3(0, 0, 0)
  end
  local speeding = desired:length() >= self.velocity:length() and desired:dot(self.velocity) >= 0
  local rate = speeding and self.acceleration or self.braking
  if not grounded then
    rate = rate * self.airControl
  end
  local change = desired - self.velocity
  local step = rate * dt
  if change:length() > step then
    change = change:normalized() * step
  end
  self.velocity = self.velocity + change
  -- On the ground the intent does not run far ahead of what the character achieves, so pressing
  -- into a wall does not store speed for when it ends.
  if grounded and self.velocity:length() > ground + 1.5 then
    self.velocity = self.velocity:normalized() * (ground + 1.5)
  end
  physics.setCharacterMove(self.entity, self.velocity)
  -- Moving off, or leaving the ground, hands a turn on the spot over to the usual facing.
  if self.turning ~= nil and (self.velocity:length() >= 0.2 or not grounded) then
    self.turning.handedOver = true
  end

  -- Jump: a buffered press, on the ground or within the coyote time after leaving it, once per
  -- time on the ground.
  if self.jumped then
    if not grounded then
      self.leftGround = true
    elseif self.leftGround or self.clock - self.jumpedAt > 0.3 then
      self.jumped, self.leftGround = false, false
    end
  end
  if self.jumpPressedAt ~= nil then
    if self.clock - self.jumpPressedAt > self.jumpBuffer + 1e-6 then
      self.jumpPressedAt = nil
    elseif not self.jumped and self.clock - self.lastGroundedAt <= self.coyoteTime + 1e-6 then
      physics.jumpCharacter(self.entity, self.jumpSpeed)
      self.jumped, self.jumpedAt, self.leftGround = true, self.clock, false
      self.jumpPressedAt = nil
      self.lastGroundedAt = -math.huge
      self:startJump()
    end
  end

  self:animate(dt, state, ground)
end

-- The root's yaw: what a teleport gave it. Only the Model turns as the character moves.
function Character:rootYaw()
  local forward = self.entity:get("Transform").rotation * vec3(0, 0, -1)
  return yawOf(forward)
end

-- Starts a turn on the spot when the character stands still on the ground and the input is more
-- than `turnThreshold` off the model's heading: a quarter or a half turn, to the side it is on.
function Character:startTurn(direction, grounded)
  local turn = self.turning
  if turn ~= nil then
    if not turn.handedOver and direction:length() > 0.1 then
      turn.target = yawOf(direction) -- where the model is to face once it is done
    end
    return
  end
  -- A route's "turn" cue lasts for the turn it starts, or none.
  local cue = self.turnFirst
  self.turnFirst = false
  if self.model == nil or direction:length() <= 0.1 or self.turnThreshold >= 180 then
    return
  end
  local landing = self.landWeight[LAND] + self.landWeight[HARD]
  if not grounded or self.jumped or self.air > 0 or landing > 0.05 or self.groundSpeed > 0.2
    or self.velocity:length() > 0.2 then
    return
  end
  local error = wrapAngle(yawOf(direction) - self.facing)
  if math.abs(error) <= math.rad(self.turnThreshold) then
    return
  end
  local clip = TURNS[(error > 0 and 1 or 2) + (math.abs(error) >= HALF_TURN and 2 or 0)]
  self.turning = {
    clip = clip.clip,
    angle = clip.angle,
    duration = clip.duration,
    from = self.facing,
    target = yawOf(direction),
    startedAt = self.clock,
    handedOver = false,
  }
  self.turnFirst = cue
  self.settle = nil
  self.restartLayer[TURN] = 0
end

-- Whether a turn on the spot holds the move back: until the model faces within TURN_RELEASE of
-- the input, for at most `turnHold` seconds, or, for a route's "turn" cue, until it has played.
function Character:turnHolds()
  local turn = self.turning
  if turn == nil or turn.handedOver then
    return false
  end
  if self.turnFirst then
    return true
  end
  local remaining = math.abs(wrapAngle(turn.target - self.facing))
  return remaining > TURN_RELEASE and self.clock - turn.startedAt < self.turnHold - 1e-6
end

-- Turns the model: during a turn on the spot, by the clip's angle times how much of it has played;
-- otherwise towards the intended velocity, smoothly and at most `facingRate`, or, standing after a
-- turn on the spot, by what that turn fell short of.
function Character:turn(dt)
  if self.model == nil then
    return
  end
  local turn = self.turning
  if turn ~= nil and not turn.handedOver then
    local layer = self.model:get("Animator").layers[TURN]
    local played = 0
    if layer ~= nil and layer.clip == turn.clip then
      played = clamp(layer.time / turn.duration, 0, 1)
    end
    self.facing = wrapAngle(turn.from + turn.angle * played)
    self:faceModel()
    return
  end
  local target
  if self.velocity:length() >= 0.2 then
    target = yawOf(self.velocity)
    self.settle = nil
  elseif self.settle ~= nil then
    target = self.settle
  else
    return
  end
  local delta = wrapAngle(target - self.facing)
  local step = delta * (1 - math.exp(-12 * dt))
  local limit = math.rad(self.facingRate) * dt
  self.facing = wrapAngle(self.facing + clamp(step, -limit, limit))
  if self.settle ~= nil and math.abs(wrapAngle(self.settle - self.facing)) < 1e-3 then
    self.facing, self.settle = self.settle, nil
  end
  self:faceModel()
end

function Character:faceModel()
  self.model:set("Transform", { rotation = quat.axisAngle(vec3(0, 1, 0), self.facing - self:rootYaw()) })
end

function Character:startJump()
  if self.turning ~= nil then
    self.turning.handedOver = true
  end
  self.restartLayer[JUMP] = TAKEOFF
  self.jumpWeight = 1
  self.landing = nil
end

-- The animation weights and the playback rate, from what the last step did.
function Character:animate(dt, state, ground)
  if self.model == nil then
    return
  end
  local grounded = state.grounded
  if grounded then
    self.airTime = 0
    self.groundSpeed = smooth(self.groundSpeed, ground, 12, dt)
  else
    self.airTime = self.airTime + dt
    self.lastAirSpeed = -state.velocity.y
  end
  -- Airborne for the animation: a jump, or off the ground for longer than a step down takes.
  local airborne = not grounded and (self.jumped or self.airTime > 0.15 or state.velocity.y < -2.5)

  -- Landing, on the step that comes down: the soft one scaled by how hard, the hard one above
  -- `hardLanding`, nothing for a hop.
  if self.wasAirborne and grounded then
    local impact = self.lastAirSpeed
    if impact > self.hardLanding then
      self.landing = { layer = HARD, peak = 1 }
    elseif impact > 2.5 then
      self.landing = { layer = LAND, peak = clamp((impact - 2.5) / 2.5, 0.4, 1) }
    end
    if self.landing ~= nil then
      self.restartLayer[self.landing.layer] = 0
    end
  end
  self.wasAirborne = airborne or (self.wasAirborne and not grounded)

  local animator = self.model:get("Animator")
  local layers = animator.layers

  -- Air: in over 0.12 s, out over 0.1 s as the landing takes over.
  self.air = approach(self.air, airborne and 1 or 0, dt / (airborne and 0.12 or 0.1))
  -- The jump clip gives way to the fall near its end or on the way down.
  local jump = layers[JUMP]
  if jump.time >= DURATION.jumpStart - 0.12 or state.velocity.y < 0 or not airborne then
    self.jumpWeight = approach(self.jumpWeight, 0, dt / 0.2)
  end
  -- A landing fades over the second half of its clip, faster when the character moves on, and at
  -- once for a jump.
  for _, layer in ipairs({ LAND, HARD }) do
    local target = 0
    if self.landing ~= nil and self.landing.layer == layer and not self.jumped then
      local t = (self.restartLayer[layer] ~= nil) and 0 or layers[layer].time
      local duration = layer == LAND and DURATION.land or DURATION.hardLand
      target = self.landing.peak * (1 - smoothstep(0.45 * duration, duration, t))
      target = target * (1 - 0.5 * smoothstep(1, 4, self.groundSpeed))
    end
    local rate = (target > self.landWeight[layer]) and dt / 0.06 or dt / 0.12
    if self.jumped then
      rate = dt / 0.08
    end
    self.landWeight[layer] = approach(self.landWeight[layer], target, rate)
  end

  -- Locomotion: idle into walking up to MOVING, walking into running between the clips' own speeds.
  local speed = self.groundSpeed
  local walkNatural = self.walkStride / DURATION.walk
  local runNatural = self.runStride / DURATION.run
  local moving = smoothstep(0.05, MOVING, speed)
  self.gait = clamp((speed - walkNatural) / (runNatural - walkNatural), 0, 1)
  local stride = self.walkStride + (self.runStride - self.walkStride) * self.gait
  local natural = (1 / DURATION.walk) + (1 / DURATION.run - 1 / DURATION.walk) * self.gait
  -- Cycles per second that keep a planted foot still, within the band the clips survive.
  local cycles = clamp(speed / stride, 0.7 * natural, 1.4 * natural)
  if grounded then
    self.playback = cycles * DURATION.walk
  end

  -- A turn on the spot: in over 0.15 s, out into the idle over the last 0.3 s of its clip, or over
  -- 0.15 s once handed over to the walk or run. It is done when its weight is gone.
  local turn = self.turning
  if turn ~= nil then
    local played = (self.restartLayer[TURN] ~= nil) and 0 or layers[TURN].time
    if turn.handedOver then
      self.turnWeight = approach(self.turnWeight, 0, dt / 0.15)
    else
      local tail = 1 - smoothstep(turn.duration - 0.3, turn.duration, played)
      self.turnWeight = math.min(approach(self.turnWeight, 1, dt / 0.15), tail)
    end
    if not turn.handedOver and played >= turn.duration - 1e-4 then
      self.facing = wrapAngle(turn.from + turn.angle)
      if math.abs(wrapAngle(turn.target - self.facing)) > 1e-3 then
        self.settle = turn.target
      end
      self.turning, self.turnWeight, self.turnFirst = nil, 0, false
    elseif turn.handedOver and self.turnWeight <= 0 then
      self.turning, self.turnFirst = nil, false
    end
  end

  local groundShare = 1 - self.air
  local landing = math.min(self.landWeight[LAND] + self.landWeight[HARD], 1)
  local locomotion = groundShare * (1 - landing)
  layers[IDLE].weight = locomotion * (1 - moving) * (1 - self.turnWeight)
  layers[TURN].weight = locomotion * self.turnWeight
  if self.turning ~= nil then
    layers[TURN].clip = self.turning.clip
  end
  layers[RUN].weight = locomotion * moving * self.gait
  layers[JUMP].weight = self.air * self.jumpWeight
  layers[FALL].weight = self.air * (1 - self.jumpWeight)
  layers[LAND].weight = groundShare * self.landWeight[LAND]
  layers[HARD].weight = groundShare * self.landWeight[HARD]
  for index, time in pairs(self.restartLayer) do
    layers[index].time = time
    layers[index].playing = true
  end
  self.restartLayer = {}
  self.model:set("Animator", {
    weight = locomotion * moving * (1 - self.gait),
    speed = self.playback,
    layers = layers,
  })
end

return Character
