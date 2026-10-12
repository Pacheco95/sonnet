-- A UI button that shows or hides another entity each time it is clicked: a menu panel that opens
-- and closes, a popup that goes away. Hiding is the Disabled tag, which takes the whole subtree out
-- of the layout and out of the input.
local ToggleButton = {
  properties = {
    target = { type = "entity" },
  },
}

function ToggleButton:onClick()
  local target = self.target
  if target == nil or not target:isValid() then
    return
  end
  if target:has("Disabled") then
    target:remove("Disabled")
  else
    target:add("Disabled")
  end
end

return ToggleButton
