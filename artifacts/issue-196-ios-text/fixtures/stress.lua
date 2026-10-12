-- The README's performance target as a scene: a hundred by a hundred boxes and spheres and a
-- hundred point lights, the grid and the lights of `renderer_tests "[benchmark]"`
-- (docs/rendering.md#gpu-driven-submission), built here so the scene file stays small. The
-- spheres are icospheres, 320 triangles, the nearest built-in to the benchmark's 224; the
-- built-in sphere has 960 and made the shadow cascades four times as heavy on a phone.
-- Play it with `--scene scenes/stress.scene.json --play 3` (docs/player.md#capture-runs).
local BOX = "a464f023-844b-8938-9df4-75487155e36d"
local SPHERE = "edd73868-8c8d-837e-9c97-f8de1a22a52a" -- builtin/icosphere
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

function Stress:buildHud()
  local canvas = world.create("HudCanvas")
  canvas:set("GuiCanvas", { match = 0.5 })
  self.labels = {}
  for i = 0, 199 do
    local col, row = i % 10, i // 10
    local e = world.create("Hud" .. i, canvas)
    e:set("GuiRect", { anchorMin = { x = 0, y = 0 }, anchorMax = { x = 0, y = 0 },
      offsetMin = { x = 60 + col * 184, y = 60 + row * 74 },
      offsetMax = { x = 240 + col * 184, y = 130 + row * 74 } })
    if i % 3 == 0 then
      e:set("GuiText", { font = "6f1c2a40-5b0e-4c1d-9a3e-2b7d9d51f001", text = string.format("%d:%02d", i, i % 60), size = 44,
        horizontal = "Center", vertical = "Center" })
      self.labels[#self.labels + 1] = e
    else
      e:set("GuiImage", { tint = { x = 0.2, y = 0.3 + (i % 7) / 20, z = 0.5, w = 0.6 } })
    end
  end
end
function Stress:update(dt)
  self.frame = (self.frame or 0) + 1
  if self.frame == 1 then self:buildHud() end
  if self.frame == 250 then self.labels[1]:set("GuiText", { text = "Hello World" }) end
end
return Stress
