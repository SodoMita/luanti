// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2018 numzero, Lobachevskiy Vitaliy <numzer0@yandex.ru>

#pragma once
#include <array>
#include <unordered_map>
#include <vector>
#include "irrlichttypes.h"
#include "irr_v3d.h"
#include <S3DVertex.h>
#include "client/tile.h"

struct PreMeshBuffer
{
	TileLayer layer;
	std::vector<u16> indices;
	std::vector<video::S3DVertex> vertices;

	PreMeshBuffer() = default;
	explicit PreMeshBuffer(const TileLayer &layer) : layer(layer) {}

	explicit PreMeshBuffer(const TileLayer &layer, u32 expected_vertices) :
			layer(layer)
	{
		reserve(expected_vertices);
	}

	bool empty() const {
		return indices.empty();
	}

	/// @brief Reserve room for geometry that is known to be coming.
	/// Growing the vertex array by doubling copies everything that was already
	/// collected, which is a whole extra pass over the block for high-poly
	/// geometry that fills a buffer up to the 16-bit index limit.
	void reserve(u32 expected_vertices)
	{
		vertices.reserve(expected_vertices);
		indices.reserve(expected_vertices * 3 / 2);
	}

	/// @brief Multiplies a single vertex color by a tile color
	static void bakeColor(video::SColor &color, const video::SColor &tc)
	{
		color.set(color.getAlpha(),
			color.getRed() * tc.getRed() / 255U,
			color.getGreen() * tc.getGreen() / 255U,
			color.getBlue() * tc.getBlue() / 255U);
	}

	/// @brief Tells whether a tile layer tints its vertices at all
	static bool isColorIdentity(const video::SColor &tc)
	{
		return tc == video::SColor(0xFFFFFFFF);
	}

	/// @brief Colorizes vertices as indicated by tile layer
	void applyTileColor()
	{
		video::SColor tc = layer.color;
		if (isColorIdentity(tc))
			return;
		for (auto &vertex : vertices)
			bakeColor(vertex.Color, tc);
	}

	/// @brief Append another buffer to this one
	/// @return false if index would overflow
	bool append(const PreMeshBuffer &other);
};

struct MeshCollector
{
	std::array<std::vector<PreMeshBuffer>, MAX_TILE_LAYERS> prebuffers;
	// bounding sphere radius and center
	f32 m_bounding_radius_sq = 0.0f;
	v3f m_center_pos;
	v3f offset;

	// center_pos: pos to use for bounding-sphere, in BS-space
	// offset: offset added to vertices
	MeshCollector(const v3f center_pos, v3f offset = v3f()) : m_center_pos(center_pos), offset(offset) {}

	void append(const TileSpec &material,
			const video::S3DVertex *vertices, u32 numVertices,
			const u16 *indices, u32 numIndices);

	/**
	 * Append vertices that still need a per-vertex transformation, writing the
	 * result straight into the destination buffer.
	 *
	 * The alternative is transforming into a scratch array and handing that to
	 * append(), which costs an extra 36-byte store and load per vertex. Mesh
	 * nodes are where that matters: every vertex of every model in a block goes
	 * through here, measured at ~34ns per vertex of which the scratch round trip
	 * was ~11ns.
	 *
	 * `transform` is called exactly once per vertex and must return the vertex
	 * as append() would have received it: already oriented, lit and translated
	 * by the node's origin, but *not* by the collector offset and with the tile
	 * colour not yet baked in. Holding that contract is what makes this and the
	 * scratch path produce byte-identical buffers.
	 */
	template<typename F>
	void appendTransformed(const TileLayer &layer,
			const video::S3DVertex *vertices, u32 numVertices,
			const u16 *indices, u32 numIndices, u8 layernum, F &&transform)
	{
		PreMeshBuffer &p = findBuffer(layer, layernum, numVertices);

		const u16 aux = layer.texture_layer_idx;
		const bool bake = m_bake_tile_color &&
				!PreMeshBuffer::isColorIdentity(layer.color);
		const video::SColor &tc = layer.color;

		const u32 vertex_count = p.vertices.size();
		assert(vertex_count + numVertices <= U16_MAX);

		f32 radius_sq = m_bounding_radius_sq;
		const v3f &center = m_center_pos;
		for (u32 i = 0; i < numVertices; i++) {
			video::S3DVertex v = transform(vertices[i]);
			video::SColor color = v.Color;
			if (bake)
				PreMeshBuffer::bakeColor(color, tc);
			// constructed in place, no intermediate vertex
			p.vertices.emplace_back(v.Pos + offset, v.Normal, color, v.TCoords, aux);
			// measured before the collector offset, exactly as append() does, and
			// kept in a local so the maximum is not a loop-carried dependency
			radius_sq = std::max(radius_sq, (v.Pos - center).getLengthSQ());
		}
		m_bounding_radius_sq = radius_sq;

		for (u32 i = 0; i < numIndices; i++)
			p.indices.push_back(indices[i] + vertex_count);
	}

	/**
	 * Bake the tile color into the vertex colors while collecting instead of in
	 * a separate pass over every buffer afterwards.
	 * Only enable this where the tile color is meant to be applied at all (see
	 * also MapBlockMesh, which used to do this in applyColorAndMerge()).
	 */
	void setBakeTileColor(bool bake) { m_bake_tile_color = bake; }

	/// @return whether vertex colors already contain the tile color
	bool tileColorBaked() const { return m_bake_tile_color; }

private:
	void append(const TileLayer &material,
			const video::S3DVertex *vertices, u32 numVertices,
			const u16 *indices, u32 numIndices,
			u8 layernum);

	PreMeshBuffer &findBuffer(const TileLayer &layer, u8 layernum, u32 numVertices);

	bool m_bake_tile_color = false;

	/**
	 * For each layer: the last buffer of a material that still takes geometry.
	 *
	 * Buffers of one material are always filled in order (a new one is only
	 * created once the previous is full), so the newest one is also the first
	 * one with room. That makes this lookup equivalent to the scan it replaces,
	 * but O(1) instead of O(buffers). The scan went quadratic exactly where it
	 * hurts: high-poly mesh nodes produce many full 65535-vertex buffers per
	 * material, and every append walked past all of them.
	 *
	 * Only indices are stored, since the buffer vector grows during collection.
	 * Nothing here is used once mapblock generation is over, which is also when
	 * buffers start moving again (merging).
	 */
	std::array<std::unordered_map<TileLayer, u32>, MAX_TILE_LAYERS> m_open_buffers;
};


/**
 * Applies tile colors to the vertices if the collector did not already bake
 * them, and merges the buffers that ended up with the same material.
 * @param prebuffers buffers of one tile layer, as collected
 * @param colors_baked whether the tile color is already part of the vertices
 */
void applyColorAndMerge(std::vector<PreMeshBuffer> &prebuffers, bool colors_baked);
