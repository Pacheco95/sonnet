-- A UI button that ends the game when it is clicked. Where the platform gives a game no way to
-- end itself (iOS), the button is hidden instead, since a click could do nothing.
local QuitButton = {}

function QuitButton:start()
  if not game.canQuit() then
    self.entity:add("Disabled")
  end
end

function QuitButton:onClick()
  game.quit()
end

return QuitButton
