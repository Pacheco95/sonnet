-- A UI button that loads another of the project's scenes when it is clicked: a title screen's
-- Play, a pause screen's Main menu. The scene replaces the running one after the frame.
local SceneButton = {
  properties = {
    -- Relative to the project, as --scene takes it.
    scene = { type = "string", default = "" },
  },
}

function SceneButton:onClick()
  if self.scene ~= "" then
    game.loadScene(self.scene)
  end
end

return SceneButton
