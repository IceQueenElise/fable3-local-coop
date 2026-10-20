-- Clone test for findings R37: can Gameface Lua add menu rows that C++ can then find and fill?
-- Needs LuaProbe=1 and Log=1 in fable3coop.ini. Open the Options menu (title screen or in
-- game), then copy this file into the game folder as luaprobe.lua. Results go to fable3coop.log.
--
-- For each Options layout ("Multiplayer" in game, "Options" on the title screen) it clones the
-- "New Game" label three ways and the whole label container once, moving each copy down:
--   CoopLabel<A|B>   clone(src, name): default parent
--   CoopParent<A|B>  clone(src, name, parent): explicit parent (the label's container)
--   CoopGroup<A|B>   clone(container, name): a group instead of a bare label
-- A = paths given from Scene.Layer, B = paths given from the layer (whichever getElement takes).
-- C++ then looks each copy up by path (under the container and at the root) and sets its text.
-- Expected: copies labelled "Label", "Parent" and a shifted copy of every label. Close and
-- reopen the menu: the copies should be gone (each open gets a fresh screen instance).

local function find(path)
	local ok, element = pcall(getElement, path)
	if ok then
		return element
	end
	return nil
end

local function shift(src, copy, dy)
	local ok, y = pcall(getAttribute, src, 'position.y')
	if ok and y then
		pcall(setAttribute, copy, 'position.y', y + dy)
	end
end

local function cloneAs(src, name, parent, dy)
	local ok, copy
	if parent then
		ok, copy = pcall(clone, src, name, parent)
	else
		ok, copy = pcall(clone, src, name)
	end
	if ok and copy then
		shift(src, copy, dy)
	end
end

local function test(section)
	local prefix, suffix = 'Scene.Layer.', 'A'
	local container = find(prefix .. section)
	if not container then
		prefix, suffix = '', 'B'
		container = find(section)
	end
	if not container then
		return
	end
	local label = find(prefix .. section .. '.NewGame')
	if label then
		cloneAs(label, 'CoopLabel' .. suffix, nil, 60)
		cloneAs(label, 'CoopParent' .. suffix, container, 120)
	end
	cloneAs(container, 'CoopGroup' .. suffix, nil, 180)
end

test('Options.Sections.Multiplayer.SelectionText')
test('Options.Sections.Options.SelectionTitle')

--check Options.Sections.Multiplayer.SelectionText.NewGame
--check Options.Sections.Multiplayer.SelectionText.CoopLabelA
--check Options.Sections.Multiplayer.SelectionText.CoopLabelB
--check CoopLabelA
--check CoopLabelB
--check Options.Sections.Multiplayer.SelectionText.CoopParentA
--check Options.Sections.Multiplayer.SelectionText.CoopParentB
--check Options.Sections.Multiplayer.CoopGroupA.NewGame
--check Options.Sections.Multiplayer.CoopGroupB.NewGame
--settext Options.Sections.Multiplayer.SelectionText.CoopLabelA|Label A
--settext Options.Sections.Multiplayer.SelectionText.CoopLabelB|Label B
--settext Options.Sections.Multiplayer.SelectionText.CoopParentA|Parent A
--settext Options.Sections.Multiplayer.SelectionText.CoopParentB|Parent B
--check Options.Sections.Options.SelectionTitle.NewGame
--check Options.Sections.Options.SelectionTitle.CoopLabelA
--check Options.Sections.Options.SelectionTitle.CoopLabelB
--check Options.Sections.Options.SelectionTitle.CoopParentA
--check Options.Sections.Options.SelectionTitle.CoopParentB
--check Options.Sections.Options.CoopGroupA.NewGame
--check Options.Sections.Options.CoopGroupB.NewGame
--settext Options.Sections.Options.SelectionTitle.CoopLabelA|Label A
--settext Options.Sections.Options.SelectionTitle.CoopLabelB|Label B
--settext Options.Sections.Options.SelectionTitle.CoopParentA|Parent A
--settext Options.Sections.Options.SelectionTitle.CoopParentB|Parent B
