#ifndef PC_GX_LIGHTING_GLSL_H
#define PC_GX_LIGHTING_GLSL_H

// Ecuación de iluminación de GX (canales 0 y 1, difusa con atenuación de foco
// y distancia y especular como cociente de cuadráticas), compartida por el
// shader de vértices (modo original, por vértice) y por los de fragmento
// (modo por píxel: misma ecuación con la normal interpolada). Los uniforms
// se declaran en ambas etapas con el mismo nombre y tipo, así que el programa
// los comparte.
static const char* kGxLightingGlsl =
    "uniform int uNumLights;\n"
    "uniform vec4 uLightPos[4];\n"
    "uniform vec4 uLightColor[4];\n"
    "uniform vec4 uLightK[4];\n"     // distance attenuation k0,k1,k2,enabled
    "uniform vec4 uLightDir[4];\n"   // spot axis as GXInitLightDir stored it
    "uniform vec4 uLightA[4];\n"     // angular attenuation a0,a1,a2
    "uniform vec4 uAmbColor;\n"
    "uniform int uChan0En;\n"
    "uniform int uChan1En;\n"
    "uniform int uChan0AttnFn;\n"  // GXAttnFn: 0=SPEC, 1=SPOT, 2=NONE
    "uniform int uChan0AlphaEn;\n"   // GX_ALPHA0 lighting on
    "uniform int uChan0AlphaDiff;\n" // GXDiffuseFn of GX_ALPHA0: 0=NONE, 1=SIGN, 2=CLAMP
    "uniform int uChan1AttnFn;\n"
    "uniform int uNumLights1;\n"
    "uniform vec4 uLightPos1[4];\n"
    "uniform vec4 uLightColor1[4];\n"
    "uniform vec4 uLightK1[4];\n"
    "uniform vec4 uLightDir1[4];\n"
    "uniform vec4 uLightA1[4];\n"
    "uniform vec4 uAmbColor1;\n"
    "uniform vec4 uSpecHalf1;\n"   // specular half-vector for channel 1
    "uniform vec4 uSpecAttn1;\n"   // specular a0,a1,a2 quadratic coefficients
    // GX_AF_SPOT as the hardware evaluates it: the angular quadratic in
    // cos(spot axis, light->vertex) over the distance quadratic (Dolphin does
    // the same). Light::setLightSpot fills a0..a2 for the cone and
    // setLightDistAttn fills k0..k2 with k0 = 1 and k1/k2 > 0, so the old
    // clamp(k0 + k1*d + k2*d*d) factor was always 1: no light ever faded with
    // distance and every spotlight lit the whole scene. Sunlight (k and a both
    // 1,0,0) comes out the same either way.
    "float gxAttn(vec3 L, float dist, vec4 k, vec4 d, vec4 a) {\n"
    "    if (k.w < 0.5) return 1.0;\n"
    "    float aat = a.x;\n"
    // Point lights keep a = (1,0,0) and never set a direction, so the axis
    // may be zero or stale; skip it rather than normalize it.
    "    if (a.y != 0.0 || a.z != 0.0) {\n"
    // GXInitLightDir stores the direction pointing away from the light, so
    // the cosine the hardware uses is against its negation.
    "        float cosA = max(dot(L, -normalize(d.xyz)), 0.0);\n"
    "        aat += a.y * cosA + a.z * cosA * cosA;\n"
    "    }\n"
    "    float dat = k.x + k.y * dist + k.z * dist * dist;\n"
    "    return (dat > 1e-5) ? max(aat, 0.0) / dat : 0.0;\n"
    "}\n"
    "vec3 gxDoLights(vec3 N, vec4 wp, int n, vec4 lp[4], vec4 lc[4], vec4 lk[4], vec4 ld[4], vec4 la[4]) {\n"
    "    vec3 sum = vec3(0.0);\n"
    "    for (int i = 0; i < n; i++) {\n"
    "        vec3 Ld = lp[i].xyz - wp.xyz;\n"
    "        float dist = length(Ld);\n"
    "        vec3 L = Ld / max(dist, 0.001);\n"
    "        float diff = max(dot(N, L), 0.0) * gxAttn(L, dist, lk[i], ld[i], la[i]);\n"
    "        sum += diff * lc[i].rgb;\n"
    "    }\n"
    "    return sum;\n"
    "}\n"
    // Alfa del canal 0 (GX_ALPHA0): misma ecuación con el alfa de las luces
    // y del ambiente. Sin ella, el agua y el casco de Olimar salían con el
    // alfa del material tal cual, más opacos que en GameCube.
    "float gxLitA0(vec3 N, vec4 wp) {\n"
    "    float sum = uAmbColor.a;\n"
    "    for (int i = 0; i < uNumLights; i++) {\n"
    "        vec3 Ld = uLightPos[i].xyz - wp.xyz;\n"
    "        float dist = length(Ld);\n"
    "        vec3 L = Ld / max(dist, 0.001);\n"
    "        float diff = 1.0;\n"
    "        if (uChan0AlphaDiff != 0) {\n"
    "            diff = dot(N, L);\n"
    "            if (uChan0AlphaDiff == 2) diff = max(diff, 0.0);\n"
    "        }\n"
    "        diff *= gxAttn(L, dist, uLightK[i], uLightDir[i], uLightA[i]);\n"
    "        sum += diff * uLightColor[i].a;\n"
    "    }\n"
    "    return clamp(sum, 0.0, 1.0);\n"
    "}\n"
    // Canal 0: ambiente + difusa. vec3(-1) = canal apagado (el fragmento usa
    // el color base tal cual).
    "vec3 gxLit0(vec3 N, vec4 wp) {\n"
    "    if (uChan0En == 0) return vec3(-1.0);\n"
    "    return clamp(uAmbColor.rgb + gxDoLights(N, wp, uNumLights, uLightPos, uLightColor, uLightK, uLightDir, uLightA), 0.0, 1.0);\n"
    "}\n"
    // Canal 1: ambiente + difusa o, con GX_AF_SPEC (0), especular GX: cociente
    // de dos cuadráticas en N.H (ángulo entre distancia).
    "vec3 gxLit1(vec3 N, vec4 wp) {\n"
    "    if (uChan1En == 0) return vec3(-1.0);\n"
    "    vec3 spec1 = vec3(0.0);\n"
    "    if (uChan1AttnFn == 0 && uNumLights1 > 0) {\n"
    "        float cosT = max(dot(N, normalize(uSpecHalf1.xyz)), 0.0);\n"
    "        vec3 quad = vec3(1.0, cosT, cosT * cosT);\n"
    "        float num = max(0.0, dot(uSpecAttn1.xyz, quad));\n"
    "        float den = dot(uLightK1[0].xyz, quad);\n"
    "        float att = (den > 1e-5) ? clamp(num / den, 0.0, 1.0) : 0.0;\n"
    "        spec1 = att * uLightColor1[0].rgb;\n"
    "    }\n"
    "    vec3 diffuse1 = (uChan1AttnFn == 0) ? vec3(0.0) : gxDoLights(N, wp, uNumLights1, uLightPos1, uLightColor1, uLightK1, uLightDir1, uLightA1);\n"
    "    return clamp(uAmbColor1.rgb + diffuse1 + spec1, 0.0, 1.0);\n"
    "}\n";

// Fragmento: entradas y selector por píxel. Con uPerPixel a 0 se usa el
// valor interpolado del vértice (vLit*), es decir, el aspecto original.
static const char* kGxLightingFragGlsl =
    "in vec3 vWorldPos;\n"
    "in vec3 vNormal;\n"
    "uniform int uPerPixel;\n"
    // Entradas a/b/c del TEV: el hardware sólo usa los 8 bits bajos de un
    // registro S10 (Dolphin hace lo mismo). El agua trae alfa 345 -> 89; sin
    // esto saturaba a 1.0 y salía opaca.
    "float tevU8(float v) { return mod(floor(v * 255.0 + 0.5), 256.0) / 255.0; }\n"
    "vec3 tevU8(vec3 v) { return mod(floor(v * 255.0 + 0.5), vec3(256.0)) / 255.0; }\n"
    "vec4 tevU8(vec4 v) { return vec4(tevU8(v.rgb), v.a); }\n"
    "vec3 gxPixelLit0(vec3 lit) { return (uPerPixel != 0 && lit.x >= -0.5) ? gxLit0(normalize(vNormal), vec4(vWorldPos, 1.0)) : lit; }\n"
    "float gxLitAlpha0(float a) { return uChan0AlphaEn != 0 ? a * gxLitA0(normalize(vNormal), vec4(vWorldPos, 1.0)) : a; }\n"
    "vec3 gxPixelLit1(vec3 lit) { return (uPerPixel != 0 && lit.x >= -0.5) ? gxLit1(normalize(vNormal), vec4(vWorldPos, 1.0)) : lit; }\n";

#endif
