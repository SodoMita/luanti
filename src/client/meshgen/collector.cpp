// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2018 numzero, Lobachevskiy Vitaliy <numzer0@yandex.ru>

#include "collector.h"
#include <algorithm>
#include <stdexcept>
#include <cassert>
#include <unordered_map>

bool PreMeshBuffer::append(const PreMeshBuffer &other)
{
	const size_t nv = vertices.size();
	const size_t ni = indices.size();
	if (nv + other.vertices.size() > U16_MAX)
		return false;

	vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
	indices.insert(indices.end(), other.indices.begin(), other.indices.end());
	for (size_t i = ni; i < indices.size(); i++)
		indices[i] += nv;
	return true;
}

void MeshCollector::append(const TileSpec &tile, const video::S3DVertex *vertices,
		u32 numVertices, const u16 *indices, u32 numIndices)
{
	for (int layernum = 0; layernum < MAX_TILE_LAYERS; layernum++) {
		const TileLayer &layer = tile.layers[layernum];
		if (layer.empty())
			continue;
		append(layer, vertices, numVertices, indices, numIndices, layernum);
	}
}

void MeshCollector::append(const TileLayer &layer, const video::S3DVertex *vertices,
		u32 numVertices, const u16 *indices, u32 numIndices, u8 layernum)
{
	PreMeshBuffer &p = findBuffer(layer, layernum, numVertices);

	const u16 aux = layer.texture_layer_idx;
	// Baking the tile color in here saves a second pass over every vertex of the
	// block later on (see also applyColorAndMerge()).
	const bool bake = m_bake_tile_color &&
			!PreMeshBuffer::isColorIdentity(layer.color);
	const video::SColor &tc = layer.color;

	u32 vertex_count = p.vertices.size();
	assert(vertex_count + numVertices <= U16_MAX);

	f32 radius_sq = m_bounding_radius_sq;
	const v3f &center = m_center_pos;
	for (u32 i = 0; i < numVertices; i++) {
		const video::S3DVertex &src = vertices[i];
		video::SColor color = src.Color;
		if (bake)
			PreMeshBuffer::bakeColor(color, tc);
		// constructed in place, no intermediate vertex
		p.vertices.emplace_back(src.Pos + offset, src.Normal, color, src.TCoords, aux);
		// Kept in a local so the maximum is not a loop-carried dependency
		radius_sq = std::max(radius_sq, (src.Pos - center).getLengthSQ());
	}
	m_bounding_radius_sq = radius_sq;

	for (u32 i = 0; i < numIndices; i++)
		p.indices.push_back(indices[i] + vertex_count);
}

PreMeshBuffer &MeshCollector::findBuffer(
		const TileLayer &layer, u8 layernum, u32 numVertices)
{
	if (numVertices > U16_MAX)
		throw std::invalid_argument(
				"Mesh can't contain more than 65536 vertices");
	std::vector<PreMeshBuffer> &buffers = prebuffers[layernum];
	// Most appends repeat the material of the previous one (all faces of a node
	// share it), so check the buffer that was used last before paying for a
	// hash. Only the newest buffer of a material can have room, so this picks
	// the same buffer the general path below would.
	if (!buffers.empty()) {
		PreMeshBuffer &last = buffers.back();
		if (last.layer == layer && last.vertices.size() + numVertices <= U16_MAX)
			return last;
	}

	std::unordered_map<TileLayer, u32> &index = m_open_buffers[layernum];

	auto it = index.find(layer);
	if (it != index.end()) {
		PreMeshBuffer &p = buffers[it->second];
		if (p.vertices.size() + numVertices <= U16_MAX)
			return p;

		// The open buffer of this material is full and another one is started.
		// It will most likely fill up as well, so size it right once instead of
		// growing it by doubling (each growth copies the whole buffer).
		buffers.emplace_back(layer, U16_MAX);
		it->second = u32(buffers.size() - 1);
		return buffers.back();
	}

	buffers.emplace_back(layer, std::min<u32>(U16_MAX, numVertices * 8));
	index.emplace(layer, u32(buffers.size() - 1));
	return buffers.back();
}

void applyColorAndMerge(std::vector<PreMeshBuffer> &prebuffers, bool colors_baked)
{
	// Tile colors are baked into the vertices by the collector already
	// (see also MeshCollector::setBakeTileColor()), which saves a full pass over
	// every vertex of the block. The color fields still have to be erased here,
	// so that buffers which only differ in their tile color get merged.
	for (auto &p : prebuffers) {
		if (!colors_baked)
			p.applyTileColor();
		// erase color information for later comparisons
		p.layer.has_color = false;
		p.layer.color = 0;
	}

	std::unordered_map<TileLayer, size_t> seen;
	for (size_t i = 0; i < prebuffers.size(); i++) {
		PreMeshBuffer &p = prebuffers[i];
		auto it = seen.find(p.layer);
		if (it == seen.end()) { // first time
			seen[p.layer] = i;
			continue;
		}
		// merge
		auto &dst = prebuffers[it->second];
		assert(p.layer == dst.layer);
		if (dst.append(p)) {
			p = PreMeshBuffer();
		} else {
			// other buffer full, this one becomes the new target
			it->second = i;
		}
	}

	// remove all empty buffers
	prebuffers.erase(std::remove_if(prebuffers.begin(), prebuffers.end(),
		[] (const PreMeshBuffer &p) {
		return p.empty();
	}), prebuffers.end());
}
