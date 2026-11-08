-- The character's footsteps and landings (issue #496), on the humanoid's entity, which carries the
-- Animator and so hears its clips' `footstep` and `landing` events (#505). A step sounds only while
-- the controller stands on something, from the surface it stands on. Walk and Run are blended and
-- synchronised, so both cross the same footstep in the same frame: a foot sounds once per
-- `minInterval`. Each step draws another recording of its foot and surface than the last one and
-- varies its pitch and volume a little.
local Footsteps = {
  properties = {
    volume = { type = "number", default = 0.9, min = 0, max = 2 },
    pitchVariation = { type = "number", default = 0.05, min = 0, max = 0.5 }, -- either way, of 1
    volumeVariation = { type = "number", default = 0.15, min = 0, max = 1 }, -- down, of the volume
    minInterval = { type = "number", default = 0.3, min = 0, max = 1 }, -- s between steps of a foot
  },
}

-- assets/audio/footsteps/, by identity (see its sidecars).
local SOUNDS = {
  concrete = {
    left = {
      "863e27cb-1cd4-8454-b36f-e3c1d5ffe3dc", -- footstep_concrete_left_01
      "fd2414fc-ee40-81d1-8076-78e9ff85cad5", -- footstep_concrete_left_02
      "e048ac6d-5dc4-870a-9d36-971fd097d6f3", -- footstep_concrete_left_03
    },
    right = {
      "1719fab3-c7c0-816c-a05d-26aade2fcc4d", -- footstep_concrete_right_01
      "743ced15-6d26-877b-9349-f7d775140dfa", -- footstep_concrete_right_02
      "41e0ad11-c258-8483-8607-793af4477e8e", -- footstep_concrete_right_03
    },
    land = "7b50a721-affe-89d9-b63e-7060b1fb9693", -- land_concrete
  },
  metal = {
    left = {
      "c8946578-4e25-8b38-838c-77f56c630747", -- footstep_metal_left_01
      "7b3406ce-4711-8a10-9019-f1c12a476069", -- footstep_metal_left_02
    },
    right = {
      "41bae886-da6b-8d21-b059-20189cafd48d", -- footstep_metal_right_01
      "1afd23b8-fde7-8138-a3e4-19416e691e14", -- footstep_metal_right_02
    },
    land = "23c6ac84-115e-8975-8438-1c5803444b2a", -- land_metal
  },
  wood = {
    left = {
      "dcdf3964-3e62-891e-a9ab-0527fb868310", -- footstep_wood_left_01
      "df040ec9-a84c-8d21-82c1-0381a039e528", -- footstep_wood_left_02
    },
    right = {
      "bd3acc2a-917c-82dc-a62e-a615f1e9a167", -- footstep_wood_right_01
      "46329a2f-1518-8159-9dc7-9446c356cfe4", -- footstep_wood_right_02
    },
    land = "d5b6365f-e8a7-89b3-a05e-ff7f760bb988", -- land_wood
  },
}

-- The course's materials (assets/level/materials/) and what they sound like underfoot.
local SURFACES = {
  ["a0104d95-3146-8885-8d7e-92c447beb656"] = "concrete", -- concrete_floor
  ["d8cc0fa5-585d-8844-9fdf-c3ae244c4b6c"] = "concrete", -- concrete_wall
  ["d5015c73-0e65-87f2-a2fb-4324b5dde89e"] = "metal", -- painted_metal
  ["d21183d2-a844-8137-916e-033acc6c7f81"] = "wood", -- worn_planks
}

local RUN_SPEED = 4.8 -- m/s at which a step is at full volume

-- What an entity stood on sounds like: its material's, or its parent's for a collider of its own
-- (the ramps), and concrete for anything else.
local function surfaceOf(entity)
  while entity ~= nil and entity:isValid() do
    local renderer = entity:get("MeshRenderer")
    if renderer ~= nil then
      return SURFACES[renderer.material] or "concrete"
    end
    entity = entity:parent()
  end
  return "concrete"
end

function Footsteps:start()
  self.character = self.entity:parent()
  self.sources = { left = self.character:child("Left foot"), right = self.character:child("Right foot") }
  self.landingSource = self.character:child("Landing")
  self.clock = 0
  self.last = { left = -math.huge, right = -math.huge, landing = -math.huge }
  self.drawn = { left = 0, right = 0 }
  self.steps = 0
end

function Footsteps:update(dt)
  self.clock = self.clock + dt
end

-- A recording other than the last one of this foot.
function Footsteps:draw(list, foot)
  local index = math.random(#list)
  if #list > 1 and index == self.drawn[foot] then
    index = index % #list + 1
  end
  self.drawn[foot] = index
  return list[index]
end

function Footsteps:play(source, sound, volume)
  if source == nil then
    return
  end
  source:set("AudioSource", {
    sound = sound,
    volume = volume * (1 - math.random() * self.volumeVariation),
    pitch = 1 + (math.random() * 2 - 1) * self.pitchVariation,
    playing = true,
  })
end

function Footsteps:onAnimationEvent(name, argument)
  local state = physics.character(self.character)
  if state == nil or not state.grounded then
    return
  end
  local surface = SOUNDS[surfaceOf(state.support)]
  if name == "footstep" and (argument == "left" or argument == "right") then
    if self.clock - self.last[argument] < self.minInterval then
      return
    end
    self.last[argument] = self.clock
    local v = state.velocity - state.supportVelocity
    local speed = math.sqrt(v.x * v.x + v.z * v.z)
    local loudness = 0.55 + 0.45 * math.min(speed / RUN_SPEED, 1)
    self.steps = self.steps + 1
    log.debug("footstep", argument, self.steps)
    self:play(self.sources[argument], self:draw(surface[argument], argument), self.volume * loudness)
  elseif name == "landing" then
    if self.clock - self.last.landing < self.minInterval then
      return
    end
    self.last.landing = self.clock
    log.debug("landing", argument)
    self:play(self.landingSource, surface.land, self.volume * (argument == "hard" and 1 or 0.75))
  end
end

return Footsteps
