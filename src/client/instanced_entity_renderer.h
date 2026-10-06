// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// GPU instanced rendering for entities with identical meshes

#pragma once

#include "irrlichttypes_bloated.h"
#include <IMesh.h>
#include <IMeshBuffer.h>
#include <IMeshSceneNode.h>
#include <ISceneManager.h>
#include <IVideoDriver.h>
#include <SMaterial.h>
#include <CMeshBuffer.h>
#include <matrix4.h>
#include <unordered_map>
#include <vector>
#include <string>
#include "irr_ptr.h"

/**
 * Manages batched rendering for entities sharing the same mesh.
 *
 * Strategy:
 *  1. Entities register a mesh-key + material + world-matrix each frame
 *     (or on change).
 *  2. At render time, entities with identical mesh+material are merged
 *     into a single SMeshBuffer with translated geometry (CPU merge
 *     batching, similar to the mapblock grouped_buffers code).
 *  3. When the GL driver exposes glDrawElementsInstanced we further
 *     collapse into a single instanced draw per batch via a per-instance
 *     world-matrix VBO bound as a divisor=1 attribute.
 *
 * This replaces the per-entity IAnimatedMeshSceneNode path for the
 * static-mesh entity case (OBJECTVISUAL_CUBE, non-animated
 * OBJECTVISUAL_MESH, OBJECTVISUAL_NODE, OBJECTVISUAL_UPRIGHT_SPRITE).
 */
class InstancedEntityRenderer
{
public:
	InstancedEntityRenderer(scene::ISceneManager *smgr,
			video::IVideoDriver *driver);
	~InstancedEntityRenderer();

	// Queue an entity for rendering this frame. The renderer is stateless
	// across frames – call render() then clear() at end of frame.
	void queueInstance(scene::IMesh *mesh, const core::matrix4 &transform,
			const video::SMaterial &material, const std::string &mesh_key);

	// Render all queued instances and reset.
	void render();

	// Drop cached merged buffers (call on video mode change, etc.)
	void clearCache();

	struct Stats {
		u32 total_instances;
		u32 batch_count;
		u32 draw_calls;
		u32 merged_vertices;
	};
	Stats getStats() const { return m_stats; }

private:
	struct InstanceEntry {
		core::matrix4 transform;
		video::SMaterial material;
	};

	struct MeshBatch {
		scene::IMesh *mesh = nullptr;
		std::vector<InstanceEntry> instances;
	};

	scene::ISceneManager *m_smgr;
	video::IVideoDriver *m_driver;

	// Keyed by mesh_key string
	std::unordered_map<std::string, MeshBatch> m_batches;
	Stats m_stats;

	bool m_hw_instancing_checked = false;
	bool m_hw_instancing_supported = false;

	void checkInstancingSupport();
	void renderBatchFallback(MeshBatch &batch);
};
