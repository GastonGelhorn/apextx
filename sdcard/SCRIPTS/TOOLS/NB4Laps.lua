-- SPDX-License-Identifier: GPL-2.0-only
-- NB4 native lap analysis. API v1, lap times in centiseconds.
-- Optional presentation only: the firmware owns timing, switches and results.
local first, lastRead, laps = 1, -100, {}
local function time(cs)
  if not cs then return "--" end
  return string.format("%d:%02d.%02d", math.floor(cs / 6000), math.floor(cs / 100) % 60, cs % 100)
end
local function run(event)
  if event == EVT_VIRTUAL_EXIT then return 2 end
  if not getCarState or not getRaceLaps then
    lcd.clear()
    lcd.drawText(12, 16, "NB4 car firmware required")
    return 0
  end
  local state = getCarState()
  if state.version ~= 1 then
    lcd.clear()
    lcd.drawText(12, 16, "NB4 resource version mismatch")
    return 0
  end
  local es = state.language == "es"
  local now = getTime()
  if now - lastRead >= 25 then
    laps = getRaceLaps() or laps
    lastRead = now
  end
  local rows = math.max(1, math.floor((state.height - 130) / 30))
  if event == EVT_VIRTUAL_NEXT then first = math.min(math.max(1, #laps - rows + 1), first + 1) end
  if event == EVT_VIRTUAL_PREV then first = math.max(1, first - 1) end
  first = math.min(first, math.max(1, #laps - rows + 1))
  lcd.clear()
  lcd.drawText(12, 12, es and "ANÁLISIS DE VUELTAS" or "LAP ANALYSIS", BOLD)
  local sum, best = 0, nil
  for _, value in ipairs(laps) do sum = sum + value; best = math.min(best or value, value) end
  lcd.drawText(12, 42, (es and "Mejor " or "Best ") .. time(best))
  lcd.drawText(12, 66, (es and "Media " or "Average ") .. time(#laps > 0 and math.floor(sum / #laps) or nil))
  if #laps == 0 then
    lcd.drawText(12, 112, es and "Sin vueltas registradas" or "No laps recorded")
  else
    for i = first, math.min(#laps, first + rows - 1) do
      local delta = laps[i] - best
      lcd.drawText(12, 100 + (i - first) * 30,
        string.format("%02d   %s   +%.2f", i, time(laps[i]), delta / 100), laps[i] == best and BOLD or 0)
    end
  end
  lcd.drawText(12, state.height - 25, es and "RTN: volver   +/-: recorrer" or "RTN: back   +/-: scroll", SMLSIZE)
  return 0
end
return {name = "NB4 Laps", run = run}
