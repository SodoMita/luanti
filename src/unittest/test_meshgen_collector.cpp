// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Arena agent EmberKite-4193

#include "test.h"
#include "client/meshgen/collector.h"
#include "client/tile.h"
#include <algorithm>
#include <vector>

/*
	Tests for MeshCollector, the structure mapblock generation appends its
	geometry into. These cover the bookkeeping that lets generation find the
	right buffer quickly: which buffer geometry lands in, in which order, and
	when the tile color is applied.
*/

namespace {

/// Deterministic blob of geometry (count must be a multiple of 3)
void makeGeometry(u32 seed, u32 count, std::vector<video::S3DVertex> &vertices,
		std::vector<u16> &indices)
{
	vertices.clear();
	indices.clear();
	for (u32 i = 0; i < count; i++) {
		f32 x = (f32) ((seed * 37 + i * 13) % 101) - 50.0f;
		f32 y = (f32) ((seed * 53 + i * 7) % 61) - 30.0f;
		f32 z = (f32) ((seed * 11 + i * 3) % 43) - 21.0f;
		vertices.emplace_back(v3f(x, y, z), v3f(0.0f, 1.0f, 0.0f),
				video::SColor(255, 200, 150, 100), v2f(x / 10.0f, y / 10.0f));
	}
	for (u32 i = 0; i + 2 < count; i += 3)
		indices.insert(indices.end(), {(u16) i, (u16) (i + 1), (u16) (i + 2)});
}

TileSpec makeTile(u32 texture_id, video::SColor color, bool has_color)
{
	TileSpec tile;
	tile.layers[0].texture_id = texture_id;
	tile.layers[0].color = color;
	tile.layers[0].has_color = has_color;
	return tile;
}

TileSpec makeTile(u32 texture_id)
{
	return makeTile(texture_id, video::SColor(0xFFFFFFFF), false);
}

bool vertexEqual(const video::S3DVertex &a, const video::S3DVertex &b)
{
	return a.Pos == b.Pos && a.Normal == b.Normal && a.Color == b.Color &&
			a.TCoords == b.TCoords && a.Aux == b.Aux;
}

void expectBuffersEqual(const PreMeshBuffer &lhs, const PreMeshBuffer &rhs)
{
	UASSERTEQ(size_t, lhs.vertices.size(), rhs.vertices.size());
	UASSERTEQ(size_t, lhs.indices.size(), rhs.indices.size());
	UASSERT(lhs.layer == rhs.layer);
	for (size_t i = 0; i < lhs.vertices.size(); i++)
		UASSERT(vertexEqual(lhs.vertices[i], rhs.vertices[i]));
	for (size_t i = 0; i < lhs.indices.size(); i++)
		UASSERTEQ(u16, lhs.indices[i], rhs.indices[i]);
}

} // anonymous namespace

class TestMeshCollector : public TestBase {
public:
	TestMeshCollector() { TestManager::registerTestModule(this); }
	const char *getName() override { return "TestMeshCollector"; }

	void runTests(IGameDef *gamedef) override;

	void testBufferFilling();
	void testTileColorBakeEquivalent();
};

static TestMeshCollector g_test_instance;

void TestMeshCollector::runTests(IGameDef *gamedef)
{
	TEST(testBufferFilling);
	TEST(testTileColorBakeEquivalent);
}

void TestMeshCollector::testBufferFilling()
{
	// One material, more geometry than fits into a single buffer: a second
	// buffer is opened and geometry keeps its order across both.
	const u32 per_append = 300;
	const u32 appends = 250;

	std::vector<video::S3DVertex> vertices;
	std::vector<u16> indices;

	MeshCollector col(v3f(0.0f, 0.0f, 0.0f));
	f32 expected_radius_sq = 0.0f;
	for (u32 i = 0; i < appends; i++) {
		makeGeometry(i, per_append, vertices, indices);
		col.append(makeTile(7), vertices.data(), vertices.size(),
				indices.data(), indices.size());
		for (const auto &v : vertices)
			expected_radius_sq = std::max(expected_radius_sq, v.Pos.getLengthSQ());
	}

	auto &buffers = col.prebuffers[0];
	// 16 bit indices cap one buffer, so this must have overflowed
	UASSERT((u32) appends * per_append > U16_MAX);
	UASSERTEQ(size_t, buffers.size(), 2);
	UASSERTEQ(size_t, buffers[0].vertices.size(), (size_t) (U16_MAX / per_append) * per_append);
	UASSERTEQ(size_t, buffers[1].vertices.size(),
			(size_t) appends * per_append - buffers[0].vertices.size());
	UASSERT(col.m_bounding_radius_sq > expected_radius_sq * 0.99f);
	UASSERT(col.m_bounding_radius_sq < expected_radius_sq * 1.01f);

	// The concatenation of all buffers is exactly the sequence that was appended
	size_t next = 0;
	for (const PreMeshBuffer &p : buffers) {
		UASSERTEQ(u32, p.layer.texture_id, 7);
		UASSERT(!p.empty());
		for (const video::S3DVertex &v : p.vertices) {
			makeGeometry((u32) (next / per_append), per_append, vertices, indices);
			UASSERT(vertexEqual(v, vertices[next % per_append]));
			next++;
		}
		UASSERTEQ(size_t, p.indices.size(), p.vertices.size());
		for (size_t i = 0; i < p.indices.size(); i++) {
			// indices reference this buffer only
			UASSERT(p.indices[i] < p.vertices.size());
			UASSERTEQ(u16, p.indices[i], (u16) i);
		}
	}
	UASSERTEQ(size_t, next, (size_t) appends * per_append);

	// A second material must not disturb the first one's buffers
	MeshCollector mixed(v3f(0.0f, 0.0f, 0.0f));
	for (u32 i = 0; i < 40; i++) {
		makeGeometry(i, per_append, vertices, indices);
		mixed.append(makeTile(7), vertices.data(), vertices.size(),
				indices.data(), indices.size());
		mixed.append(makeTile(9), vertices.data(), vertices.size(),
				indices.data(), indices.size());
	}
	UASSERTEQ(size_t, mixed.prebuffers[0].size(), 2);
	UASSERTEQ(u32, mixed.prebuffers[0][0].layer.texture_id, 7);
	UASSERTEQ(u32, mixed.prebuffers[0][1].layer.texture_id, 9);
	UASSERTEQ(size_t, mixed.prebuffers[0][0].vertices.size(), 40 * per_append);
	UASSERTEQ(size_t, mixed.prebuffers[0][1].vertices.size(), 40 * per_append);
}

void TestMeshCollector::testTileColorBakeEquivalent()
{
	// Applying the tile color while collecting must give the same result as
	// applying it to the collected buffers afterwards - including buffers that
	// only differ by their tile color and are therefore merged.
	const video::SColor tint_a(255, 128, 64, 32);
	const video::SColor tint_b(255, 17, 171, 250);

	auto build = [&] (bool bake_here) {
		MeshCollector col(v3f(0.0f, 0.0f, 0.0f));
		col.setBakeTileColor(bake_here);
		std::vector<video::S3DVertex> vertices;
		std::vector<u16> indices;
		for (u32 i = 0; i < 20; i++) {
			makeGeometry(i, 30, vertices, indices);
			col.append(makeTile(7, tint_a, true), vertices.data(), vertices.size(),
					indices.data(), indices.size());
			col.append(makeTile(7, tint_b, true), vertices.data(), vertices.size(),
					indices.data(), indices.size());
			col.append(makeTile(9), vertices.data(), vertices.size(),
					indices.data(), indices.size());
		}
		applyColorAndMerge(col.prebuffers[0], bake_here);
		return col;
	};

	MeshCollector baked = build(true);
	MeshCollector late = build(false);

	// Three materials become two buffers: the two tints share a texture and are
	// merged after the color is dropped from the comparison.
	UASSERTEQ(size_t, baked.prebuffers[0].size(), 2);
	UASSERTEQ(size_t, late.prebuffers[0].size(), 2);
	for (size_t i = 0; i < baked.prebuffers[0].size(); i++)
		expectBuffersEqual(baked.prebuffers[0][i], late.prebuffers[0][i]);

	// And the tint really is in the vertices (otherwise both are trivially equal)
	const auto &first = baked.prebuffers[0][0];
	UASSERT(!first.vertices.empty());
	std::vector<video::S3DVertex> check_vertices;
	std::vector<u16> check_indices;
	makeGeometry(0, 30, check_vertices, check_indices);
	UASSERT(first.vertices[0].Color != check_vertices[0].Color);
	UASSERTEQ(u32, first.vertices[0].Color.getRed(),
			check_vertices[0].Color.getRed() * tint_a.getRed() / 255U);
}
