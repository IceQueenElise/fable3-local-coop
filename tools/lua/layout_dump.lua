-- Layout dump for findings R37: the real positions and spacing of the Options rows, sent to
-- fable3coop.log through output() (the probe routes Gameface's log there). Then one clone of the
-- last row is placed a row below it, using the spacing measured here. The probe survives the
-- settext below when the copied row is a group rather than a text label (it logs that instead).
-- Needs LuaProbe=1 and Log=1. Open the Options menu, then copy this file in as luaprobe.lua.

local function find(path)
	local ok, element = pcall(getElement, path)
	if ok then
		return element
	end
	return nil
end

local function attr(element, name)
	local ok, value = pcall(getAttribute, element, name)
	if ok then
		return value
	end
	return nil
end

local function describe(element)
	local okType, kind = pcall(getElementType, element)
	return string.format('%s type=%s pos=(%s, %s, %s) opacity=%s scale.y=%s',
		tostring(attr(element, 'name')), tostring(okType and kind),
		tostring(attr(element, 'position.x')), tostring(attr(element, 'position.y')),
		tostring(attr(element, 'position.z')), tostring(attr(element, 'opacity')),
		tostring(attr(element, 'scale.y')))
end

-- Children of a row, so the text element inside a row group shows up.
local function tree(element, indent, depth)
	if depth == 0 then
		return
	end
	local ok, children = pcall(getChildren, element)
	if ok and type(children) == 'table' then
		for _, child in ipairs(children) do
			output('dump: ' .. indent .. describe(child))
			tree(child, indent .. '  ', depth - 1)
		end
	end
end

local function dump(path)
	local container = find('Scene.Layer.' .. path)
	if not container then
		output('dump: ' .. path .. ' not found')
		return
	end
	output('dump: ' .. path .. ' = ' .. describe(container))
	local ok, children = pcall(getChildren, container)
	if not ok or type(children) ~= 'table' then
		output('dump: getChildren failed: ' .. tostring(children))
		return
	end
	local last, prev
	for i, child in ipairs(children) do
		output(string.format('dump:   [%d] %s', i, describe(child)))
		tree(child, '      ', 3)
		prev, last = last, child
	end
	if not (last and prev) then
		return
	end
	local y, prevY = attr(last, 'position.y'), attr(prev, 'position.y')
	if type(y) ~= 'number' or type(prevY) ~= 'number' then
		return
	end
	local okClone, copy = pcall(clone, last, 'CoopRow1')
	if okClone and copy then
		pcall(setAttribute, copy, 'position.y', y + (y - prevY))
		output('dump: CoopRow1 = ' .. describe(copy))
	else
		output('dump: clone failed: ' .. tostring(copy))
	end
end

dump('Options.Sections.Multiplayer.SelectionText')
dump('Options.Sections.Options.SelectionTitle')

--settext Options.Sections.Multiplayer.SelectionText.CoopRow1|Coop Row 1
--settext Options.Sections.Options.SelectionTitle.CoopRow1|Coop Row 1
