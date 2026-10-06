// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Arena agent EmberKite-4193

/*
	Benchmarks for mapblock mesh generation, aimed at the geometry heavy paths:
	nodes that place a model (drawtype "mesh"), high-poly meshes, and many
	materials in one block.

	This measures the CPU side of mesh generation, which is what makes a dense
	field of models slow to appear and to refresh when something changes near
	them. The GPU cost is roughly proportional to the vertex count either way.

	Run with:
		luanti --run-benchmarks [meshgen]
	Set BENCH_MESHGEN_OUT=<file> to append one line per case with a hash over the
	generated geometry, so that two builds can be compared byte for byte.
*/

#include "catch.h"
#include "dummygamedef.h"
#include "itemdef.h"
#include "inventory.h"
#include "nodedef.h"
#include "light.h"
#include "voxel.h"
#include "client/content_mapblock.h"
#include "client/mesh.h"
#include "client/mapblock_mesh.h"
#include "client/meshgen/collector.h"
#include "client/node_visuals.h"
#include <CMeshBuffer.h>
#include <SMesh.h>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace {

// Deterministic light for every node, so that the result does not depend on the
// decode table having been set up by something else.
void setLightDecodeTable()
{
	static u8 table[LIGHT_SUN + 1] = {
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
		0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
	};
	memcpy(const_cast<u8 *>(light_decode_table), table, sizeof(table));
}

/*
	A mesh with the shape of a moderately detailed model: a subdivided dome of
	side x side quads in a single buffer.
	side = 16 gives 289 vertices, side = 50 gives 2601, which is around what a
	high-poly content model has.
*/
scene::SMesh *makeDomeMesh(u32 side)
{
	auto *mesh = new scene::SMesh();
	auto *buf = new scene::SMeshBuffer();

	const u32 n = side + 1;
	std::vector<video::S3DVertex> vertices;
	vertices.reserve(n * n);
	for (u32 z = 0; z < n; z++) {
		for (u32 x = 0; x < n; x++) {
			f32 fx = (f32) x / side, fz = (f32) z / side;
			f32 h = std::sin(fx * M_PI) * std::sin(fz * M_PI) * 0.25f * BS;
			v3f pos((fx - 0.5f) * BS, h - 0.5f * BS, (fz - 0.5f) * BS);
			v3f normal(0.0f, 1.0f, 0.0f);
			vertices.emplace_back(pos, normal, video::SColor(255, 255, 255, 255),
					v2f(fx * 4.0f, fz * 4.0f));
		}
	}

	std::vector<u16> indices;
	indices.reserve(side * side * 6);
	for (u32 z = 0; z < side; z++) {
		for (u32 x = 0; x < side; x++) {
			u16 a = (u16) (z * n + x), b = (u16) (a + 1);
			u16 c = (u16) (a + n), d = (u16) (c + 1);
			indices.insert(indices.end(), {a, b, c, c, b, d});
		}
	}

	buf->append(vertices.data(), (u32) vertices.size(), indices.data(),
			(u32) indices.size());
	mesh->addMeshBuffer(buf);
	buf->drop();
	recalculateBoundingBox(mesh);
	return mesh;
}

struct BenchNode {
	BenchNode(const std::string &name, u32 texture)
	{
		f.name = name;
		f.visuals = std::make_unique<NodeVisuals>();
		f.param_type_2 = CPT2_COLOR;
		for (TileDef &tiledef : f.tiledef)
			tiledef.name = name + ".png";
		for (TileSpec &tile : f.visuals->tiles)
			tile.layers[0].texture_id = texture;
	}

	ContentFeatures f;
};

/**
 * Node definitions plus a filled-in map region to generate a mesh for.
 */
class MeshgenCase {
	class GDef : public DummyGameDef {
	public:
		NodeDefManager *ndef() { return getWritableNodeDefManager(); }
	};

public:
	explicit MeshgenCase(u32 side) :
			side(side),
			data(gdef.ndef(), side, MeshGrid{1})
	{
		setLightDecodeTable();
		data.m_generate_minimap = false;
		data.m_smooth_lighting = false;
		data.m_enable_water_reflections = false;
		data.m_blockpos = {0, 0, 0};

		// The generator reads a margin of 3 nodes around the area.
		s32 ext = (s32) side + 3;
		VoxelArea area(v3s16(-3, -3, -3), v3s16(ext, ext, ext));
		data.m_vmanip.clear();
		data.m_vmanip.addArea(area);
		for (u32 i = 0; i < data.m_vmanip.m_area.getVolume(); i++) {
			data.m_vmanip.m_data[i] = MapNode(CONTENT_AIR);
			data.m_vmanip.m_flags[i] &= ~VOXELFLAG_NO_DATA;
		}
	}

	/// @return the content id of the new node definition
	content_t addNode(BenchNode &node)
	{
		auto *imgr = static_cast<IWritableItemDefManager *>(gdef.getItemDefManager());
		ItemDefinition itemdef;
		itemdef.type = ITEM_NODE;
		itemdef.name = node.f.name;
		itemdef.description = node.f.name;
		imgr->registerItem(itemdef);
		return gdef.ndef()->set(node.f.name, std::move(node.f));
	}

	void finalize()
	{
		gdef.ndef()->resolveCrossrefs();
		gdef.ndef()->applyFunction([] (ContentFeatures &f) {
			if (!f.visuals)
				f.visuals = std::make_unique<NodeVisuals>();
		});
	}

	/// Stamps the given contents into the whole region, cycling deterministically
	void stamp(const std::vector<content_t> &contents)
	{
		assert(!contents.empty());
		u32 i = 0;
		for (s16 z = 0; z < (s16) side; z++)
		for (s16 y = 0; y < (s16) side; y++)
		for (s16 x = 0; x < (s16) side; x++) {
			data.m_vmanip.setNodeNoEmerge(v3s16(x, y, z),
					MapNode(contents[i % contents.size()], 0, 0));
			i++;
		}
	}

	/// Generates the block mesh and hashes the collected geometry
	u64 generate(u32 &out_vertices, u32 &out_indices, u32 &out_buffers)
	{
		MeshCollector collector(v3f(0), v3f(0));
		MapblockMeshGenerator generator(&data, &collector);
		generator.generate();

		// FNV-1a over everything that decides how the block is drawn
		u64 hash = 1469598103934665603ULL;
		auto mix = [&hash] (const void *bytes_, size_t len) {
			const u8 *bytes = (const u8 *) bytes_;
			for (size_t j = 0; j < len; j++) {
				hash ^= bytes[j];
				hash *= 1099511628211ULL;
			}
		};

		out_vertices = out_indices = out_buffers = 0;
		for (u8 layer = 0; layer < MAX_TILE_LAYERS; layer++) {
			for (const PreMeshBuffer &p : collector.prebuffers[layer]) {
				out_buffers++;
				// field by field: TileLayer holds a texture pointer and both it and
				// the vertex padding would make the hash depend on addresses
				mix(&layer, sizeof(layer));
				mix(&p.layer.texture_id, sizeof(p.layer.texture_id));
				mix(&p.layer.shader_id, sizeof(p.layer.shader_id));
				mix(&p.layer.material_flags, sizeof(p.layer.material_flags));
				mix(&p.layer.material_type, sizeof(p.layer.material_type));
				mix(&p.layer.color, sizeof(p.layer.color));
				mix(&p.layer.has_color, sizeof(p.layer.has_color));
				for (const video::S3DVertex &v : p.vertices) {
					out_vertices++;
					mix(&v.Pos, sizeof(v.Pos));
					mix(&v.Normal, sizeof(v.Normal));
					mix(&v.Color, sizeof(v.Color));
					mix(&v.TCoords, sizeof(v.TCoords));
					mix(&v.Aux, sizeof(v.Aux));
				}
				mix(p.indices.data(), p.indices.size() * sizeof(u16));
				out_indices += (u32) p.indices.size();
			}
		}
		return hash;
	}

	u64 runOnce(u32 &vertices, u32 &indices, u32 &buffers)
	{
		return generate(vertices, indices, buffers);
	}

private:
	u32 side;
	GDef gdef;
	MeshMakeData data;
};

enum Kind {
	PLAIN,      // NDT_NORMAL, backfaces and hidden faces culled
	ALLFACES,   // NDT_ALLFACES, every face of every node is drawn
	MESH,       // NDT_MESH, one model per node
};

struct Case {
	const char *name;
	u32 side;
	u32 num_nodes;      // number of node definitions (=> materials)
	Kind kind;
	u32 mesh_side;      // for MESH: grid size of the model
};

// Keeps the definitions alive; their mesh ownership moves into the NodeDefManager.
struct Fixture {
	explicit Fixture(const Case &c) : bench(c.side)
	{
		std::vector<content_t> ids;
		for (u32 i = 0; i < c.num_nodes; i++) {
			std::string name = std::string(c.name) + "_n" + std::to_string(i);
			defs.emplace_back(name, 1000 + i);
			BenchNode &node = defs.back();
			node.f.drawtype = (c.kind == PLAIN) ? NDT_NORMAL :
					(c.kind == ALLFACES ? NDT_ALLFACES : NDT_MESH);
			if (c.kind == MESH)
				node.f.visuals->mesh_ptr = makeDomeMesh(c.mesh_side);
			ids.push_back(bench.addNode(node));
		}
		bench.finalize();
		bench.stamp(ids);
	}

	std::deque<BenchNode> defs;
	MeshgenCase bench;
};

void reportHash(const char *name, u64 hash, u32 vertices, u32 indices, u32 buffers)
{
	const char *path = getenv("BENCH_MESHGEN_OUT");
	if (!path)
		return;
	FILE *f = fopen(path, "a");
	if (!f)
		return;
	fprintf(f, "%-16s vertices=%-9u indices=%-9u buffers=%-6u hash=%016llx\n",
			name, vertices, indices, buffers, (unsigned long long) hash);
	fclose(f);
}

// side^3 is the node count, so MESH cases use a small side: a high-poly model
// per node multiplies the vertex count quickly.
static const Case cases[] = {
	// solid nodes, one material: the cheap case, must not regress
	{"plain_1",         16,   1, PLAIN,      0},
	// every face of every node drawn, one material: many appends
	{"allfaces_1",      16,   1, ALLFACES,   0},
	// same geometry spread over many materials: lookup scaling
	{"allfaces_128",    16, 128, ALLFACES,   0},
	{"allfaces_2048",   16, 2048, ALLFACES,  0},
	// mesh nodes, small model, many of them
	{"mesh_lo_1",       16,   1, MESH,      16},
	{"mesh_lo_128",     16, 128, MESH,      16},
	// high-poly models (2601 vertices each)
	{"mesh_hi_1",        8,   1, MESH,      50},
	{"mesh_hi_64",       8,  64, MESH,      50},
};

} // anonymous namespace

TEST_CASE("benchmark_meshgen")
{
	for (const Case &c : cases) {
		Fixture f(c);
		u64 hash = 0;
		u32 vertices = 0, indices = 0, buffers = 0;

		BENCHMARK_ADVANCED((std::string("meshgen_") + c.name).c_str())
		(Catch::Benchmark::Chronometer meter) {
			meter.measure([&] {
				// writing to the outer variables keeps the result observable
				hash = f.bench.runOnce(vertices, indices, buffers);
			});
		};

		f.bench.runOnce(vertices, indices, buffers);
		reportHash(c.name, hash, vertices, indices, buffers);
		INFO("meshgen_" << c.name << " buffers=" << buffers <<
				" vertices=" << vertices << " hash=" << std::hex << hash);
		// There has to be something to generate for the numbers to mean anything
		CHECK(vertices > 0);
		CHECK(indices > 0);
	}
}
