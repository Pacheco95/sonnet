-- Click or touch either shape in the Game view or player to tint it green.
local Pick = {}
function Pick:update(dt)
  if not input.mousePressed("Left") then return end
  local ray = camera.ray(input.mousePosition())
  local hit = physics.raycast(ray.origin, ray.direction, 100)
  if hit and hit.entity:has("MeshRenderer") then
    hit.entity:set("MeshRenderer", {color = {x=0.2, y=1, z=0.3, w=1}})
    log.info("Picked " .. hit.entity:name())
  end
end
return Pick
