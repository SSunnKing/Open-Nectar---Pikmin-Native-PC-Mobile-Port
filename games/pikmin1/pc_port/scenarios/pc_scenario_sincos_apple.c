/* Apple only, in scenario builds (PIKMIN_SCENARIO_FLOAT_DET) and netplay builds
 * (PIKMIN_NETPLAY_BUILD), the two that link the software libm (CMakeLists.txt).
 *
 * clang on Apple targets turns a sinf(x) and a cosf(x) of the same x into one
 * call to __sincosf_stret (and sin/cos into __sincos_stret), Apple libm entry
 * points that the software libm in pc_port/netplay/pc_netplay_libm.c does not
 * define. Without these two definitions such a build still runs Apple's
 * sine and cosine wherever the compiler fused the pair. Defined here, in the
 * executable, they take precedence over libSystem at static link and route
 * both halves to the software sinf/cosf (sin/cos).
 *
 * Compiled with -fno-builtin and outside LTO: otherwise clang would fuse the
 * two calls below back into __sincosf_stret, which is this function.
 */
#include <math.h>

struct __float2 __sincosf_stret(float x)
{
	struct __float2 r;
	r.__sinval = sinf(x);
	r.__cosval = cosf(x);
	return r;
}

struct __double2 __sincos_stret(double x)
{
	struct __double2 r;
	r.__sinval = sin(x);
	r.__cosval = cos(x);
	return r;
}
