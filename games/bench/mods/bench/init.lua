-- Rendering benchmark scenes for Luanti
-- Controlled through settings in minetest.conf:
--   bench_scene    : none | node_plain | node_nodebox | node_mesh
--                    | ent_cube | ent_mesh | ent_mesh_hi | ent_sprite | ent_wield
--   bench_count    : number of entities (entity scenes)
--   bench_grid     : grid size for node scenes (grid*grid nodes)
--   bench_dist     : distance of the entity wall from the player
--   bench_spacing  : spacing between entities
--   bench_pitch    : camera pitch in radians (positive = looking down)

local S = core.settings
local scene = S:get("bench_scene") or "none"
local count = tonumber(S:get("bench_count") or "1000") or 1000
local grid = tonumber(S:get("bench_grid") or "16") or 16
local dist = tonumber(S:get("bench_dist") or "20") or 20
local spacing = tonumber(S:get("bench_spacing") or "1.05") or 1.05
local pitch = tonumber(S:get("bench_pitch") or "0") or 0
-- nodes re-placed per 0.5 s tick, forcing the client to regenerate mesh
local churn = tonumber(S:get("bench_churn") or "0") or 0

local PLAYER_POS = {x = 0, y = 0, z = 0}
-- eye height is used by the client for the camera; keep the scene anchored to it
local EYE = 1.5

core.log("action", "[bench] scene=" .. scene .. " count=" .. count ..
	" grid=" .. grid .. " dist=" .. dist)

--------------------------------------------------------------------------
-- Nodes
--------------------------------------------------------------------------

core.register_node("bench:plain", {
	description = "Bench plain node",
	drawtype = "normal",
	tiles = {"bench_node.png"},
	groups = {cracky = 3},
})

-- A "mesh node": nodebox composed out of many boxes, like fences/plants but heavier
local boxes = {}
for i = 0, 3 do
	for j = 0, 3 do
		for k = 0, 1 do
			table.insert(boxes, {
				-0.5 + i * 0.25, -0.5 + k * 0.5, -0.5 + j * 0.25,
				-0.5 + i * 0.25 + 0.18, -0.5 + k * 0.5 + 0.4, -0.5 + j * 0.25 + 0.18,
			})
		end
	end
end

core.register_node("bench:nodebox", {
	description = "Bench nodebox node",
	drawtype = "nodebox",
	node_box = {type = "fixed", fixed = boxes},
	tiles = {"bench_node2.png"},
	groups = {cracky = 3},
})

core.register_node("bench:mesh", {
	description = "Bench mesh node",
	drawtype = "mesh",
	mesh = "bench_low.obj",
	tiles = {"bench_model.png"},
	groups = {cracky = 3},
})

-- Same model, but with N distinct tile materials, so that a block has to deal
-- with many different materials at once (dense areas of unique models).
local node_materials = tonumber(S:get("bench_materials") or "1") or 1
local node_model = S:get("bench_node_model") or "bench_low.obj"
local node_layers = tonumber(S:get("bench_node_layers") or "1") or 1
local tints = {"", "^[multiply:#ff0000", "^[multiply:#00ff00", "^[multiply:#0000ff",
	"^[multiply:#ffff00", "^[multiply:#00ffff", "^[multiply:#ff00ff",
	"^[multiply:#808080"}
for i = 0, node_materials - 1 do
	core.register_node("bench:mesh_m" .. i, {
		description = "Bench mesh node " .. i,
		drawtype = "mesh",
		mesh = node_model,
		tiles = {"bench_model.png" .. (tints[(i % #tints) + 1] or "")},
		groups = {cracky = 3},
	})
end

core.register_node("bench:floor", {
	description = "Bench floor",
	drawtype = "normal",
	tiles = {"bench_node.png"},
	groups = {cracky = 3},
})

--------------------------------------------------------------------------
-- Entities
--------------------------------------------------------------------------

local function ent_def(visual, extra)
	local props = {
		visual = visual,
		visual_size = {x = 1, y = 1, z = 1},
		physical = false,
		collisionbox = {0, 0, 0, 0, 0, 0},
		pointable = false,
		static_save = false,
		makes_footstep_sound = false,
		automatic_rotate = 0,
	}
	for k, v in pairs(extra or {}) do props[k] = v end
	return {
		initial_properties = props,
		-- no on_step / on_activate: keep server-side cost minimal
	}
end

core.register_entity("bench:cube", ent_def("cube", {
	textures = {"bench_node.png", "bench_node.png", "bench_node.png",
			"bench_node.png", "bench_node.png", "bench_node.png"},
}))
core.register_entity("bench:mesh", ent_def("mesh", {
	mesh = "bench_mid.obj", textures = {"bench_model.png"},
}))
core.register_entity("bench:mesh_low", ent_def("mesh", {
	mesh = "bench_low.obj", textures = {"bench_model.png"},
}))
core.register_entity("bench:mesh_hi", ent_def("mesh", {
	mesh = "bench_hi.obj", textures = {"bench_model.png"},
}))
core.register_entity("bench:mesh_wield", ent_def("mesh", {
	mesh = "bench_mid.obj", textures = {"bench_model.png"}, wield_item = "bench:mesh",
}))
core.register_entity("bench:sprite", ent_def("upright_sprite", {
	textures = {"bench_node2.png"}, visual_size = {x = 1, y = 1},
}))

--------------------------------------------------------------------------
-- Scene building
--------------------------------------------------------------------------

local built = false
local player_ref = nil

local function build_node_scene(name)
	local n = name == "node_plain" and "bench:plain"
		or name == "node_nodebox" and "bench:nodebox"
		or "bench:mesh"
	local half = math.floor(grid / 2)
	local y = PLAYER_POS.y - 6
	-- node_mesh_* scenes place models with a configurable mesh and material count
	local mats = name:match("^node_mesh_(%d+)$")
	if mats then
		n = "bench:mesh_m"
		core.log("action", "[bench] node scene: " .. node_model ..
			", " .. node_materials .. " materials, " .. node_layers .. " layers")
	end
	local t0 = core.get_us_time()
	local placed = 0
	for layer = 0, node_layers - 1 do
		for i = -half, half - 1 do
			for j = -half, half - 1 do
				local name2 = n
				if mats then
					name2 = n .. ((i + j + layer) % node_materials)
				end
				core.set_node({x = i, y = y + layer, z = j}, {name = name2})
				placed = placed + 1
			end
		end
	end
	core.log("action", "[bench] built node scene at y=" .. y .. " with " .. placed ..
		" nodes in " .. string.format("%.1f", (core.get_us_time() - t0) / 1000) .. " ms")
end

local function build_entity_scene(name)
	local e = name == "ent_cube" and "bench:cube"
		or name == "ent_mesh" and "bench:mesh"
		or name == "ent_mesh_low" and "bench:mesh_low"
		or name == "ent_mesh_hi" and "bench:mesh_hi"
		or name == "ent_sprite" and "bench:sprite"
		or name == "ent_wield" and "bench:mesh_wield"
		or "bench:mesh"
	-- Slab of entities in front of the camera, compact enough that all of them
	-- stay inside the loaded/active region of the server.
	-- Layout: cols (X) x rows (Y) x depth (Z)
	local cols = math.max(1, math.ceil(math.sqrt(count * 0.8)))
	local depth = 2
	local rows = math.ceil(count / (cols * depth))
	local x0 = -(cols - 1) * spacing / 2
	local y0 = EYE - (rows - 1) * spacing / 2
	local z0 = PLAYER_POS.z + dist
	local made = 0
	for i = 0, count - 1 do
		local cx = i % cols
		local cy = math.floor(i / cols) % rows
		local cz = math.floor(i / (cols * rows))
		local pos = {
			x = PLAYER_POS.x + x0 + cx * spacing,
			y = PLAYER_POS.y + y0 + cy * spacing,
			z = z0 + cz * spacing,
		}
		if core.add_entity(pos, e) then made = made + 1 end
	end
	-- a floor to avoid an empty background
	local half = math.max(4, cols)
	for i = -half, half do
		for j = 0, 24 do
			core.set_node({x = i, y = PLAYER_POS.y - 8, z = PLAYER_POS.z + j}, {name = "bench:floor"})
		end
	end
	core.log("action", "[bench] built entity scene " .. e .. " x" .. made ..
		" (" .. cols .. "x" .. rows .. "x" .. depth .. " at z=" .. z0 .. ")")
end

local function build_scene()
	if scene == "none" then return end
	if scene:sub(1, 5) == "node_" then
		build_node_scene(scene)
	else
		build_entity_scene(scene)
	end
end

core.register_on_joinplayer(function(player)
	if S:get("bench_freeze") == "true" then
		player:set_physics_override({speed = 0, jump = 0, gravity = 0, sneak = false})
	end
	player:set_pos(PLAYER_POS)
	player:set_look_horizontal(0)
	player:set_look_vertical(pitch)
	core.set_timeofday(0.5)
	player_ref = player
	if not built then
		built = true
		local t0 = core.get_us_time()
		build_scene()
		core.log("action", "[bench] scene built in " ..
			string.format("%.1f", (core.get_us_time() - t0) / 1000) .. " ms")
	end
end)

local counter = 0.0
local churn_n = 0
core.register_globalstep(function(dtime)
	if not player_ref then return end
	counter = counter + dtime
	if counter < 0.5 then return end
	counter = 0
	player_ref:set_pos(PLAYER_POS)
	player_ref:set_look_horizontal(0)
	player_ref:set_look_vertical(pitch)

	-- Keep some blocks dirty so that mesh generation stays on the critical path.
	if churn > 0 then
		local half = math.floor(grid / 2)
		for _ = 1, churn do
			churn_n = churn_n + 1
			local x = (churn_n * 7) % grid - half
			local z = (churn_n * 11) % grid - half
			local y = PLAYER_POS.y - 6 + (churn_n % node_layers)
			core.set_node({x = x, y = y, z = z},
				{name = "bench:mesh_m" .. (churn_n % node_materials)})
		end
	end
end)
