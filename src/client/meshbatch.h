// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include <ISceneNode.h>
#include <matrix4.h>
#include <SMaterial.h>
#include "irrlichttypes.h"
#include "shader.h"

namespace scene {
	class IMesh;
	class IMeshBuffer;
	class ISceneManager;
}
namespace video {
	class IVideoDriver;
	class ITexture;
}
class IShaderSource;

/**
 * Batches meshes of active objects (entities) into instanced draw calls.
 *
 * Without this, every object with a mesh visual causes one draw call per mesh
 * buffer per frame -- with thousands of objects that is thousands of draw
 * calls, which is far more expensive than the actual geometry.
 *
 * Objects that use the same mesh (and the same material) are drawn together
 * with a single instanced draw call (see IVideoDriver::drawMeshBufferInstanced).
 *
 * Usage:
 *  - the environment calls setFrustumCuller() once per frame
 *  - objects call add() while they are being stepped
 *  - the node itself is registered with the scene manager, which calls
 *    OnRegisterSceneNode() (builds the batches) and render() (draws them)
 */
class MeshBatchDrawer final : public scene::ISceneNode
{
public:
	/// Per-object material adjustments, applied on top of the mesh buffer's own material
	struct MaterialOverride {
		/// Material type (shader) to use, EMT_INVALID to keep the mesh buffer's own
		video::E_MATERIAL_TYPE material_type = video::EMT_INVALID;
		bool set_backface_culling = false;
		bool backface_culling = true;
	};

	MeshBatchDrawer(scene::ISceneNode *parent, scene::ISceneManager *mgr,
			video::IVideoDriver *driver, IShaderSource *shaders);
	~MeshBatchDrawer();

	/// Whether batched drawing can be used at all with the current setup
	bool isEnabled() const { return m_enabled; }

	/// Set the frustum culling function used for the current frame.
	/// Must be set before objects are added.
	template <typename F>
	void setFrustumCuller(F &&f)
	{
		m_frustum_culler = std::forward<F>(f);
	}

	/// Register one instance of a mesh for drawing in this frame
	void add(const scene::IMesh *mesh, const core::matrix4 &world,
			const MaterialOverride &mat);

	/// @return number of instances registered for the current frame
	u32 getPendingCount() const { return m_pending.size(); }

	// Statistics of the last drawn frame
	u32 getLastDrawcalls() const { return m_stats_drawcalls; }
	u32 getLastBatches() const { return m_stats_batches; }
	u32 getLastInstances() const { return m_stats_instances; }
	u32 getLastCulled() const { return m_stats_culled; }

	// scene::ISceneNode
	void OnRegisterSceneNode() override;
	void render() override;
	const core::aabbox3d<f32> &getBoundingBox() const override { return m_bbox; }
	u32 getMaterialCount() const override { return 0; }
	video::SMaterial &getMaterial(u32 i) override
	{
		static video::SMaterial dummy;
		return dummy;
	}
	void setVisible(bool visible) override { m_visible = visible; }
	bool isVisible() const { return m_visible; }
	scene::ESCENE_NODE_TYPE getType() const override { return scene::ESNT_UNKNOWN; }
	
	scene::ISceneNode *clone(scene::ISceneNode *newParent, scene::ISceneManager *newManager) override { return nullptr; }

	/// Shader features used for the instanced variant of a shader
	static ShaderFeatures getInstancedFeatures();

private:
	struct Instance {
		const scene::IMesh *mesh;
		core::matrix4 world;
		MaterialOverride mat;
	};

	/// Identifies a set of instances that can share one draw call
	struct BatchKey {
		const scene::IMeshBuffer *buffer;
		video::E_MATERIAL_TYPE material_type;
		video::ITexture *texture0;
		video::ITexture *texture1;
		bool backface_culling;
		u32 color;

		bool operator==(const BatchKey &o) const
		{
			return buffer == o.buffer && material_type == o.material_type &&
				texture0 == o.texture0 && texture1 == o.texture1 &&
				backface_culling == o.backface_culling && color == o.color;
		}
	};

	struct BatchKeyHash {
		std::size_t operator()(const BatchKey &k) const
		{
			std::size_t h = std::hash<const void *>()(k.buffer);
			h = h * 31 ^ std::hash<int>()((int)k.material_type);
			h = h * 31 ^ std::hash<const void *>()(k.texture0);
			h = h * 31 ^ std::hash<const void *>()(k.texture1);
			h = h * 31 ^ std::hash<int>()((int)k.backface_culling);
			h = h * 31 ^ std::hash<u32>()(k.color);
			return h;
		}
	};

	struct Batch {
		video::SMaterial material;
		/// 16 floats per instance
		std::vector<f32> matrices;
		const scene::IMeshBuffer *buffer = nullptr;
		bool transparent = false;
		bool allow_instancing = false;
	};

	/// Convert a base shader material type to its instanced variant
	video::E_MATERIAL_TYPE getInstancedMaterialType(video::E_MATERIAL_TYPE base);

	void buildBatches();

	video::IVideoDriver *m_driver;
	IShaderSource *m_shaders;
	std::function<bool(v3f, f32)> m_frustum_culler;

	std::vector<Instance> m_pending;
	std::unordered_map<BatchKey, Batch, BatchKeyHash> m_batches;
	std::unordered_map<int, int> m_material_cache;

	core::aabbox3d<f32> m_bbox{-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
	bool m_visible = true;
	bool m_enabled = false;

	u32 m_stats_drawcalls = 0;
	u32 m_stats_batches = 0;
	u32 m_stats_instances = 0;
	u32 m_stats_culled = 0;
};
