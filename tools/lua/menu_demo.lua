-- Menu demo for findings R37: turns the open Options menu into an n-item list using only its own
-- art. It hides the real rows and their descriptions, clones the NewGame row COUNT times as
-- CoopItem1..N, labels them "Item 1".."Item N" (the --settext lines below, from C++) and puts the
-- selection bar on item SELECTED. Visual only: moving the cursor brings the game's own bar back.
-- Run it on a freshly opened menu (clones pile up when it runs twice on one menu).
-- Needs LuaProbe=1 and Log=1. Open the Options menu, then copy this file in as luaprobe.lua.

local COUNT = 8
local SELECTED = 3
local SPACING = 27  -- the game spaces its six rows 38 apart; eight fit in the same span at 27

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

local function set(element, name, value)
	if element then
		pcall(setAttribute, element, name, value)
	end
end

-- Opacity alone is not enough: the game re-applies it to some rows every frame (the greyed
-- Marketplace row and the Steam logo), so the originals are also moved far off screen.
local function hideChildren(element)
	local ok, children = pcall(getChildren, element)
	if ok and type(children) == 'table' then
		for _, child in ipairs(children) do
			set(child, 'opacity', 0)
			set(child, 'position.x', -10000)
		end
	end
end

-- The two layouts: single player (Options) and co-op (Multiplayer); only one is visible.
local layouts = {
	{ path = 'Scene.Layer.Options.Sections.Options', list = 'SelectionTitle' },
	{ path = 'Scene.Layer.Options.Sections.Multiplayer', list = 'SelectionText' },
}

for _, layout in ipairs(layouts) do
	local root = find(layout.path)
	local opacity = root and attr(root, 'opacity')
	if root and type(opacity) == 'number' and opacity > 0 then
		local list = find(layout.path .. '.' .. layout.list)
		local template = find(layout.path .. '.' .. layout.list .. '.NewGame')
		local bar = find(layout.path .. '.Background.Selection')
		if not (list and template and bar) then
			output('demo: ' .. layout.path .. ' is missing its list, NewGame row or selection bar')
		else
			hideChildren(list)
			hideChildren(find(layout.path .. '.SelectionDesription'))
			local top = 65
			local selectedY = top
			for i = 1, COUNT do
				local okClone, row = pcall(clone, template, 'CoopItem' .. i)
				if okClone and row then
					local y = top - SPACING * (i - 1)
					set(row, 'position.y', y)
					set(row, 'opacity', 100)
					if i == SELECTED then
						selectedY = y
					end
				else
					output('demo: clone ' .. i .. ' failed: ' .. tostring(row))
				end
			end
			-- The bar's centre sits at its background child's offset, so place it relative to that.
			local barOffset = attr(find(layout.path .. '.Background.Selection.SelectionBar_Background'), 'position.y') or 0
			set(bar, 'position.y', attr(list, 'position.y') + selectedY - barOffset)
			set(find(layout.path .. '.Background.Selection.SelectionBar_ArrowTop'), 'opacity', SELECTED > 1 and 100 or 0)
			set(find(layout.path .. '.Background.Selection.SelectionBar_ArrowBottom'), 'opacity', SELECTED < COUNT and 100 or 0)
			output('demo: ' .. COUNT .. ' items in ' .. layout.path .. ', bar on item ' .. SELECTED)
		end
	end
end

--settext Options.Sections.Options.SelectionTitle.CoopItem1|Item 1
--settext Options.Sections.Options.SelectionTitle.CoopItem2|Item 2
--settext Options.Sections.Options.SelectionTitle.CoopItem3|Item 3
--settext Options.Sections.Options.SelectionTitle.CoopItem4|Item 4
--settext Options.Sections.Options.SelectionTitle.CoopItem5|Item 5
--settext Options.Sections.Options.SelectionTitle.CoopItem6|Item 6
--settext Options.Sections.Options.SelectionTitle.CoopItem7|Item 7
--settext Options.Sections.Options.SelectionTitle.CoopItem8|Item 8
--settext Options.Sections.Multiplayer.SelectionText.CoopItem1|Item 1
--settext Options.Sections.Multiplayer.SelectionText.CoopItem2|Item 2
--settext Options.Sections.Multiplayer.SelectionText.CoopItem3|Item 3
--settext Options.Sections.Multiplayer.SelectionText.CoopItem4|Item 4
--settext Options.Sections.Multiplayer.SelectionText.CoopItem5|Item 5
--settext Options.Sections.Multiplayer.SelectionText.CoopItem6|Item 6
--settext Options.Sections.Multiplayer.SelectionText.CoopItem7|Item 7
--settext Options.Sections.Multiplayer.SelectionText.CoopItem8|Item 8
