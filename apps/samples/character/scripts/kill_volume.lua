-- A trigger below the course: a body that falls into it is put back `lift` metres above the `respawn`
-- marker, at rest and upright. Writing a dynamic body's Transform teleports it (docs/physics.md).
local KillVolume = {
  properties = {
    respawn = { type = "entity" },
    lift = { type = "number", default = 1, min = 0, max = 10 }, -- metres above the marker
  },
}

function KillVolume:onTriggerEnter(other)
  if self.respawn == nil or not other:isValid() or not other:has("RigidBody") then
    return
  end
  other:set("Transform", { position = self.respawn:worldPosition() + vec3(0, self.lift, 0), rotation = quat.identity() })
  physics.setLinearVelocity(other, vec3(0, 0, 0))
  physics.setAngularVelocity(other, vec3(0, 0, 0))
  log.info(other:name(), "fell and was respawned")
end

return KillVolume
