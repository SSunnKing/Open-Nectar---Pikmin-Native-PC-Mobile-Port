#pragma once

#include <cmath>

// Interpolation of two affine 3x4 matrices (GameCube Mtx layout: m[row][col],
// translation in column 3) as separate translation / rotation / scale:
// translation and scale linearly, rotation by quaternion slerp along the
// shortest arc.  Blending the raw matrix elements instead shrinks and shears
// a rotating object, and a 359 -> 1 degree step would turn the long way round.
//
// Returns false when a matrix cannot be split that way (zero scale, mirrored
// on only one side, or strongly sheared); out is then left untouched and the
// caller should use one of the two samples as is.
namespace pc_mtx_interp_detail {

struct Quat {
	float x, y, z, w;
};

inline Quat quatFromRotation(const float r[3][3])
{
	Quat q;
	const float trace = r[0][0] + r[1][1] + r[2][2];
	if (trace > 0.0f) {
		const float s = std::sqrt(trace + 1.0f) * 2.0f;
		q.w           = 0.25f * s;
		q.x           = (r[2][1] - r[1][2]) / s;
		q.y           = (r[0][2] - r[2][0]) / s;
		q.z           = (r[1][0] - r[0][1]) / s;
	} else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
		const float s = std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]) * 2.0f;
		q.w           = (r[2][1] - r[1][2]) / s;
		q.x           = 0.25f * s;
		q.y           = (r[0][1] + r[1][0]) / s;
		q.z           = (r[0][2] + r[2][0]) / s;
	} else if (r[1][1] > r[2][2]) {
		const float s = std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]) * 2.0f;
		q.w           = (r[0][2] - r[2][0]) / s;
		q.x           = (r[0][1] + r[1][0]) / s;
		q.y           = 0.25f * s;
		q.z           = (r[1][2] + r[2][1]) / s;
	} else {
		const float s = std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]) * 2.0f;
		q.w           = (r[1][0] - r[0][1]) / s;
		q.x           = (r[0][2] + r[2][0]) / s;
		q.y           = (r[1][2] + r[2][1]) / s;
		q.z           = 0.25f * s;
	}
	return q;
}

inline void rotationFromQuat(const Quat& q, float r[3][3])
{
	const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
	const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
	const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
	r[0][0] = 1.0f - 2.0f * (yy + zz);
	r[0][1] = 2.0f * (xy - wz);
	r[0][2] = 2.0f * (xz + wy);
	r[1][0] = 2.0f * (xy + wz);
	r[1][1] = 1.0f - 2.0f * (xx + zz);
	r[1][2] = 2.0f * (yz - wx);
	r[2][0] = 2.0f * (xz - wy);
	r[2][1] = 2.0f * (yz + wx);
	r[2][2] = 1.0f - 2.0f * (xx + yy);
}

inline Quat slerp(Quat a, const Quat& b, float t)
{
	float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
	if (d < 0.0f) { // shortest arc
		a.x = -a.x;
		a.y = -a.y;
		a.z = -a.z;
		a.w = -a.w;
		d   = -d;
	}
	float wa, wb;
	if (d > 0.9995f) {
		wa = 1.0f - t;
		wb = t;
	} else {
		const float th = std::acos(d);
		const float s  = std::sin(th);
		wa             = std::sin((1.0f - t) * th) / s;
		wb             = std::sin(t * th) / s;
	}
	Quat q = { a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb };
	const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	q.x /= n;
	q.y /= n;
	q.z /= n;
	q.w /= n;
	return q;
}

// m = R * diag(s) (+ translation). Fails on zero scale or noticeable shear.
inline bool decompose(const float m[3][4], float r[3][3], float s[3])
{
	for (int c = 0; c < 3; c++) {
		s[c] = std::sqrt(m[0][c] * m[0][c] + m[1][c] * m[1][c] + m[2][c] * m[2][c]);
		if (!(s[c] > 1e-6f)) {
			return false;
		}
		for (int row = 0; row < 3; row++) {
			r[row][c] = m[row][c] / s[c];
		}
	}
	for (int a = 0; a < 3; a++) {
		for (int b = a + 1; b < 3; b++) {
			const float dot = r[0][a] * r[0][b] + r[1][a] * r[1][b] + r[2][a] * r[2][b];
			if (std::fabs(dot) > 1e-2f) {
				return false;
			}
		}
	}
	const float det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0])
	                + r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
	if (det < 0.0f) { // mirrored: keep R a rotation, carry the sign in the scale
		s[0] = -s[0];
		for (int row = 0; row < 3; row++) {
			r[row][0] = -r[row][0];
		}
	}
	return true;
}

} // namespace pc_mtx_interp_detail

inline bool pc_mtx34_interp(const float a[3][4], const float b[3][4], float t, float out[3][4])
{
	using namespace pc_mtx_interp_detail;
	float ra[3][3], rb[3][3], sa[3], sb[3];
	if (!decompose(a, ra, sa) || !decompose(b, rb, sb)) {
		return false;
	}
	if ((sa[0] < 0.0f) != (sb[0] < 0.0f)) {
		return false;
	}
	float r[3][3];
	rotationFromQuat(slerp(quatFromRotation(ra), quatFromRotation(rb), t), r);
	for (int c = 0; c < 3; c++) {
		const float sc = sa[c] + (sb[c] - sa[c]) * t;
		for (int row = 0; row < 3; row++) {
			out[row][c] = r[row][c] * sc;
		}
	}
	for (int row = 0; row < 3; row++) {
		out[row][3] = a[row][3] + (b[row][3] - a[row][3]) * t;
	}
	return true;
}

// Rotation angle (radians) between the rotation parts of two matrices; -1 if
// either cannot be decomposed.  Used to tell a camera cut from a camera move.
inline float pc_mtx34_rotation_delta(const float a[3][4], const float b[3][4])
{
	using namespace pc_mtx_interp_detail;
	float ra[3][3], rb[3][3], sa[3], sb[3];
	if (!decompose(a, ra, sa) || !decompose(b, rb, sb)) {
		return -1.0f;
	}
	const pc_mtx_interp_detail::Quat qa = quatFromRotation(ra), qb = quatFromRotation(rb);
	float d = std::fabs(qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w);
	if (d > 1.0f) {
		d = 1.0f;
	}
	return 2.0f * std::acos(d);
}
