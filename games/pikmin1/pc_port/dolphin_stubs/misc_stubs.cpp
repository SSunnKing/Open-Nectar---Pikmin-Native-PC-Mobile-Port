/**
 * @file misc_stubs.cpp
 * @brief Stub implementations for miscellaneous Dolphin SDK functions.
 * Signatures match: exi.h, si.h, mtx.h, db.h, hio.h, gba.h
 */
#include "types.h"
#include "Dolphin/exi.h"
#include "Dolphin/si.h"
#include "Dolphin/mtx.h"
#include "Dolphin/db.h"
#include "Dolphin/hio.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>

extern "C" {

/* ── EXI (from Dolphin/exi.h) ── */
/* EXILock takes EXICallback, not void* */
BOOL EXILock(s32 chan, u32 dev, EXICallback unlockedCb) { (void)chan; (void)dev; (void)unlockedCb; return TRUE; }
BOOL EXIUnlock(s32 chan)           { (void)chan; return TRUE; }
BOOL EXISelect(s32 chan, u32 dev, u32 freq) { (void)chan; (void)dev; (void)freq; return TRUE; }
BOOL EXIDeselect(s32 chan)         { (void)chan; return TRUE; }
BOOL EXIImm(s32 chan, void* buf, s32 len, u32 type, EXICallback cb) {
    (void)chan; (void)buf; (void)len; (void)type; (void)cb; return TRUE;
}
BOOL EXIImmEx(s32 chan, void* buf, s32 len, u32 type) {
    (void)chan; (void)buf; (void)len; (void)type; return TRUE;
}
BOOL EXIDma(s32 chan, void* buf, s32 len, u32 type, EXICallback cb) {
    (void)chan; (void)buf; (void)len; (void)type; (void)cb; return TRUE;
}
BOOL EXISync(s32 chan)             { (void)chan; return TRUE; }
BOOL EXIAttach(s32 chan, EXICallback cb) { (void)chan; (void)cb; return TRUE; }
BOOL EXIDetach(s32 chan)           { (void)chan; return TRUE; }
s32  EXIGetID(s32 chan, u32 dev, u32* id) { (void)chan; (void)dev; *id = 0; return 0; }
void EXIInit()                     { }

/* ── SI (from Dolphin/si.h) ── */
void SIInit(void)                  { }
u32  SISync(void)                  { return 0; }
u32  SIGetStatus(s32 chan)         { (void)chan; return 0; }
void SISetCommand(s32 chan, u32 command) { (void)chan; (void)command; }
void SITransferCommands(void)      { }
u32  SISetXY(u32 x, u32 y)       { (void)x; (void)y; return 0; }
u32  SIEnablePolling(u32 poll)    { (void)poll; return 0; }
u32  SIDisablePolling(u32 poll)   { (void)poll; return 0; }
/* SIGetResponse changed signature between SDK builds, and the two retail
   releases of this game were built against different ones: USA Rev 1 predates
   20011002, PAL does not. si.h already branches on OS_BUILD_VERSION; match it
   rather than assuming a version here. */
#if OS_BUILD_VERSION >= 20011002L
BOOL SIGetResponse(s32 chan, void* data) { (void)chan; (void)data; return FALSE; }
#else
void SIGetResponse(s32 chan, void* data) { (void)chan; (void)data; }
#endif
/* Takes __OSInterruptHandler, not void* */
BOOL SIRegisterPollingHandler(__OSInterruptHandler handler) { (void)handler; return TRUE; }
BOOL SIUnregisterPollingHandler(__OSInterruptHandler handler) { (void)handler; return TRUE; }

/* ── MTX (from Dolphin/mtx.h) ── */
void PSMTXIdentity(Mtx mtx) {
    memset(mtx, 0, sizeof(Mtx));
    mtx[0][0] = 1.0f; mtx[1][1] = 1.0f; mtx[2][2] = 1.0f;
}
void PSMTXCopy(const Mtx src, Mtx dst) { memcpy(dst, src, sizeof(Mtx)); }
void PSMTXConcat(const Mtx a, const Mtx b, Mtx ab) {
    Mtx tmp;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            tmp[i][j] = a[i][0]*b[0][j] + a[i][1]*b[1][j] + a[i][2]*b[2][j];
        }
        tmp[i][3] += a[i][3];
    }
    memcpy(ab, tmp, sizeof(Mtx));
}
/* Returns u32 (mtx.h line 149) */
u32 PSMTXInverse(const Mtx src, Mtx inv) {
    Mtx result;
    const f32 det =
        src[0][0] * (src[1][1] * src[2][2] - src[1][2] * src[2][1]) -
        src[0][1] * (src[1][0] * src[2][2] - src[1][2] * src[2][0]) +
        src[0][2] * (src[1][0] * src[2][1] - src[1][1] * src[2][0]);
    if (fabsf(det) <= 1.0e-12f) return 0;

    const f32 invDet = 1.0f / det;
    result[0][0] =  (src[1][1] * src[2][2] - src[1][2] * src[2][1]) * invDet;
    result[0][1] = -(src[0][1] * src[2][2] - src[0][2] * src[2][1]) * invDet;
    result[0][2] =  (src[0][1] * src[1][2] - src[0][2] * src[1][1]) * invDet;
    result[1][0] = -(src[1][0] * src[2][2] - src[1][2] * src[2][0]) * invDet;
    result[1][1] =  (src[0][0] * src[2][2] - src[0][2] * src[2][0]) * invDet;
    result[1][2] = -(src[0][0] * src[1][2] - src[0][2] * src[1][0]) * invDet;
    result[2][0] =  (src[1][0] * src[2][1] - src[1][1] * src[2][0]) * invDet;
    result[2][1] = -(src[0][0] * src[2][1] - src[0][1] * src[2][0]) * invDet;
    result[2][2] =  (src[0][0] * src[1][1] - src[0][1] * src[1][0]) * invDet;

    for (int row = 0; row < 3; ++row) {
        result[row][3] = -(result[row][0] * src[0][3] +
                           result[row][1] * src[1][3] +
                           result[row][2] * src[2][3]);
    }
    memcpy(inv, result, sizeof(Mtx));
    return 1;
}
void C_MTXOrtho(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f) {
    const f32 invWidth = 1.0f / (r - l);
    const f32 invHeight = 1.0f / (t - b);
    const f32 invDepth = 1.0f / (f - n);
    memset(m, 0, sizeof(Mtx44));
    m[0][0] = 2.0f * invWidth;
    m[0][3] = -(r + l) * invWidth;
    m[1][1] = 2.0f * invHeight;
    m[1][3] = -(t + b) * invHeight;
    m[2][2] = -invDepth;
    m[2][3] = -f * invDepth;
    m[3][3] = 1.0f;
}
void C_MTXPerspective(Mtx44 m, f32 fovY, f32 aspect, f32 n, f32 f) {
    const f32 radians = fovY * 0.5f * (3.14159265358979323846f / 180.0f);
    const f32 cotangent = 1.0f / tanf(radians);
    const f32 invDepth = 1.0f / (f - n);
    memset(m, 0, sizeof(Mtx44));
    m[0][0] = cotangent / aspect;
    m[1][1] = cotangent;
    m[2][2] = -n * invDepth;
    m[2][3] = -(f * n) * invDepth;
    m[3][2] = -1.0f;
}
void MTXLightPerspective(Mtx m, f32 fovY, f32 aspect, f32 scaleS, f32 scaleT, f32 transS, f32 transT) {
    (void)m; (void)fovY; (void)aspect; (void)scaleS; (void)scaleT; (void)transS; (void)transT;
}
u32 PSMTXInvXpose(const Mtx src, Mtx invX) {
    Mtx inverse;
    if (!PSMTXInverse(src, inverse)) return 0;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            invX[row][column] = inverse[column][row];
        }
        invX[row][3] = 0.0f;
    }
    return 1;
}
void PSMTXTranspose(const Mtx src, Mtx xPose) {
    Mtx tmp;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            tmp[i][j] = src[j][i];
    tmp[0][3] = tmp[1][3] = tmp[2][3] = 0.0f;
    memcpy(xPose, tmp, sizeof(Mtx));
}
void PSMTXScale(Mtx mtx, f32 sx, f32 sy, f32 sz) {
    PSMTXIdentity(mtx);
    mtx[0][0] = sx; mtx[1][1] = sy; mtx[2][2] = sz;
}
/* On the newer SDK, MTXScale is a macro for PSMTXScale, so this wrapper would
   be a second definition of the function directly above. On the older one it
   aliases C_MTXScale and the wrapper is what provides it. */
#if OS_BUILD_VERSION < 20011002L
void MTXScale(Mtx mtx, f32 sx, f32 sy, f32 sz) {
    PSMTXScale(mtx, sx, sy, sz);
}
#endif
void PSMTXTrans(Mtx mtx, f32 tx, f32 ty, f32 tz) {
    PSMTXIdentity(mtx);
    mtx[0][3] = tx; mtx[1][3] = ty; mtx[2][3] = tz;
}
#ifdef MTXTrans
#undef MTXTrans
#endif
void MTXTrans(Mtx mtx, f32 tx, f32 ty, f32 tz) {
    PSMTXTrans(mtx, tx, ty, tz);
}
void PSMTXScaleApply(const Mtx src, Mtx dst, f32 sx, f32 sy, f32 sz) {
    PSMTXCopy(src, dst);
    dst[0][0] *= sx; dst[0][1] *= sx; dst[0][2] *= sx; dst[0][3] *= sx;
    dst[1][0] *= sy; dst[1][1] *= sy; dst[1][2] *= sy; dst[1][3] *= sy;
    dst[2][0] *= sz; dst[2][1] *= sz; dst[2][2] *= sz; dst[2][3] *= sz;
}
void PSMTXTransApply(const Mtx src, Mtx dst, f32 tx, f32 ty, f32 tz) {
    PSMTXCopy(src, dst);
    dst[0][3] += tx; dst[1][3] += ty; dst[2][3] += tz;
}
void PSMTXRotRad(Mtx mtx, char axis, f32 rad) {
    PSMTXIdentity(mtx);
    f32 s = sinf(rad), c = cosf(rad);
    switch (axis) {
        case 'x': case 'X': mtx[1][1]=c; mtx[1][2]=-s; mtx[2][1]=s; mtx[2][2]=c; break;
        case 'y': case 'Y': mtx[0][0]=c; mtx[0][2]=s; mtx[2][0]=-s; mtx[2][2]=c; break;
        case 'z': case 'Z': mtx[0][0]=c; mtx[0][1]=-s; mtx[1][0]=s; mtx[1][1]=c; break;
    }
}
void PSMTXMultVec(const Mtx mtx, const Vec* src, Vec* dst) {
    Vec tmp;
    tmp.x = mtx[0][0]*src->x + mtx[0][1]*src->y + mtx[0][2]*src->z + mtx[0][3];
    tmp.y = mtx[1][0]*src->x + mtx[1][1]*src->y + mtx[1][2]*src->z + mtx[1][3];
    tmp.z = mtx[2][0]*src->x + mtx[2][1]*src->y + mtx[2][2]*src->z + mtx[2][3];
    *dst = tmp;
}
void PSMTXMultVecSR(const Mtx mtx, const Vec* src, Vec* dst) {
    Vec tmp;
    tmp.x = mtx[0][0]*src->x + mtx[0][1]*src->y + mtx[0][2]*src->z;
    tmp.y = mtx[1][0]*src->x + mtx[1][1]*src->y + mtx[1][2]*src->z;
    tmp.z = mtx[2][0]*src->x + mtx[2][1]*src->y + mtx[2][2]*src->z;
    *dst = tmp;
}

/* Vec operations */
void PSVECAdd(const Vec* a, const Vec* b, Vec* ab) {
    ab->x = a->x + b->x; ab->y = a->y + b->y; ab->z = a->z + b->z;
}
void PSVECSubtract(const Vec* a, const Vec* b, Vec* ab) {
    ab->x = a->x - b->x; ab->y = a->y - b->y; ab->z = a->z - b->z;
}
void PSVECScale(const Vec* src, Vec* dst, f32 scale) {
    dst->x = src->x * scale; dst->y = src->y * scale; dst->z = src->z * scale;
}
void PSVECNormalize(const Vec* src, Vec* unit) {
    f32 mag = sqrtf(src->x*src->x + src->y*src->y + src->z*src->z);
    if (mag > 0.0f) { unit->x = src->x/mag; unit->y = src->y/mag; unit->z = src->z/mag; }
    else { unit->x = unit->y = unit->z = 0.0f; }
}
f32 PSVECMag(const Vec* v) { return sqrtf(v->x*v->x + v->y*v->y + v->z*v->z); }
#if OS_BUILD_VERSION < 20011002L
f32 VECMag(const Vec* v) { return PSVECMag(v); }
#endif
f32 PSVECSquareMag(const Vec* v) { return v->x*v->x + v->y*v->y + v->z*v->z; }
f32 PSVECDotProduct(const Vec* a, const Vec* b) { return a->x*b->x + a->y*b->y + a->z*b->z; }
void PSVECCrossProduct(const Vec* a, const Vec* b, Vec* axb) {
    Vec tmp;
    tmp.x = a->y*b->z - a->z*b->y;
    tmp.y = a->z*b->x - a->x*b->z;
    tmp.z = a->x*b->y - a->y*b->x;
    *axb = tmp;
}
f32 PSVECSquareDistance(const Vec* a, const Vec* b) {
    f32 dx = a->x-b->x, dy = a->y-b->y, dz = a->z-b->z;
    return dx*dx + dy*dy + dz*dz;
}
f32 PSVECDistance(const Vec* a, const Vec* b) {
    return sqrtf(PSVECSquareDistance(a, b));
}

/* Quaternion stubs */
void PSQUATMultiply(const void* q1, const void* q2, void* result) { (void)q1; (void)q2; (void)result; }

/* ── HIO (from Dolphin/hio.h) — takes HIOCallback ── */
BOOL HIOInit(s32 chan, HIOCallback callback) { (void)chan; (void)callback; return FALSE; }
BOOL HIOReadMailbox(u32* word) { (void)word; return FALSE; }
BOOL HIOEnumDevices(HIOEnumCallback cb) { (void)cb; return FALSE; }
BOOL HIOWrite(u32 addr, void* buf, s32 size) { (void)addr; (void)buf; (void)size; return FALSE; }
BOOL HIOWriteMailbox(u32 word) { (void)word; return FALSE; }

/* ── GBA ── */
void GBAInit(void) { }

/* ── PPCArch ── */
void PPCMtmsr(u32 val) { (void)val; }
u32  PPCMfmsr(void)    { return 0; }
void PPCMtdec(u32 val) { (void)val; }
void PPCSync(void)     { }
void PPCHalt(void)     { printf("[PC Port] PPCHalt called - exiting\n"); exit(0); }

/* ── OdemuExi2 / AMC stubs ── */
void OdemuExi2_Init(void) { }
void amcExi2_Init(void) { }

} // extern "C"

/* ── DB (from Dolphin/db.h) — NOT extern "C" (db.h has no BEGIN_SCOPE_EXTERN_C) ── */
BOOL DBVerbose = FALSE;
DBInterface* __DBInterface = nullptr;

void DBInit(void)                  { }
void DBPrintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
