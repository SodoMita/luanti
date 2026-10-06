// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "meshbatch.h"

#include "client/shader.h"
#include "client/tile.h"
#include "irr_v3d.h"
#include "profiler.h"
#include "settings.h"

#include <IMesh.h>
#include <IMeshBuffer.h>
#include <ISceneManager.h>
#include <IVideoDriver.h>
#include <IMaterialRenderer.h>

ShaderFeatures MeshBatchDrawer::getInstancedFeatures()
{
	ShaderFeatures features;
	features.instancing = true;
	return features;
}

MeshBatchDrawer::MeshBatchDrawer(scene::ISceneNode *parent, scene::ISceneManager *mgr,
		video::IVideoDriver *driver, IShaderSource *shaders) :
	scene::ISceneNode(parent, mgr, -1),
	m_driver(driver), m_shaders(shaders)
{
	setAutomaticCulling(scene::EAC_OFF);
	m_bbox.reset(0.0f, 0.0f, 0.0f);

	if (!m_driver || !m_shaders) {
		m_enabled = false;
		return;
	}

	// Hardware instancing is required, otherwise this is not worth it
	m_enabled = m_driver->queryInstancingSupport() &&
		g_settings->getBool("client_entity_batching");
	// The shadow map renderer draws scene nodes again with an override material,
	// which would not match the instanced shaders here.
	if (g_settings->getBool("enable_dynamic_shadows"))
		m_enabled = false;
}

MeshBatchDrawer::~MeshBatchDrawer()
{
}

video::E_MATERIAL_TYPE MeshBatchDrawer::getInstancedMaterialType(video::E_MATERIAL_TYPE base)
{
	if (base == video::EMT_INVALID)
		return video::EMT_INVALID;

	auto it = m_material_cache.find((int)base);
	if (it != m_material_cache.end())
		return (video::E_MATERIAL_TYPE)it->second;

	const ShaderInfo &info = m_shaders->getShaderInfo((u32)base);
	video::E_MATERIAL_TYPE result = base;

	if (!info.name.empty()) {
		ShaderConstants consts = info.input_constants;
		consts["USE_INSTANCING"] = 1;
		u32 id = m_shaders->getShader(info.name, consts, info.base_material, info.setter_cb.get());
		if (id != 0) {
			const ShaderInfo &newinfo = m_shaders->getShaderInfo(id);
			if (newinfo.material != video::EMT_INVALID)
				result = newinfo.material;
			else
				m_enabled = false; // instancing unsupported for this shader
		} else {
			m_enabled = false;
		}
	}

	m_material_cache.emplace((int)base, (int)result);
	return result;
}

void MeshBatchDrawer::add(const scene::IMesh *mesh, const core::matrix4 &world,
		const MaterialOverride &mat)
{
	if (!m_enabled || !mesh)
		return;

	// Safety valve: if the scene is not drawn (e.g. paused), don't grow forever
	if (m_pending.size() >= 100000)
		return;

	m_pending.push_back({mesh, world, mat});
}

void MeshBatchDrawer::OnRegisterSceneNode()
{
	if (!m_enabled || !m_visible) {
		m_pending.clear();
		m_stats_drawcalls = m_stats_batches = m_stats_instances = m_stats_culled = 0;
		return;
	}

	buildBatches();

	// Register for the passes we have content for
	bool have_opaque = false, have_transparent = false;
	for (auto &it : m_batches) {
		if (it.second.transparent)
			have_transparent = true;
		else
			have_opaque = true;
	}

	if (have_opaque)
		SceneManager->registerNodeForRendering(this, scene::ESNRP_SOLID);
	if (have_transparent)
		SceneManager->registerNodeForRendering(this, scene::ESNRP_TRANSPARENT);

	ISceneNode::OnRegisterSceneNode();
}

void MeshBatchDrawer::buildBatches()
{
	ScopeProfiler sp(g_profiler, "MeshBatch: build batches [ms]", SPT_AVG);

	m_batches.clear();
	m_stats_culled = 0;
	m_bbox.reset(0.0f, 0.0f, 0.0f);
	bool first_box = true;

	for (const Instance &inst : m_pending) {
		const u32 bufcount = inst.mesh->getMeshBufferCount();
		// Animated meshes (skinning) and meshes with animated textures are not
		// handled here; the caller only passes meshes that are safe to batch.
		const core::aabbox3d<f32> &box = inst.mesh->getBoundingBox();
		v3f center = box.getCenter();
		inst.world.transformVect(center);
		f32 radius = box.getExtent().getLength() * 0.5f *
			inst.world.getScale().getLength() / sqrtf(3.0f);
		if (radius <= 0.0f)
			radius = 0.01f;

		if (first_box) {
			m_bbox.reset(center.X, center.Y, center.Z);
			first_box = false;
		} else {
			m_bbox.addInternalPoint(center);
		}

		if (m_frustum_culler && m_frustum_culler(center, radius)) {
			m_stats_culled++;
			continue;
		}

		for (u32 i = 0; i < bufcount; i++) {
			scene::IMeshBuffer *buffer = inst.mesh->getMeshBuffer(i);
			if (!buffer || buffer->getPrimitiveCount() == 0)
				continue;

			video::SMaterial material = buffer->getMaterial();
			if (inst.mat.material_type != video::EMT_INVALID)
				material.MaterialType = inst.mat.material_type;
			if (inst.mat.set_backface_culling)
				material.BackfaceCulling = inst.mat.backface_culling;

			// Materials with multiple texture layers are not supported
			if (material.getTexture(2))
				continue;

			BatchKey key{};
			key.buffer = buffer;
			key.material_type = material.MaterialType;
			key.texture0 = material.getTexture(0);
			key.texture1 = material.getTexture(1);
			key.backface_culling = material.BackfaceCulling;
			key.color = material.ColorParam.color;

			auto it = m_batches.find(key);
			if (it == m_batches.end()) {
				Batch batch;
				batch.buffer = buffer;
				batch.material = material;
				batch.allow_instancing = m_driver->canDrawInstanced(buffer);

				// Switch to the instanced variant of this shader
				if (batch.allow_instancing) {
					auto type = getInstancedMaterialType(material.MaterialType);
					if (type != video::EMT_INVALID)
						batch.material.MaterialType = type;
				}

				auto *rnd = m_driver->getMaterialRenderer(batch.material.MaterialType);
				batch.transparent = rnd && rnd->isTransparent();

				it = m_batches.emplace(key, std::move(batch)).first;
			}

			Batch &batch = it->second;
			const f32 *m = inst.world.pointer();
			batch.matrices.insert(batch.matrices.end(), m, m + 16);
		}
	}

	m_stats_batches = m_batches.size();
	m_stats_instances = 0;
	m_stats_drawcalls = 0;
	for (auto &it : m_batches) {
		u32 n = it.second.matrices.size() / 16;
		m_stats_instances += n;
		// One draw call for all instances, unless the driver cannot instance
		m_stats_drawcalls += it.second.allow_instancing ? 1 : n;
	}

	m_pending.clear();
}

void MeshBatchDrawer::render()
{
	if (!m_enabled)
		return;

	const auto pass = SceneManager->getSceneNodeRenderPass();
	const bool transparent_pass = pass == scene::ESNRP_TRANSPARENT;

	for (auto &it : m_batches) {
		Batch &batch = it.second;
		const u32 count = batch.matrices.size() / 16;
		if (count == 0 || batch.transparent != transparent_pass)
			continue;

		m_driver->setMaterial(batch.material);
		m_driver->drawMeshBufferInstanced(batch.buffer, batch.matrices.data(), count);
	}
}
