// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "catch.h"
#include "SkinnedMesh.h"

TEST_CASE("shared skinned mesh pose follows render order")
{
	scene::SkinnedMesh mesh(scene::SkinnedMesh::SourceFormat::OTHER);
	constexpr u64 pose_a = 0xA11CE;
	constexpr u64 pose_b = 0xB0B;

	// First use populates shared buffers; a second pass of the same node reuses them.
	CHECK(mesh.activateSkinnedPose(pose_a));
	CHECK_FALSE(mesh.activateSkinnedPose(pose_a));

	// All nodes animate before rendering. Alternating nodes must therefore check
	// shared state in render order on every frame, not cache this result per node.
	CHECK(mesh.activateSkinnedPose(pose_b));
	CHECK(mesh.activateSkinnedPose(pose_a));
	CHECK(mesh.activateSkinnedPose(pose_b));

	// Raw-matrix joints cannot be represented by the pose fingerprint.
	CHECK(mesh.activateSkinnedPose(pose_b, true));

	mesh.invalidateSkinnedPose();
	CHECK(mesh.activateSkinnedPose(pose_b));
}
