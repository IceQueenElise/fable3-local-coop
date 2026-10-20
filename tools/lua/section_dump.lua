-- Section dump for findings R37: every element under both Options layouts (and the screen root's
-- first levels), to find the selection highlight. Run it twice with the cursor on different
-- rows; whatever changes position or opacity between the runs is the highlight.
-- Needs LuaProbe=1 and Log=1. Open the Options menu, then copy this file in as luaprobe.lua.

local function attr(element, name)
	local ok, value = pcall(getAttribute, element, name)
	if ok then
		return value
	end
	return nil
end

local function describe(element)
	local okType, kind = pcall(getElementType, element)
	return string.format('%s type=%s pos=(%s, %s, %s) opacity=%s',
		tostring(attr(element, 'name')), tostring(okType and kind),
		tostring(attr(element, 'position.x')), tostring(attr(element, 'position.y')),
		tostring(attr(element, 'position.z')), tostring(attr(element, 'opacity')))
end

local function tree(element, indent, depth)
	output('section: ' .. indent .. describe(element))
	if depth == 0 then
		return
	end
	local ok, children = pcall(getChildren, element)
	if ok and type(children) == 'table' then
		for _, child in ipairs(children) do
			local okType, kind = pcall(getElementType, child)
			-- Materials and textures only repeat the look of their node.
			if not (okType and (kind == 'material' or kind == 'texture')) then
				tree(child, indent .. '  ', depth - 1)
			end
		end
	end
end

local function dump(path, depth)
	local ok, element = pcall(getElement, path)
	if ok and element then
		tree(element, '', depth)
	else
		output('section: ' .. path .. ' not found')
	end
end

local okSlide, slide = pcall(function() return getCurrentSlide(getElement('Scene.Layer.Options')) end)
output('section: current slide of Options = ' .. tostring(okSlide and slide))
dump('Scene.Layer', 2)
dump('Scene.Layer.Options.Sections.Options', 4)
dump('Scene.Layer.Options.Sections.Multiplayer', 4)
