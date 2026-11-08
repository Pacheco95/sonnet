-- A trigger below the course: what falls into it is put back at the `respawn` marker. A body comes
-- back `lift` metres above it, at rest and upright: writing a dynamic body's Transform teleports
-- it (docs/physics.md). A character is teleported with its feet on the marker, facing the way the
-- marker faces, which stops it and forgets its move and jump; its script starts over from there.
local KillVolume = {
  properties = {
    respawn = { type = "entity" },
    lift = { type = "number", default = 1, min = 0, max = 10 }, -- metres above the marker, for bodies
  },
}

function KillVolume:onTriggerEnter(other)
  if self.respawn == nil or not other:isValid() then
    return
  end
  local marker = self.respawn:worldPosition()
  if other:has("CharacterController") then
    local forward = self.respawn:get("Transform").rotation * vec3(0, 0, -1)
    if not physics.teleportCharacter(other, marker, math.atan(-forward.x, -forward.z)) then
      return
    end
  elseif other:has("RigidBody") then
    other:set("Transform", { position = marker + vec3(0, self.lift, 0), rotation = quat.identity() })
    physics.setLinearVelocity(other, vec3(0, 0, 0))
    physics.setAngularVelocity(other, vec3(0, 0, 0))
  else
    return
  end
  log.info(other:name(), "fell and was respawned")
end

return KillVolume
