// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "catch.h"
#include <SViewFrustum.h>
#include <aabbox3d.h>
#include <matrix4.h>

#include <vector>

namespace {

bool legacyFrustumCull(const scene::SViewFrustum &world_frustum,
		const core::aabbox3df &box, const core::matrix4 &transform)
{
	scene::SViewFrustum frustum = world_frustum;
	core::matrix4 inverse(transform, core::matrix4::EM4CONST_INVERSE);
	frustum.transform(inverse);

	core::vector3df edges[8];
	box.getEdges(edges);
	for (s32 i = 0; i < scene::SViewFrustum::VF_PLANE_COUNT; ++i) {
		bool inside = false;
		for (const auto &edge : edges) {
			if (frustum.planes[i].classifyPointRelation(edge) != core::ISREL3D_FRONT) {
				inside = true;
				break;
			}
		}
		if (!inside)
			return true;
	}
	return false;
}

bool worldSpaceFrustumCull(const scene::SViewFrustum &frustum,
		const core::aabbox3df &box, const core::matrix4 &transform)
{
	core::vector3df edges[8];
	box.getEdges(edges);
	for (auto &edge : edges)
		transform.transformVect(edge);

	for (s32 i = 0; i < scene::SViewFrustum::VF_PLANE_COUNT; ++i) {
		bool inside = false;
		for (const auto &edge : edges) {
			if (frustum.planes[i].classifyPointRelation(edge) != core::ISREL3D_FRONT) {
				inside = true;
				break;
			}
		}
		if (!inside)
			return true;
	}
	return false;
}

scene::SViewFrustum makeFrustum()
{
	scene::SViewFrustum frustum;
	frustum.planes[scene::SViewFrustum::VF_LEFT_PLANE].setPlane(
			{-100.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f});
	frustum.planes[scene::SViewFrustum::VF_RIGHT_PLANE].setPlane(
			{100.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
	frustum.planes[scene::SViewFrustum::VF_BOTTOM_PLANE].setPlane(
			{0.0f, -100.0f, 0.0f}, {0.0f, -1.0f, 0.0f});
	frustum.planes[scene::SViewFrustum::VF_TOP_PLANE].setPlane(
			{0.0f, 100.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
	frustum.planes[scene::SViewFrustum::VF_NEAR_PLANE].setPlane(
			{0.0f, 0.0f, -100.0f}, {0.0f, 0.0f, -1.0f});
	frustum.planes[scene::SViewFrustum::VF_FAR_PLANE].setPlane(
			{0.0f, 0.0f, 100.0f}, {0.0f, 0.0f, 1.0f});
	return frustum;
}

std::vector<core::matrix4> makeTransforms()
{
	std::vector<core::matrix4> transforms;
	transforms.reserve(2048);
	u32 state = 0x91e10da5;
	auto random = [&state] {
		state = state * 1664525u + 1013904223u;
		return static_cast<f32>(state >> 8) / static_cast<f32>(0x01000000);
	};

	for (u32 i = 0; i < 2048; ++i) {
		core::matrix4 rotation;
		rotation.setRotationDegrees({random() * 360.0f, random() * 360.0f,
				random() * 360.0f});
		core::matrix4 scale;
		scale.setScale({0.25f + random() * 3.0f, 0.25f + random() * 3.0f,
				0.25f + random() * 3.0f});
		rotation *= scale;
		rotation.setTranslation({-180.0f + random() * 360.0f,
				-180.0f + random() * 360.0f, -180.0f + random() * 360.0f});
		transforms.push_back(rotation);
	}
	return transforms;
}

} // namespace

TEST_CASE("benchmark_scene_frustum_culling")
{
	const scene::SViewFrustum frustum = makeFrustum();
	const core::aabbox3df box({-4.0f, -7.0f, -3.0f}, {4.0f, 7.0f, 3.0f});
	const std::vector<core::matrix4> transforms = makeTransforms();

	for (const auto &transform : transforms)
		REQUIRE(worldSpaceFrustumCull(frustum, box, transform) ==
				legacyFrustumCull(frustum, box, transform));

	BENCHMARK("scene_frustum_culling_legacy") {
		u32 culled = 0;
		for (const auto &transform : transforms)
			culled += legacyFrustumCull(frustum, box, transform);
		return culled;
	};

	BENCHMARK("scene_frustum_culling_world_space") {
		u32 culled = 0;
		for (const auto &transform : transforms)
			culled += worldSpaceFrustumCull(frustum, box, transform);
		return culled;
	};
}
