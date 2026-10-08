-- The running score, shared by every script that requires it: the module loads once.
local Score = { total = 0 }

function Score.add(points)
  Score.total = Score.total + points
end

return Score
