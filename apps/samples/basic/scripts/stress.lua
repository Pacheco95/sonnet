-- The README's performance target as a scene: a hundred by a hundred boxes and spheres and a
-- hundred point lights, the grid and the lights of `renderer_tests "[benchmark]"`
-- (docs/rendering.md#gpu-driven-submission), built here so the scene file stays small.
-- Play it with `--scene scenes/stress.scene.json --play 3` (docs/player.md#capture-runs).
local BOX = "a464f023-844b-8938-9df4-75487155e36d"
local SPHERE = "fe3d3b79-c4ee-8f48-8944-6b1596b2c621"
local SIDE = 100
local LIGHTS = 100

local Stress = {}

function Stress:start()
  local root = self.entity
  for z = 0, SIDE - 1 do
    for x = 0, SIDE - 1 do
      local cell = world.create("Cell", root)
      cell:set("Transform", { position = vec3((x - SIDE / 2) * 1.5, 0.5, (z - SIDE / 2) * 1.5) })
      cell:set("MeshRenderer", { mesh = (x + z) % 2 == 0 and BOX or SPHERE, visible = true })
    end
  end
  for i = 0, LIGHTS - 1 do
    local angle = i * 0.37
    local radius = 5 + i * 0.5
    local lamp = world.create("Lamp", root)
    lamp:set("Transform", { position = vec3(math.cos(angle) * radius, 1.5, math.sin(angle) * radius) })
    lamp:set("PointLight", { color = vec3(1.0, 0.8, 0.6), intensity = 8, range = 6 })
  end
end

return Stress
