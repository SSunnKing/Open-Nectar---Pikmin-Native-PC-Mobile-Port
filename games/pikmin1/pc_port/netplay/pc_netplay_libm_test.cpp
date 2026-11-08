// Host-run accuracy + determinism test for the netplay software libm.
// Engine-free: links only pc_netplay_libm.c.
//
// What it does:
//  1. Proves the override links: pc_netplay_libm_present() must be 1, so
//     every standard-named call below reaches OUR definitions (the same
//     link mechanics as the netplay game exe).
//  2. ULP report: each float entry point is evaluated on a dense
//     deterministic grid over its game domain plus edge cases, against
//     the platform libm. The reference is loaded explicitly (msvcrt.dll
//     on Windows, libm.so on Linux, libSystem.B.dylib on macOS) so it is never
//     shadowed by our strong definitions. mingw float funcs are (float) of a
//     correctly-rounded double (verified: 0 ulp vs (float)msvcrt-double on 400k grids), so
//     the msvcrt reference is a valid proxy for "the mingw libm".
//     Game-domain bound: max <= 1 ulp. Huge trig args
//     (|x| > 2.8e16, i.e. past the double-double-2pi range) are checksum-only.
//  3. Golden checksum: FNV-1a/64 over every OUR result bit. The constant
//     below is committed; any machine computing a different checksum is
//     non-deterministic and the test fails.
//
// Exit 0 on pass, 1 with a diagnostic on any failure. No game, no assets.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>

#include "netplay/pc_netplay_libm.h"

#ifdef __APPLE__
// Apple's <math.h> has no sincosf (it has __sincosf_stret). The software libm
// under test defines the GNU-style sincosf, so declare it here to keep calling
// our definition directly.
extern "C" void sincosf(float x, float *s, float *c);
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

typedef double (*Df1)(double);
typedef double (*Df2)(double, double);

struct Ref {
	Df1 sin = nullptr, cos = nullptr, tan = nullptr, asin = nullptr, acos = nullptr, atan = nullptr,
	    exp = nullptr, log = nullptr, log10 = nullptr, sqrt = nullptr;
	Df2 atan2 = nullptr, pow = nullptr, fmod = nullptr, hypot = nullptr;
	bool ok = false;
};

Ref loadRef()
{
	Ref r;
#ifdef _WIN32
	HMODULE m = LoadLibraryA("msvcrt.dll");
	if (!m)
		return r;
	auto sym = [&](const char *name) -> void * { return reinterpret_cast<void *>(GetProcAddress(m, name)); };
	auto get1 = [&](const char *name, Df1 &out) {
		void *p = sym(name);
		memcpy(&out, &p, sizeof(p));
	};
	auto get2 = [&](const char *name, Df2 &out) {
		void *p = sym(name);
		memcpy(&out, &p, sizeof(p));
	};
	get1("sin", r.sin);
	get1("cos", r.cos);
	get1("tan", r.tan);
	get1("asin", r.asin);
	get1("acos", r.acos);
	get1("atan", r.atan);
	get1("exp", r.exp);
	get1("log", r.log);
	get1("log10", r.log10);
	get1("sqrt", r.sqrt);
	get2("atan2", r.atan2);
	get2("pow", r.pow);
	get2("fmod", r.fmod);
	get2("_hypot", r.hypot);
	r.ok = r.sin && r.cos && r.tan && r.asin && r.acos && r.atan && r.exp && r.log && r.log10
	    && r.sqrt && r.atan2 && r.pow && r.fmod && r.hypot;
#else
	auto loadFrom = [&](void *m) {
		if (!m)
			return;
#define LOAD1(f) r.f = reinterpret_cast<Df1>(dlsym(m, #f))
#define LOAD2(f) r.f = reinterpret_cast<Df2>(dlsym(m, #f))
		LOAD1(sin);
		LOAD1(cos);
		LOAD1(tan);
		LOAD1(asin);
		LOAD1(acos);
		LOAD1(atan);
		LOAD1(exp);
		LOAD1(log);
		LOAD1(log10);
		LOAD1(sqrt);
		LOAD2(atan2);
		LOAD2(pow);
		LOAD2(fmod);
		LOAD2(hypot);
#undef LOAD1
#undef LOAD2
	};
	void *m = nullptr;
#if defined(__APPLE__)
	// Math lives in libSystem, not a separate libm.so.
	m = dlopen("/usr/lib/libSystem.B.dylib", RTLD_NOW);
	loadFrom(m);
#else
	m = dlopen("libm.so.6", RTLD_NOW);
	if (!m)
		m = dlopen("libm.so", RTLD_NOW);
	loadFrom(m);
#endif
	r.ok = r.sin && r.cos && r.tan && r.asin && r.acos && r.atan && r.exp && r.log && r.log10
	    && r.sqrt && r.atan2 && r.pow && r.fmod && r.hypot;
#endif
	return r;
}

uint32_t bitsOf(float x)
{
	uint32_t u;
	memcpy(&u, &x, 4);
	return u;
}

// ULP distance with NaN/Inf class matching (NaN==NaN, same-signed Inf==Inf).
unsigned ulpDiff(float a, float b)
{
	if (std::isnan(a) && std::isnan(b))
		return 0;
	if (std::isinf(a) && std::isinf(b) && ((a > 0) == (b > 0)))
		return 0;
	if (std::isnan(a) || std::isnan(b) || std::isinf(a) || std::isinf(b))
		return 999;
	int32_t ia, ib;
	memcpy(&ia, &a, 4);
	memcpy(&ib, &b, 4);
	if ((ia < 0) != (ib < 0))
		return (a == b) ? 0 : 999;
	return (ia > ib) ? static_cast<unsigned>(ia - ib) : static_cast<unsigned>(ib - ia);
}

uint64_t fnv1a(uint64_t h, uint32_t w)
{
	h ^= w;
	h *= 1099511628211ull;
	return h;
}

// Committed golden checksum (FNV-1a/64 over every OUR result below).
// Any machine computing a different value is non-deterministic.
// This file is compiled with the libm's -fno-builtin-* flags so that every
// call reaches OUR definitions; with builtins on, GCC folds some of them at
// compile time and the checksum depends on the optimisation level.
// Measured 2026-10-13 on x86-64/MinGW, identical at -O1, -O2 and -O3. Updated
// 2026-11-04 for the NaN canonicalisation in eatF/eatD: the only words that
// changed are the three NaN results that came out negative on x86 (a macOS
// x86_64 build reproduced the old value exactly before the change).
const uint64_t kGolden = 0x4a959508a6080cfdULL;

} // namespace

int main()
{
	if (!pc_netplay_libm_present()) {
		std::printf("FAIL: pc_netplay_libm not linked\n");
		return 1;
	}
	Ref ref = loadRef();
	if (!ref.ok) {
		std::printf("FAIL: could not load platform libm reference\n");
		return 1;
	}
	std::printf("libm ABI version: 0x%016llx\n", (unsigned long long)pc_netplay_libm_abi_version());

	unsigned worst1[26] = { 0 };
	unsigned long long overBound[26] = { 0 }; // samples with d > bound
	unsigned long long nSamples[26] = { 0 };
	uint64_t sum = 1469598103934665603ull;
	// The sign of a NaN produced by an invalid operation is set by the CPU: x86
	// gives a negative quiet NaN, arm64 a positive one. Every NaN is hashed as
	// the positive quiet NaN so the golden holds on both. Finite and infinite
	// results are hashed bit for bit.
	auto eatF = [&](float v) { sum = fnv1a(sum, v != v ? 0x7fc00000u : bitsOf(v)); };
	auto eatD = [&](double v) {
		uint64_t u;
		memcpy(&u, &v, 8);
		if (v != v)
			u = 0x7ff8000000000000ull;
		sum = fnv1a(sum, static_cast<uint32_t>(u));
		sum = fnv1a(sum, static_cast<uint32_t>(u >> 32));
	};
	auto checkF = [&](unsigned idx, float ours, float expect) {
		// every sample counts. Class/sign mismatches (999) and
		// any error above the bound are recorded, never silently dropped.
		unsigned d = ulpDiff(ours, expect);
		nSamples[idx]++;
		if (d > worst1[idx])
			worst1[idx] = d;
		unsigned bound = (idx < 16) ? 1 : ((idx == 24 || idx == 25) ? 0 : 8);
		if (d > bound)
			overBound[idx]++;
		eatF(ours);
		return d;
	};

	const double l2 = ref.log(2.0);

	// ---- float grids over game domains (ulp-asserted, <= 1) ----
	for (unsigned i = 0; i < 150000; i++) {
		float x = static_cast<float>(-100.531 + 201.062 * (double)i / 149999.0);
		checkF(0, sinf(x), static_cast<float>(ref.sin(x)));
		checkF(1, cosf(x), static_cast<float>(ref.cos(x)));
	}
	for (unsigned i = 0; i < 100000; i++) {
		float x = static_cast<float>(-100.0 + 200.0 * (double)i / 99999.0);
		float s = 0, c = 0;
		sincosf(x, &s, &c);
		// Same no-drop rule as checkF.
		unsigned d1 = ulpDiff(s, static_cast<float>(ref.sin(x)));
		unsigned d2 = ulpDiff(c, static_cast<float>(ref.cos(x)));
		nSamples[2] += 2;
		if (d1 > worst1[2])
			worst1[2] = d1;
		if (d2 > worst1[2])
			worst1[2] = d2;
		if (d1 > 1)
			overBound[2]++;
		if (d2 > 1)
			overBound[2]++;
		eatF(s);
		eatF(c);
	}
	for (unsigned i = 0; i < 100000; i++) {
		float x = static_cast<float>(-100.0 + 200.0 * (double)i / 99999.0);
		checkF(3, tanf(x), static_cast<float>(ref.tan(x)));
	}
	for (unsigned i = 0; i < 80000; i++) {
		float x = static_cast<float>(-1000.0 + 2000.0 * (double)i / 79999.0);
		checkF(4, atanf(x), static_cast<float>(ref.atan(x)));
	}
	for (unsigned i = 0; i < 80000; i++) {
		float y = static_cast<float>(-10.0 + 20.0 * (double)(i % 400) / 399.0);
		float x = static_cast<float>(-10.0 + 20.0 * (double)(i / 400) / 199.0);
		checkF(5, atan2f(y, x), static_cast<float>(ref.atan2(y, x)));
	}
	for (unsigned i = 0; i < 60000; i++) {
		float x = static_cast<float>(-1.0 + 2.0 * (double)i / 59999.0);
		checkF(6, asinf(x), static_cast<float>(ref.asin(x)));
		checkF(7, acosf(x), static_cast<float>(ref.acos(x)));
	}
	for (unsigned i = 0; i < 80000; i++) {
		float x = static_cast<float>(-100.0 + 200.0 * (double)i / 79999.0);
		checkF(8, expf(x), static_cast<float>(ref.exp(x)));
	}
	for (unsigned i = 0; i < 80000; i++) { // dense mantissa x exponent grid
		// 121 exponents x ~661 mantissas: exercises the non-trivial
		// pc_log2_split path (the old powers-of-two grid hit u = s = 0).
		int ee = -60 + static_cast<int>(i % 121);
		double frac = 1.0 + (double)(i / 121) / (80000.0 / 121.0);
		float x = static_cast<float>(ldexp(frac, ee));
		checkF(9, logf(x), static_cast<float>(ref.log(x)));
		checkF(10, log2f(x), static_cast<float>(ref.log(x) / l2));
		checkF(11, log10f(x), static_cast<float>(ref.log10(x)));
	}
	for (unsigned i = 0; i < 40000; i++) {
		float x = static_cast<float>(-100.0 + 200.0 * (double)i / 39999.0);
		checkF(12, exp2f(x), static_cast<float>(ref.exp(x * 0.6931471805599453)));
	}
	for (unsigned i = 0; i < 80000; i++) {
		float b = static_cast<float>(0.01 + 100.0 * (double)(i % 400) / 399.0);
		float e = static_cast<float>(-5.0 + 10.0 * (double)(i / 400) / 199.0);
		checkF(13, powf(b, e), static_cast<float>(ref.pow(b, e)));
	}
	{
		float ys[4] = { 3.0f, 6.2831853f, 0.7f, 1234.5f };
		for (unsigned i = 0; i < 80000; i++) {
			float x = static_cast<float>(-1000000.0 + 2000000.0 * (double)i / 79999.0);
			for (int k = 0; k < 4; k++)
				checkF(14, fmodf(x, ys[k]), static_cast<float>(ref.fmod(x, ys[k])));
		}
	}
	for (unsigned i = 0; i < 40000; i++) {
		float x = static_cast<float>(-1000.0 + 2000.0 * (double)(i % 200) / 199.0);
		float y = static_cast<float>(-1000.0 + 2000.0 * (double)(i / 200) / 199.0);
		checkF(15, hypotf(x, y), static_cast<float>(ref.hypot(x, y)));
	}
	// SSE sqrt overrides: correctly rounded by
	// hardware, so the bound is 0, stricter than the 1 ulp float bound.
	for (unsigned i = 0; i < 40000; i++) {
		float x = static_cast<float>(1000.0 * (double)i / 39999.0);
		checkF(24, sqrtf(x), static_cast<float>(ref.sqrt(x)));
	}
	// Large trig args within the exact range (|x| <= 1e10).
	for (unsigned i = 0; i < 20000; i++) {
		float x = static_cast<float>(-1e10 + 2e10 * (double)i / 19999.0);
		checkF(0, sinf(x), static_cast<float>(ref.sin(x)));
		checkF(1, cosf(x), static_cast<float>(ref.cos(x)));
	}

	// ---- edge cases (finite ones ulp-asserted; huge/checksum-only noted) ----
	{
		float fedges[] = { 0.0f, -0.0f, 1.0f, -1.0f, 3.14159265f, -3.14159265f, 1.57079633f,
			6.28318531f, 1e6f, -1e6f, 1e10f, -1e10f, 1e-40f, 3.4028235e38f,
			static_cast<float>(INFINITY), static_cast<float>(-INFINITY),
			static_cast<float>(NAN) };
		for (float x : fedges) {
			// trig past the exact-reduction range (|x| > 2.8e16,
			// here FLT_MAX) is determinism/checksum-only, never ulp-asserted.
			if (!(x != x) && x != static_cast<float>(INFINITY)
			    && x != static_cast<float>(-INFINITY)
			    && (double)(x < 0 ? -(double)x : (double)x) > 2.8e16) {
				eatF(sinf(x));
				eatF(cosf(x));
				float s = 0, c = 0;
				sincosf(x, &s, &c);
				eatF(s);
				eatF(c);
				eatF(tanf(x));
			} else {
				checkF(0, sinf(x), static_cast<float>(ref.sin(x)));
				checkF(1, cosf(x), static_cast<float>(ref.cos(x)));
				checkF(3, tanf(x), static_cast<float>(ref.tan(x)));
			}
			checkF(4, atanf(x), static_cast<float>(ref.atan(x)));
			checkF(8, expf(x), static_cast<float>(ref.exp(x)));
			checkF(9, logf(x), static_cast<float>(ref.log(x)));
			checkF(24, sqrtf(x < 0 ? 0 : x), static_cast<float>(ref.sqrt(x < 0 ? 0 : x)));
		}
		{
			float neg[] = { -1.0f, -100.0f, static_cast<float>(-INFINITY) };
			for (float x : neg)
				checkF(24, sqrtf(x), static_cast<float>(ref.sqrt(x)));
		}
		float aedges[] = { 0.0f, -0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 1.5f, -1.5f,
			static_cast<float>(INFINITY), static_cast<float>(NAN) };
		for (float x : aedges) {
			checkF(6, asinf(x), static_cast<float>(ref.asin(x)));
			checkF(7, acosf(x), static_cast<float>(ref.acos(x)));
		}
		struct P2 {
			float y, x;
		};
		P2 pedges[] = { { 0, 0 }, { 0, -0.0f }, { -0.0f, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
			{ 1, 1 }, { -1, -1 }, { 1e30f, 1e-30f }, { static_cast<float>(INFINITY), 1 },
			{ 1, static_cast<float>(INFINITY) }, { static_cast<float>(NAN), 1 } };
		for (P2 p : pedges) {
			checkF(5, atan2f(p.y, p.x), static_cast<float>(ref.atan2(p.y, p.x)));
			checkF(13, powf(p.y, p.x), static_cast<float>(ref.pow(p.y, p.x)));
			// fmod is exact only for quotients below 2^52 and merely
			// deterministic past that (pc_netplay_libm.c): checksum only.
			if (std::fabs((double)p.y / (double)p.x) < 4503599627370496.0)
				checkF(14, fmodf(p.y, p.x), static_cast<float>(ref.fmod(p.y, p.x)));
			else
				eatF(fmodf(p.y, p.x));
			checkF(15, hypotf(p.y, p.x), static_cast<float>(ref.hypot(p.y, p.x)));
		}
		// Huge trig args: determinism (checksum) only, past exact range.
		float huge[] = { 1e20f, -1e20f, 1e30f, 3.4028235e38f };
		for (float x : huge) {
			eatF(sinf(x));
			eatF(cosf(x));
			eatF(tanf(x));
		}
	}

	// ---- double spot checks (informational bound, the bound applies to floats) ----
	{
		unsigned wsin = 0, wcos = 0, wtan = 0, watan = 0, wlog = 0, wexp = 0, wasin = 0,
		         wacos = 0, wsqrt = 0;
		auto ud = [](double a, double b) -> unsigned long long {
			if (std::isnan(a) && std::isnan(b))
				return 0;
			if (std::isinf(a) && std::isinf(b) && ((a > 0) == (b > 0)))
				return 0;
			if (std::isnan(a) || std::isnan(b) || std::isinf(a) || std::isinf(b))
				return 999;
			int64_t ia, ib;
			memcpy(&ia, &a, 8);
			memcpy(&ib, &b, 8);
			if ((ia < 0) != (ib < 0))
				return (a == b) ? 0 : 999;
			return (ia > ib) ? static_cast<unsigned long long>(ia - ib)
			                 : static_cast<unsigned long long>(ib - ia);
		};
		for (unsigned i = 0; i < 30000; i++) {
			double x = -100.531 + 201.062 * (double)i / 29999.0;
			unsigned long long d;
			// no < 900 cap; any error above bound 8 fails below.
			d = ud(sin(x), ref.sin(x));
			if (d > wsin)
				wsin = static_cast<unsigned>(d > 0xffffffffull ? 0xffffffffull : d);
			eatD(sin(x));
			d = ud(cos(x), ref.cos(x));
			if (d > wcos)
				wcos = static_cast<unsigned>(d > 0xffffffffull ? 0xffffffffull : d);
			eatD(cos(x));
			d = ud(tan(x), ref.tan(x));
			if (d > wtan)
				wtan = static_cast<unsigned>(d > 0xffffffffull ? 0xffffffffull : d);
			eatD(tan(x));
			d = ud(atan(x * 9.9), ref.atan(x * 9.9));
			if (d > watan)
				watan = static_cast<unsigned>(d > 0xffffffffull ? 0xffffffffull : d);
			eatD(atan(x));
			double la = 1.0 + (x + 100.531) * 10.0;
			d = ud(log(la), ref.log(la));
			if (d > wlog)
				wlog = static_cast<unsigned>(d > 0xffffffffull ? 0xffffffffull : d);
			eatD(log(la));
			d = ud(exp(x * 0.5), ref.exp(x * 0.5));
			if (d > wexp)
				wexp = static_cast<unsigned>(d > 0xffffffffull ? 0xffffffffull : d);
			eatD(exp(x));
			double aa = -1.0 + 2.0 * (double)(i % 30000) / 29999.0;
			d = ud(asin(aa), ref.asin(aa));
			if (d > wasin)
				wasin = static_cast<unsigned>(d > 0xffffffffull ? 0xffffffffull : d);
			eatD(asin(aa));
			d = ud(acos(aa), ref.acos(aa));
			if (d > wacos)
				wacos = static_cast<unsigned>(d > 0xffffffffull ? 0xffffffffull : d);
			eatD(acos(aa));
			double sa = 1000.0 * (double)i / 29999.0;
			d = ud(sqrt(sa), ref.sqrt(sa));
			if (d > wsqrt)
				wsqrt = static_cast<unsigned>(d > 0xffffffffull ? 0xffffffffull : d);
			eatD(sqrt(sa));
		}
		worst1[16] = wsin;
		worst1[17] = wcos;
		worst1[18] = wtan;
		worst1[19] = watan;
		worst1[20] = wasin;
		worst1[21] = wacos;
		worst1[22] = wexp;
		worst1[23] = wlog;
		worst1[25] = wsqrt;
	}

	const char *shortNames[26] = { "sinf", "cosf", "sincosf", "tanf", "atanf", "atan2f", "asinf",
		"acosf", "expf", "logf", "log2f", "log10f", "exp2f", "powf", "fmodf", "hypotf", "sin",
		"cos", "tan", "atan", "asin", "acos", "exp", "log", "sqrtf", "sqrt" };
	unsigned fails = 0;
	for (unsigned i = 0; i < 26; i++) {
		// floats: <= 1; doubles: informational 8; sqrt is
		// exact IEEE (sqrtsd/sqrtss): bound 0.
		unsigned bound = (i < 16) ? 1 : ((i == 24 || i == 25) ? 0 : 8);
		bool pass = worst1[i] <= bound;
		std::printf("ulp %-8s max=%-10u bound=%u over=%llu n=%llu %s\n", shortNames[i],
		    worst1[i], bound, overBound[i], nSamples[i], pass ? "ok" : "FAIL");
		if (!pass)
			fails++;
	}
	std::printf("checksum 0x%016llx golden 0x%016llx %s\n", (unsigned long long)sum,
	    (unsigned long long)kGolden, sum == kGolden ? "ok" : "FAIL");
	if (sum != kGolden)
		fails++;
	if (fails) {
		std::printf("pc_netplay_libm_test: %u FAILURE(s)\n", fails);
		return 1;
	}
	std::printf("pc_netplay_libm_test: all checks passed\n");
	return 0;
}
