// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "instanced_entity_renderer.h"
#include "log.h"
#include <algorithm>

InstancedEntityRenderer::InstancedEntityRenderer(scene::ISceneManager *smgr,
		video::IVideoDriver *driver)
	: m_smgr(smgr), m_driver(driver)
{
}

InstancedEntityRenderer::~InstancedEntityRenderer()
{
	clearCache();
}

void InstancedEntityRenderer::checkInstancingSupport()
{
	if (m_hw_instancing_checked)
		return;
	m_hw_instancing_checked = true;
	// CPU-merge batching works everywhere (llvmpipe, pixman, GL, GLES).
	// GPU instancing via glDrawElementsInstanced would be a follow-up.
	m_hw_instancing_supported = false;
}

void InstancedEntityRenderer::queueInstance(scene::IMesh *mesh,
		const core::matrix4 &transform, const video::SMaterial &material,
		const std::string &mesh_key)
{
	if (!mesh)
		return;

	MeshBatch &batch = m_batches[mesh_key];
	if (!batch.mesh) {
		batch.mesh = mesh;
		batch.mesh->grab();
	}
	batch.instances.push_back({transform, material});
	m_stats.total_instances++;
}

void InstancedEntityRenderer::render()
{
	checkInstancingSupport();

	u32 batch_count = 0;
	m_stats.draw_calls = 0;
	m_stats.merged_vertices = 0;

	// Save and restore world transform
	core::matrix4 saved_world = m_driver->getTransform(video::ETS_WORLD);
	core::matrix4 identity;
	m_driver->setTransform(video::ETS_WORLD, identity);

	for (auto &pair : m_batches) {
		MeshBatch &batch = pair.second;
		if (batch.instances.empty())
			continue;
		renderBatchFallback(batch);
		batch.instances.clear();
		batch_count++;
	}

	m_driver->setTransform(video::ETS_WORLD, saved_world);
	m_stats.batch_count = batch_count;
}

void InstancedEntityRenderer::renderBatchFallback(MeshBatch &batch)
{
	// Group instances by material identity (texture0, texture1, MaterialType).
	// Within each group, merge all instance geometry into one SMeshBuffer
	// per source meshbuffer, transforming vertices by each instance's matrix.

	struct MatKey {
		const void *tex0;
		const void *tex1;
		s32 mat_type;
		bool operator==(const MatKey &o) const {
			return tex0 == o.tex0 && tex1 == o.tex1 && mat_type == o.mat_type;
		}
	};
	struct MatKeyHash {
		size_t operator()(const MatKey &k) const {
			size_t h = std::hash<const void*>{}(k.tex0);
			h ^= std::hash<const void*>{}(k.tex1) + 0x9e3779b9 + (h<<6) + (h>>2);
			h ^= std::hash<s32>{}(k.mat_type) + 0x9e3779b9 + (h<<6) + (h>>2);
			return h;
		}
	};

	std::unordered_map<MatKey, std::vector<size_t>, MatKeyHash> groups;
	groups.reserve(batch.instances.size());
	for (size_t i = 0; i < batch.instances.size(); ++i) {
		const auto &mat = batch.instances[i].material;
		MatKey k{
			mat.getTexture(0),
			mat.getTexture(1),
			(s32)mat.MaterialType
		};
		groups[k].push_back(i);
	}

	const u32 mb_count = batch.mesh->getMeshBufferCount();
	for (auto &grp : groups) {
		const auto &indices = grp.second;
		const video::SMaterial &ref_mat = batch.instances[indices[0]].material;
		m_driver->setMaterial(ref_mat);

		for (u32 mb = 0; mb < mb_count; ++mb) {
			scene::IMeshBuffer *src = batch.mesh->getMeshBuffer(mb);
			if (!src)
				continue;

			const u32 src_vtx = src->getVertexCount();
			const u32 src_idx = src->getIndexCount();
			if (src_vtx == 0 || src_idx == 0)
				continue;

			// Only handle S3DVertex (EVT_STANDARD) for now
			if (src->getVertexType() != video::EVT_STANDARD)
				continue;

			const auto *src_vptr = static_cast<const video::S3DVertex*>(src->getVertices());
			const u16 *src_iptr = src->getIndices();

			// Allocate merged buffer
			scene::SMeshBuffer *merged = new scene::SMeshBuffer();
			merged->Material = ref_mat;
			merged->Vertices->Data.reserve(src_vtx * indices.size());
			merged->Indices->Data.reserve(src_idx * indices.size());

			for (size_t ii = 0; ii < indices.size(); ++ii) {
				const core::matrix4 &xf = batch.instances[indices[ii]].transform;
				const size_t voff = merged->Vertices->Data.size();

				// Copy and transform vertices
				for (u32 v = 0; v < src_vtx; ++v) {
					video::S3DVertex vtx = src_vptr[v];
					xf.transformVect(vtx.Pos);
					// Transform normal (approximation - works for orthogonal matrices)
					v3f n = vtx.Normal;
					xf.transformVect(n);
					vtx.Normal = n;
					merged->Vertices->Data.push_back(vtx);
				}

				// Copy and fixup indices
				for (u32 i = 0; i < src_idx; ++i)
					merged->Indices->Data.push_back(src_iptr[i] + voff);
			}

			merged->setHardwareMappingHint(scene::EHM_STREAM);
			m_driver->updateHardwareBuffer(merged->getVertexBuffer());
			m_driver->updateHardwareBuffer(merged->getIndexBuffer());

			m_driver->drawMeshBuffer(merged);
			m_stats.draw_calls++;
			m_stats.merged_vertices += merged->getVertexCount();
			merged->drop();
		}
	}
}

void InstancedEntityRenderer::clearCache()
{
	for (auto &pair : m_batches) {
		if (pair.second.mesh)
			pair.second.mesh->drop();
	}
	m_batches.clear();
}
