-- M15's HUD budget as a scene: two hundred elements on the canvas this script runs on, a third of
-- them labels, in a grid of twenty by ten tiles that fills the reference size. Of the rest, half are
-- nine-sliced panels and half plain rectangles. The labels count frames, so their text is laid out
-- again every frame, as a HUD's score and timers are. Built here so the scene file stays small.
-- Play it with `--scene scenes/hud_stress.scene.json --play 3` (docs/player.md#capture-runs) and
-- read the overlay pass's GPU time.
local FONT = "6f1c2a40-5b0e-4c1d-9a3e-2b7d9d51f001"
local PANEL = "dfb644d8-00f0-4e56-9435-3e612b585e93"
local COLUMNS = 20
local ROWS = 10
local TILE = { x = 1920 / COLUMNS, y = 1080 / ROWS }
local GAP = 4

local HudStress = {}

function HudStress:start()
  self.labels = {}
  self.frame = 0
  for row = 0, ROWS - 1 do
    for column = 0, COLUMNS - 1 do
      local index = row * COLUMNS + column
      local tile = world.create("Tile", self.entity)
      tile:set("GuiRect", {
        anchorMin = { x = 0, y = 0 },
        anchorMax = { x = 0, y = 0 },
        offsetMin = { x = column * TILE.x + GAP, y = row * TILE.y + GAP },
        offsetMax = { x = (column + 1) * TILE.x - GAP, y = (row + 1) * TILE.y - GAP },
      })
      if index % 3 == 0 then
        tile:set("GuiText", { font = FONT, text = "0", size = 32, horizontal = "Center", vertical = "Center" })
        table.insert(self.labels, tile)
      elseif index % 3 == 1 then
        tile:set("GuiImage", { texture = PANEL, tint = { x = 0.55, y = 0.6, z = 0.8, w = 0.9 },
                               slice = { x = 16, y = 16, z = 16, w = 16 } })
      else
        tile:set("GuiImage", { tint = { x = 0.9, y = 0.5, z = 0.2, w = 0.6 } })
      end
    end
  end
end

function HudStress:update()
  self.frame = self.frame + 1
  for i, label in ipairs(self.labels) do
    label:set("GuiText", { text = tostring((self.frame * 7 + i * 131) % 100000) })
  end
end

return HudStress
