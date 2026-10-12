-- A UI button that pauses the game and shows its pause menu, or resumes and hides it: the HUD's
-- Pause and the menu's Resume are both this script, pointed at the same menu. A time scale of 0
-- stops physics, animation and the scripts' dt, while buttons still answer.
local PauseButton = {
  properties = {
    menu = { type = "entity" },
  },
}

function PauseButton:onClick()
  local menu = self.menu
  if menu == nil or not menu:isValid() then
    return
  end
  if game.timeScale() == 0 then
    game.setTimeScale(1)
    menu:add("Disabled")
  else
    game.setTimeScale(0)
    menu:remove("Disabled")
  end
end

return PauseButton
