#ifndef PC_VR_MATH_H
#define PC_VR_MATH_H

// The few vector, quaternion and pose operations VR needs, free of both the
// game's math headers and OpenXR's types so the rig can be tested on its own.
//
// Conventions match OpenXR and the game alike: right-handed, +Y up, a view
// looks down -Z. A Pose maps local to parent: x' = rotate(q, x) + p.

#include <cmath>

namespace pcvr {

struct Vec3 {
	float x = 0.0f, y = 0.0f, z = 0.0f;
	Vec3() = default;
	Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) { }
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3 operator*(Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
inline Vec3 operator*(float s, Vec3 a) { return a * s; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a)
{
	const float len = length(a);
	return len > 1e-6f ? a * (1.0f / len) : Vec3 { 0.0f, 0.0f, 0.0f };
}

struct Quat {
	float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
	Quat() = default;
	Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) { }
};

inline Quat operator*(Quat a, Quat b)
{
	return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
		     a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}
inline Quat conjugate(Quat q) { return { -q.x, -q.y, -q.z, q.w }; }
inline Quat normalize(Quat q)
{
	const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	return len > 1e-6f ? Quat { q.x / len, q.y / len, q.z / len, q.w / len } : Quat {};
}

inline Vec3 rotate(Quat q, Vec3 v)
{
	const Vec3 u { q.x, q.y, q.z };
	const Vec3 t = 2.0f * cross(u, v);
	return v + q.w * t + cross(u, t);
}

/// Rotation of `radians` about +Y. Positive turns -Z (forward) towards -X, i.e.
/// to the left, as OpenXR's and the game's right-handed axes both have it.
inline Quat yawQuat(float radians) { return { 0.0f, std::sin(radians * 0.5f), 0.0f, std::cos(radians * 0.5f) }; }

/// Heading of a direction on the ground plane, in yawQuat's convention: the
/// yaw that turns -Z onto the direction's horizontal part.
inline float yawOfDirection(Vec3 d) { return std::atan2(-d.x, -d.z); }
inline float yawOf(Quat q) { return yawOfDirection(rotate(q, { 0.0f, 0.0f, -1.0f })); }

inline Vec3 forwardOnGround(float yaw) { return { -std::sin(yaw), 0.0f, -std::cos(yaw) }; }

/// Wraps an angle to (-pi, pi].
inline float wrapAngle(float a)
{
	const float twoPi = 6.28318530718f;
	a = std::fmod(a + 3.14159265359f, twoPi);
	if (a < 0.0f) a += twoPi;
	return a - 3.14159265359f;
}

struct Pose {
	Quat q;
	Vec3 p;
};

inline Vec3 transform(const Pose& a, Vec3 v) { return rotate(a.q, v) + a.p; }
/// a ∘ b: first b, then a.
inline Pose compose(const Pose& a, const Pose& b) { return { normalize(a.q * b.q), rotate(a.q, b.p) + a.p }; }
inline Pose inverse(const Pose& a)
{
	const Quat qi = conjugate(a.q);
	return { qi, rotate(qi, a.p * -1.0f) };
}

/// A 4x4 matrix in OpenGL's column-major layout: m[column * 4 + row].
struct Mat4 {
	float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	float& at(int row, int column) { return m[column * 4 + row]; }
	float at(int row, int column) const { return m[column * 4 + row]; }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b)
{
	Mat4 r;
	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			float sum = 0.0f;
			for (int k = 0; k < 4; ++k) sum += a.at(row, k) * b.at(k, column);
			r.at(row, column) = sum;
		}
	}
	return r;
}

inline Mat4 matrixFromPose(const Pose& pose)
{
	Mat4 r;
	const Vec3 xAxis = rotate(pose.q, { 1.0f, 0.0f, 0.0f });
	const Vec3 yAxis = rotate(pose.q, { 0.0f, 1.0f, 0.0f });
	const Vec3 zAxis = rotate(pose.q, { 0.0f, 0.0f, 1.0f });
	const Vec3 axes[3] = { xAxis, yAxis, zAxis };
	for (int column = 0; column < 3; ++column) {
		r.at(0, column) = axes[column].x;
		r.at(1, column) = axes[column].y;
		r.at(2, column) = axes[column].z;
	}
	r.at(0, 3) = pose.p.x;
	r.at(1, 3) = pose.p.y;
	r.at(2, 3) = pose.p.z;
	return r;
}

/// C_MTXFrustum's GameCube form from OpenXR-style tangents (left and down
/// negative). Near lands on NDC -1 and far on NDC 0, which is what the port's
/// depth test and fog reconstruction (pc_tev_shader.cpp) are built around; an
/// OpenGL frustum here would fog the whole world.
inline Mat4 gxFrustum(float tanLeft, float tanRight, float tanUp, float tanDown, float nearZ, float farZ)
{
	Mat4 r;
	r.at(0, 0) = 2.0f / (tanRight - tanLeft);
	r.at(0, 2) = (tanRight + tanLeft) / (tanRight - tanLeft);
	r.at(1, 1) = 2.0f / (tanUp - tanDown);
	r.at(1, 2) = (tanUp + tanDown) / (tanUp - tanDown);
	r.at(2, 2) = -nearZ / (farZ - nearZ);
	r.at(2, 3) = -(farZ * nearZ) / (farZ - nearZ);
	r.at(3, 2) = -1.0f;
	r.at(3, 3) = 0.0f;
	return r;
}

/// A GX view matrix (3 rows x 4 columns, world -> view) from a camera pose:
/// the rows are the camera's right, up and back axes, as Matrix4f::makeLookat
/// builds them.
inline void gxViewFromPose(const Pose& camera, float out[3][4])
{
	const Vec3 axes[3] = { rotate(camera.q, { 1.0f, 0.0f, 0.0f }), rotate(camera.q, { 0.0f, 1.0f, 0.0f }),
		                   rotate(camera.q, { 0.0f, 0.0f, 1.0f }) };
	for (int row = 0; row < 3; ++row) {
		out[row][0] = axes[row].x;
		out[row][1] = axes[row].y;
		out[row][2] = axes[row].z;
		out[row][3] = -dot(axes[row], camera.p);
	}
}

} // namespace pcvr

#endif // PC_VR_MATH_H
