-- The ground under the character's feet, for leg IK (ADR-0042, issue #500). It sits on the humanoid,
-- which carries the Animator and the `LegIk` component: each frame, in `lateUpdate`, it casts one ray
-- per foot and writes what it found into the component's legs. The engine does the rest, in PreRender,
-- after the transform system and before the skin palettes: it drops the pelvis, reaches the legs and
-- turns the feet to the ground. This script is the game's half of it, so the ground is the game's
-- ground: what the rays ignore, and where they start, are decided here.
--
-- Each ray starts at the animated ankle's xz, `stepHeight` above the character's feet, and runs down
-- to `stepHeight` plus the component's `maxPelvisDrop` below them: ground higher than a step, or
-- lower than the pelvis can follow, is not hit, and a foot without ground is left as animated.
-- `worldPosition()` here is the animated, pre-IK pose (the pass rewrites world matrices only), which
-- is where the ray should start. The weight is 1 while the controller stands on walkable ground and
-- 0 in the air or on ground too steep to stand on; the engine eases it, so IK fades out in a jump
-- and back in on landing.
local Feet = {
  properties = {
    stepHeight = { type = "number", default = 0.3, min = 0, max = 1 }, -- m above the feet the rays start
    weight = { type = "number", default = 1, min = 0, max = 1 }, -- of leg IK while on the ground; 0 is off
  },
}

-- The humanoid's feet, as paths under the model (the legs' order in the LegIk).
local FEET = {
  "Armature/Root/pelvis/thigh_l/calf_l/foot_l",
  "Armature/Root/pelvis/thigh_r/calf_r/foot_r",
}

function Feet:start()
  self.character = self.entity:parent()
  self.feet = {}
  for i, path in ipairs(FEET) do
    self.feet[i] = self.entity:child(path)
  end
end

function Feet:lateUpdate(dt)
  local ik = self.entity:get("LegIk")
  if ik == nil or self.character == nil then
    return
  end
  local state = physics.character(self.character)
  ik.weight = (state ~= nil and state.grounded) and self.weight or 0
  -- The feet's plane: the model stands at the capsule's origin.
  local top = self.entity:worldPosition().y + self.stepHeight
  for i, foot in ipairs(self.feet) do
    local leg = ik.legs[i]
    if leg ~= nil and foot ~= nil then
      local ankle = foot:worldPosition()
      local hit = physics.raycast(vec3(ankle.x, top, ankle.z), vec3(0, -1, 0), self.stepHeight + ik.maxPelvisDrop, self.character)
      if hit ~= nil then
        leg.groundPoint, leg.groundNormal, leg.grounded = hit.point, hit.normal, true
      else
        leg.grounded = false
      end
    end
  end
  self.entity:set("LegIk", ik)
end

return Feet
