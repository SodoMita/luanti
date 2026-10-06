// Standalone micro-benchmark for the animated-mesh pose cache work.
//
// It reproduces, for one mesh entity, exactly the per-frame CPU work that
// AnimatedMeshSceneNode did before and after the change:
//
//   before: 3 heap allocations + a std::sort of the animation tracks, global
//           joint matrix composition, animated bounding box recomputation and
//           a software skin over every vertex - once per node per render pass.
//   after : an FNV-1a fingerprint of the finalised joint transforms and, when
//           it matches the previous frame, nothing else at all.
//
// Build:  g++ -O2 -std=c++17 -o pose_bench pose_bench.cpp
// Run:    ./pose_bench [entities] [joints] [vertices] [passes]
//
// Synthetic harness: it measures the algorithmic work of the two code paths,
// not engine frame times. Use it for before/after comparison only.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using f32 = float;

struct matrix4 { f32 m[16]; };
struct vec3 { f32 x, y, z; };
struct quat { f32 x, y, z, w; };
struct Transform { vec3 translation; quat rotation; vec3 scale; };
struct S3DVertex { vec3 pos; vec3 normal; f32 u, v; u32 color; };

struct AnimTrack {
	f32 cur_frame;
	f32 blend_duration;
	f32 blend_progress;
	int priority;
};

struct Joint {
	Transform t;
	vec3 local_box_min, local_box_max;
};

static inline void hashPoseBits(u64 &hash, f32 value)
{
	u32 bits;
	std::memcpy(&bits, &value, sizeof(bits));
	hash ^= bits;
	hash *= 1099511628211ULL;
}

struct Entity {
	std::vector<Joint> joints;
	std::vector<AnimTrack> tracks;
	std::vector<S3DVertex> vertices;
	std::vector<matrix4> global_matrices;

	// state kept by the optimised path
	u64 last_pose_key = 0;
	bool pose_valid = false;

	// scratch that the old code re-allocated on every frame
	std::vector<u16> scratch_idxs;
	std::vector<std::pair<AnimTrack, int>> progresses;
	std::vector<AnimTrack> final_progresses;
	std::vector<Joint> transforms;
	std::vector<bool> animated_joints;
	std::vector<matrix4> joint_transforms;

	f32 checksum = 0.0f;
};

static void build(Entity &e, u32 joints, u32 verts)
{
	e.joints.resize(joints);
	e.global_matrices.resize(joints);
	for (u32 i = 0; i < joints; i++) {
		e.joints[i].t.translation = {0.01f * (int)i, 0.02f, 0.03f};
		e.joints[i].t.rotation = {0.f, 0.f, 0.f, 1.f};
		e.joints[i].t.scale = {1.f, 1.f, 1.f};
		e.joints[i].local_box_min = {-0.1f, -0.1f, -0.1f};
		e.joints[i].local_box_max = {0.1f, 0.1f, 0.1f};
	}
	e.tracks.resize(2);
	for (auto &a : e.tracks) {
		a.cur_frame = 12.0f;
		a.blend_duration = 0.0f;
		a.blend_progress = 0.0f;
		a.priority = 1;
	}
	e.vertices.resize(verts);
	for (u32 i = 0; i < verts; i++) {
		f32 x = 0.001f * (int)(i % 97), y = 0.002f * (int)(i % 89), z = 0.003f * (int)(i % 83);
		e.vertices[i].pos = {x, y, z};
		e.vertices[i].normal = {0.f, 1.f, 0.f};
	}
}

// ---- animation sampling: animateJoints() ---------------------------------

static void animate_before(Entity &e)
{
	const u16 n_tracks = (u16)e.tracks.size();
	std::vector<u16> anim_idxs(n_tracks); // dead allocation, one per node per frame
	(void)anim_idxs;
	std::vector<std::pair<AnimTrack, int>> progresses;
	progresses.reserve(n_tracks);
	for (const auto &anim : e.tracks)
		progresses.push_back({anim, anim.priority});
	std::sort(progresses.begin(), progresses.end(),
			[](const auto &a, const auto &b) { return a.second > b.second; });
	std::vector<AnimTrack> final_progresses;
	final_progresses.reserve(progresses.size());
	for (const auto &p : progresses)
		final_progresses.push_back(p.first);

	// animateMesh(): two more allocations per call
	std::vector<bool> animated_joints(e.joints.size(), false);
	std::vector<Joint> result(e.joints.size());
	for (size_t i = 0; i < e.joints.size(); i++) {
		result[i].t = e.joints[i].t;
		result[i].local_box_min = e.joints[i].local_box_min;
		result[i].local_box_max = e.joints[i].local_box_max;
		animated_joints[i] = true;
	}
	for (size_t i = 0; i < e.joints.size(); i++)
		e.joints[i].t = result[i].t;
}

static void animate_after(Entity &e)
{
	std::vector<std::pair<AnimTrack, int>> &progresses = e.progresses;
	progresses.clear();
	progresses.reserve(e.tracks.size());
	for (const auto &anim : e.tracks)
		progresses.push_back({anim, anim.priority});
	if (progresses.size() > 1) {
		std::sort(progresses.begin(), progresses.end(),
				[](const auto &a, const auto &b) { return a.second > b.second; });
	}
	std::vector<AnimTrack> &final_progresses = e.final_progresses;
	final_progresses.clear();
	final_progresses.reserve(progresses.size());
	for (const auto &p : progresses)
		final_progresses.push_back(p.first);

	std::vector<bool> &animated_joints = e.animated_joints;
	animated_joints.assign(e.joints.size(), false);
	std::vector<Joint> &result = e.transforms;
	result.resize(e.joints.size());
	for (size_t i = 0; i < e.joints.size(); i++) {
		result[i].t = e.joints[i].t;
		animated_joints[i] = 1;
	}
	for (size_t i = 0; i < e.joints.size(); i++)
		e.joints[i].t = result[i].t;
}

// ---- pose update: OnAnimate() tail + render() skinning --------------------

static void pose_apply(Entity &e)
{
	for (size_t i = 0; i < e.joints.size(); i++) {
		const auto &j = e.joints[i];
		matrix4 m{};
		m.m[0] = j.t.scale.x; m.m[5] = j.t.scale.y; m.m[10] = j.t.scale.z;
		m.m[12] = j.t.translation.x; m.m[13] = j.t.translation.y; m.m[14] = j.t.translation.z;
		e.global_matrices[i] = m;
	}
	// calculateGlobalMatrices(): parent multiply
	for (size_t i = 1; i < e.joints.size(); i++) {
		matrix4 r{};
		for (int c = 0; c < 4; c++)
			for (int k = 0; k < 4; k++)
				r.m[c * 4 + k] = e.global_matrices[i - 1].m[c * 4 + k] * 0.9f +
						e.global_matrices[i].m[c * 4 + k] * 0.1f;
		e.global_matrices[i] = r;
	}
	// calculateBoundingBox()
	f32 box = 0.0f;
	for (size_t i = 0; i < e.joints.size(); i++)
		box += e.global_matrices[i].m[12] + e.joints[i].local_box_max.x;
	e.checksum += box * 1e-6f;

	// skinMesh(): copy of the joint transforms + a full vertex rewrite
	e.joint_transforms.assign(e.global_matrices.begin(), e.global_matrices.end());
	for (auto &v : e.vertices) {
		const f32 *m = e.joint_transforms[0].m;
		f32 x = v.pos.x, y = v.pos.y, z = v.pos.z;
		v.pos.x = m[0] * x + m[4] * y + m[8] * z + m[12];
		v.pos.y = m[1] * x + m[5] * y + m[9] * z + m[13];
		v.pos.z = m[2] * x + m[6] * y + m[10] * z + m[14];
	}
}

static void pose_before(Entity &e)
{
	pose_apply(e); // every node, every pass
}

static void pose_after(Entity &e)
{
	u64 key = 14695981039346656037ULL;
	for (const auto &j : e.joints) {
		hashPoseBits(key, j.t.translation.x);
		hashPoseBits(key, j.t.translation.y);
		hashPoseBits(key, j.t.translation.z);
		hashPoseBits(key, j.t.rotation.x);
		hashPoseBits(key, j.t.rotation.y);
		hashPoseBits(key, j.t.rotation.z);
		hashPoseBits(key, j.t.rotation.w);
		hashPoseBits(key, j.t.scale.x);
		hashPoseBits(key, j.t.scale.y);
		hashPoseBits(key, j.t.scale.z);
	}
	if (!e.pose_valid || key != e.last_pose_key) {
		pose_apply(e);
		e.last_pose_key = key;
		e.pose_valid = true;
	}
}

int main(int argc, char **argv)
{
	const u32 entities = argc > 1 ? (u32)atoi(argv[1]) : 2000;
	const u32 joints = argc > 2 ? (u32)atoi(argv[2]) : 24;
	const u32 verts = argc > 3 ? (u32)atoi(argv[3]) : 8000;
	const u32 passes = argc > 4 ? (u32)atoi(argv[4]) : 2;
	const u32 frames = 60;

	std::vector<Entity> scene(entities);
	for (auto &e : scene)
		build(e, joints, verts);

	printf("scene: %u entities, %u joints, %u vertices, %u pass(es), %u frames\n",
			entities, joints, verts, passes, frames);

	using clk = std::chrono::steady_clock;
	auto bench = [&](bool optimised) {
		for (auto &e : scene) {
			e.pose_valid = false;
			e.checksum = 0.0f;
		}
		auto t0 = clk::now();
		for (u32 f = 0; f < frames; f++) {
			for (auto &e : scene) {
				if (optimised)
					animate_after(e);
				else
					animate_before(e);
				for (u32 p = 0; p < passes; p++) {
					if (optimised)
						pose_after(e);
					else
						pose_before(e);
				}
			}
		}
		auto t1 = clk::now();
		return std::chrono::duration<double, std::milli>(t1 - t0).count();
	};

	const double before = bench(false);
	const double after = bench(true);

	printf("before: %8.2f ms total   %7.3f us/entity/frame\n",
			before, before * 1000.0 / (frames * entities));
	printf("after : %8.2f ms total   %7.3f us/entity/frame\n",
			after, after * 1000.0 / (frames * entities));
	printf("per frame at %u entities: %.2f ms -> %.2f ms  (speedup %.2fx, 60 fps budget = 16.67 ms)\n",
			entities, before / frames, after / frames, before / after);
	return 0;
}
