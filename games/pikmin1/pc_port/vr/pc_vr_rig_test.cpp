// The VR rig and its matrices, pinned down without a headset or a stage.
//
// A sign error in any of these shows up in the headset as a world that swims
// against head motion, eyes that are swapped, or a table that turns the wrong
// way -- all of which feel like something is subtly wrong long before anyone
// can say what. Each convention is checked here instead.
#include "pc_vr_rig.h"

#include <cmath>
#include <cstdio>

using namespace pcvr;

namespace {
int failures = 0;

void check(bool ok, const char* what)
{
	if (!ok) {
		std::printf("FAIL: %s\n", what);
		failures++;
	}
}

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }
bool near(Vec3 a, Vec3 b, float eps = 1e-2f) { return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps); }

const float kPi = 3.14159265358979323846f;

Vec3 transformPoint(const Mat4& m, Vec3 v, float* wOut = nullptr)
{
	const float x = m.at(0, 0) * v.x + m.at(0, 1) * v.y + m.at(0, 2) * v.z + m.at(0, 3);
	const float y = m.at(1, 0) * v.x + m.at(1, 1) * v.y + m.at(1, 2) * v.z + m.at(1, 3);
	const float z = m.at(2, 0) * v.x + m.at(2, 1) * v.y + m.at(2, 2) * v.z + m.at(2, 3);
	const float w = m.at(3, 0) * v.x + m.at(3, 1) * v.y + m.at(3, 2) * v.z + m.at(3, 3);
	if (wOut) *wOut = w;
	return { x, y, z };
}

Vec3 ndc(const Mat4& m, Vec3 v)
{
	float w       = 1.0f;
	const Vec3 c  = transformPoint(m, v, &w);
	return { c.x / w, c.y / w, c.z / w };
}

TrackedFrame headAt(Vec3 position, float yaw)
{
	TrackedFrame t;
	t.valid  = true;
	t.head.q = yawQuat(yaw);
	t.head.p = position;
	return t;
}
} // namespace

int main()
{
	// ── Yaw convention ───────────────────────────────────────────────────────
	check(near(forwardOnGround(0.0f), Vec3 { 0, 0, -1 }), "yaw 0 faces -Z");
	check(near(forwardOnGround(kPi * 0.5f), Vec3 { -1, 0, 0 }), "positive yaw turns left, to -X");
	for (int i = 0; i < 12; i++) {
		const float yaw = -3.0f + 0.5f * i;
		check(near(wrapAngle(yawOf(yawQuat(yaw)) - yaw), 0.0f), "yawOf inverts yawQuat");
	}

	// ── Poses ────────────────────────────────────────────────────────────────
	{
		const Pose a { yawQuat(0.7f), { 1, 2, 3 } };
		const Pose b { normalize(Quat { 0.2f, 0.1f, -0.3f, 0.9f }), { -4, 5, 0.5f } };
		const Vec3 v { 0.3f, -0.8f, 2.0f };
		check(near(transform(compose(a, b), v), transform(a, transform(b, v))), "compose applies right then left");
		check(near(transform(inverse(a), transform(a, v)), v), "inverse undoes a pose");
	}

	// ── The GameCube frustum ─────────────────────────────────────────────────
	{
		const float n = 10.0f, f = 6000.0f;
		// Asymmetric, as a real headset lens is.
		const float tl = -1.2f, tr = 0.9f, tu = 1.0f, td = -1.1f;
		const Mat4 p = gxFrustum(tl, tr, tu, td, n, f);
		check(near(ndc(p, { 0, 0, -n }).z, -1.0f), "near plane lands on NDC -1");
		check(near(ndc(p, { 0, 0, -f }).z, 0.0f), "far plane lands on NDC 0");
		check(near(ndc(p, { tl * 50.0f, 0, -50.0f }).x, -1.0f), "left tangent is the left edge");
		check(near(ndc(p, { tr * 50.0f, 0, -50.0f }).x, 1.0f), "right tangent is the right edge");
		check(near(ndc(p, { 0, tu * 50.0f, -50.0f }).y, 1.0f), "up tangent is the top edge");
		check(near(ndc(p, { 0, td * 50.0f, -50.0f }).y, -1.0f), "down tangent is the bottom edge");
	}

	// ── The eye offset ───────────────────────────────────────────────────────
	{
		// Left eye 32 mm to the left of the head, at 100 units per metre.
		Pose head { yawQuat(0.4f), { 0.1f, 1.6f, 0.2f } };
		Pose eye  = compose(head, Pose { Quat {}, { -0.032f, 0.0f, 0.0f } });
		Mat4 rel  = eyeFromHead(head, eye, 100.0f);
		// Something straight ahead of the head is 3.2 units to the right of
		// the left eye.
		check(near(transformPoint(rel, { 0, 0, -100 }), Vec3 { 3.2f, 0, -100 }), "left eye sees ahead-of-head shifted right");
	}

	// ── GX view from a pose ──────────────────────────────────────────────────
	{
		const Pose camera { yawQuat(1.1f), { 50, 20, -30 } };
		float view[3][4];
		gxViewFromPose(camera, view);
		const Vec3 ahead = transform(camera, { 0, 0, -10 });
		const float vx   = view[0][0] * ahead.x + view[0][1] * ahead.y + view[0][2] * ahead.z + view[0][3];
		const float vy   = view[1][0] * ahead.x + view[1][1] * ahead.y + view[1][2] * ahead.z + view[1][3];
		const float vz   = view[2][0] * ahead.x + view[2][1] * ahead.y + view[2][2] * ahead.z + view[2][3];
		check(near(vx, 0.0f) && near(vy, 0.0f) && near(vz, -10.0f), "view matrix puts the camera's forward on -Z");
	}

	// ── Third-person ─────────────────────────────────────────────────────────
	{
		Rig rig;
		SceneState scene;
		scene.focus         = { 1000, 50, 2000 };
		scene.cameraForward = { 0, -0.5f, -1 };
		const TrackedFrame t = headAt({ 0, 1.6f, 0 }, 0.0f);
		rig.update(t, scene, 1.0f / 72.0f);
		check(rig.ready(), "third-person ready after one tracked frame");

		const RigSettings& s = rig.settings;
		const Vec3 expectedHead { 1000, 50 + s.thirdPersonHeight, 2000 + s.thirdPersonDistance };
		check(near(rig.headInWorld(t.head).p, expectedHead), "recentred head sits behind and above the captain");
		check(near(rotate(rig.headInWorld(t.head).q, { 0, 0, -1 }), Vec3 { 0, 0, -1 }), "and faces the way the game camera did");
		check(near(rig.trackingToWorld({ 1, 1.6f, 0 }), expectedHead + Vec3 { s.thirdPersonScale, 0, 0 }),
		      "a step right moves right in the world, at the rig's scale");

		rig.snapTurn(+1);
		rig.update(t, scene, 1.0f / 72.0f);
		const float step    = -s.snapTurnDegrees * kPi / 180.0f;
		const Vec3 turned   = scene.focus + rotate(yawQuat(step), Vec3 { 0, s.thirdPersonHeight, s.thirdPersonDistance });
		check(near(rig.headInWorld(t.head).p, turned), "a snap turn swings the view round the captain, at once");
		check(near(wrapAngle(yawOf(rig.headInWorld(t.head).q) - step), 0.0f), "and turns the view to the right");

		// Captain walks: the view follows, but smoothly.
		scene.focus = scene.focus + Vec3 { 100, 0, 0 };
		rig.update(t, scene, 1.0f / 72.0f);
		const float movedX = rig.headInWorld(t.head).p.x - turned.x;
		check(movedX > 0.0f && movedX < 100.0f, "following the captain is smoothed");

		// Cutscene: the head goes where the director's camera is.
		SceneState cut   = scene;
		cut.cutscene     = true;
		cut.cameraPos    = { -500, 300, 800 };
		cut.cameraForward = { 1, 0, 0 };
		rig.update(t, cut, 1.0f / 72.0f);
		check(near(rig.headInWorld(t.head).p, cut.cameraPos), "cutscene puts the head at the camera");
		check(near(rotate(rig.headInWorld(t.head).q, { 0, 0, -1 }), Vec3 { 1, 0, 0 }), "facing where it looks");
	}

	// ── Tabletop ─────────────────────────────────────────────────────────────
	{
		Rig rig;
		rig.settings.mode = PC_VR_RIG_TABLETOP;
		SceneState scene;
		scene.focus         = { -200, 10, 300 };
		scene.cameraForward = { 0, 0, -1 };
		const TrackedFrame t = headAt({ 0, 1.6f, 0 }, 0.0f);
		rig.update(t, scene, 1.0f / 72.0f);

		const RigSettings& s = rig.settings;
		const Vec3 table { 0, 1.6f - s.tabletopDrop, -s.tabletopDistance };
		check(near(rig.anchorTracking(), table), "table appears in front of and below the head");
		check(near(rig.trackingToWorld(table), scene.focus), "captain stands at the table's centre");
		check(near(rig.trackingToWorld(table + Vec3 { 0, 0, -0.1f }), scene.focus + Vec3 { 0, 0, -0.1f * s.tabletopScale }),
		      "10 cm across the table is the scale's worth of world");
		check(rig.nearPlane() >= 1.0f && rig.farPlane() > rig.nearPlane() * 100.0f, "planes are sane at tabletop scale");
		check(near(rig.fogOffset(t.head), length(t.head.p - table) * s.tabletopScale, 0.5f), "fog is pushed out by the head's distance");

		rig.rotateTable(kPi * 0.5f);
		rig.update(t, scene, 1.0f / 72.0f);
		check(near(rig.trackingToWorld(table), scene.focus), "turning the table keeps the captain at its centre");
		check(near(rig.trackingDirectionToWorld({ 0, 0, -1 }), Vec3 { -1, 0, 0 }), "and spins the world with it");

		rig.dragTable({ 0.2f, 0.0f, 0.0f });
		rig.update(t, scene, 1.0f / 72.0f);
		check(near(rig.trackingToWorld(table + Vec3 { 0.2f, 0, 0 }), scene.focus), "dragging moves the table");
	}

	if (failures == 0) std::printf("pc_vr_rig_test: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
