// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "catch.h"
#include "catch_amalgamated.hpp"
#include "irrMath.h"
#include "matrix4.h"
#include "SViewFrustum.h"
#include "irr_v3d.h"
#include <cmath>

using matrix4 = core::matrix4;
using SViewFrustum = scene::SViewFrustum;

/**
 * SViewFrustum::transform() hoists the inverse transpose of the matrix out of
 * the plane loop (CMatrix4::transformPlane() would recompute it once per
 * plane). The result must stay bit identical to the per-plane reference,
 * because CSceneManager::isCulled() classifies bounding box corners against
 * these planes and any drift would make entities pop in or out.
 */
static SViewFrustum reference_transform(const SViewFrustum &in, const matrix4 &m)
{
	SViewFrustum f = in;
	for (u32 i = 0; i < SViewFrustum::VF_PLANE_COUNT; ++i)
		m.transformPlane(f.planes[i]);
	m.transformVect(f.cameraPosition);
	f.recalculateBoundingBox();
	return f;
}

static matrix4 make_viewproj(f32 yaw, f32 pitch, f32 tx, f32 ty, f32 tz)
{
	matrix4 view, proj, vp;
	view.setRotationRadians({pitch, yaw, 0.0f});
	view.setTranslation({tx, ty, tz});
	proj.buildProjectionMatrixPerspectiveFovRH(core::PI / 2.5f, 4.0f / 3.0f, 0.1f, 600.0f, false);
	vp = proj * view;
	return vp;
}

TEST_CASE("SViewFrustum::transform matches the per-plane reference exactly")
{
	const matrix4 vps[] = {
		make_viewproj(0.0f, 0.0f, 0, 0, 0),
		make_viewproj(0.7f, -0.3f, 123.5f, -7.25f, 904.0f),
		make_viewproj(3.1f, 1.2f, -500.0f, 64.0f, -12.5f),
	};

	for (const auto &vp : vps) {
		SViewFrustum frustum;
		frustum.setFrom(vp, false);

		const matrix4 node_transforms[] = {
			matrix4(), // identity
			[] {
				matrix4 m;
				m.setRotationRadians({0.4f, 1.1f, -0.7f});
				m.setTranslation({30.0f, -12.0f, 88.0f});
				return m;
			} (),
			[] {
				matrix4 m;
				m.setScale({2.0f, 0.5f, 3.0f});
				m.setRotationRadians({-1.4f, 0.2f, 2.2f});
				m.setTranslation({-410.0f, 77.0f, -60.0f});
				return m;
			} (),
		};

		for (const auto &m : node_transforms) {
			SViewFrustum optimized = frustum;
			optimized.transform(m);

			const SViewFrustum reference = reference_transform(frustum, m);

			for (u32 i = 0; i < SViewFrustum::VF_PLANE_COUNT; ++i) {
				CHECK(optimized.planes[i].Normal.equals(reference.planes[i].Normal));
				CHECK(optimized.planes[i].D == reference.planes[i].D);
			}
			CHECK(optimized.cameraPosition.equals(reference.cameraPosition));
			CHECK(optimized.getBoundingBox().MinEdge.equals(reference.getBoundingBox().MinEdge));
			CHECK(optimized.getBoundingBox().MaxEdge.equals(reference.getBoundingBox().MaxEdge));
		}
	}
}

/**
 * The culling test in CSceneManager::isCulled() transforms the frustum into the
 * node's local space and classifies the local box corners. The equivalent
 * world-space formulation (transform the box corners, classify against the
 * untransformed planes) must agree, or one of the two is wrong. This pins the
 * relationship that a previous attempt at optimizing the culling got wrong.
 */
TEST_CASE("frustum-in-node-space and node-in-world-space culling agree")
{
	const matrix4 vp = make_viewproj(0.3f, 0.2f, 10.0f, -5.0f, 250.0f);
	SViewFrustum frustum;
	frustum.setFrom(vp, false);

	for (f32 angle = 0.0f; angle < 6.28f; angle += 0.37f) {
		for (f32 radius = 2.0f; radius < 550.0f; radius += 47.0f) {
			matrix4 m;
			m.setRotationRadians({angle * 0.5f, angle, 0.0f});
			m.setTranslation({std::sin(angle) * radius, std::cos(angle * 2.0f) * 3.0f,
					std::cos(angle) * radius});

			const core::aabbox3d<f32> box{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};

			// formulation A: frustum transformed into node space
			SViewFrustum f = frustum;
			matrix4 inv(m, matrix4::EM4CONST_INVERSE);
			f.transform(inv);
			core::vector3df edges[8];
			box.getEdges(edges);
			bool culled_a = false;
			for (u32 i = 0; i < SViewFrustum::VF_PLANE_COUNT && !culled_a; ++i) {
				bool all_front = true;
				for (auto &e : edges) {
					if (f.planes[i].classifyPointRelation(e) != core::ISREL3D_FRONT) {
						all_front = false;
						break;
					}
				}
				culled_a = all_front;
			}

			// formulation B: the same corners transformed into world space and
			// classified against the untransformed camera planes
			core::vector3df world_edges[8];
			box.getEdges(world_edges);
			for (auto &e : world_edges)
				m.transformVect(e);
			bool culled_b = false;
			for (u32 i = 0; i < SViewFrustum::VF_PLANE_COUNT && !culled_b; ++i) {
				bool all_front = true;
				for (auto &e : world_edges) {
					if (frustum.planes[i].classifyPointRelation(e) != core::ISREL3D_FRONT) {
						all_front = false;
						break;
					}
				}
				culled_b = all_front;
			}

			INFO("angle=" << angle << " radius=" << radius);
			CHECK(culled_a == culled_b);
		}
	}
}
