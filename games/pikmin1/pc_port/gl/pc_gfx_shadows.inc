// Sombras proyectadas (shadow map del sol) para el backend GX→GL.
//
// Se incluye desde pc_gfx.cpp para compartir sus estáticos y punteros GL.
//
// Cómo funciona:
//  1. Durante el dibujo del mundo (proyección perspectiva, antes del primer
//     ortográfico que inicia la interfaz) cada draw que escribe profundidad
//     se anota: geometría (copia de los vértices en streaming, o rango del
//     arena de mallas residentes), matriz de posición o paleta, textura y
//     test de alpha para los recortes, y culling.
//  2. En post_apply, antes de la oclusión, se dibuja esa lista desde la luz
//     en un mapa de profundidad. Todo está en espacio de vista (las matrices
//     GX ya llevan la cámara), y las luces GX también, así que el mapa se
//     construye en ese mismo espacio: no hace falta la matriz de vista.
//  3. Una pasada de pantalla completa reconstruye la posición de vista de
//     cada píxel desde el depth buffer, la lleva al espacio de la luz y
//     compara (PCF 3x3). La máscara resultante la multiplica el compuesto
//     como hace con la oclusión.

namespace {

struct ShadowVertex {
    float x, y, z;
    float u, v;
    float slot;
};

struct ShadowDraw {
    bool resident = false;   // geometría en el arena de mallas (sMeshArena)
    GLint first = 0;
    GLsizei count = 0;
    GLenum mode = GL_TRIANGLES;
    bool palette = false;
    int paletteOffset = 0;   // índice en sShadowPalettes (slots*3 vec4)
    int paletteSlots = 0;
    float mtx[16];
    GLuint tex = 0;
    bool alphaTest = false;
    float alphaRef = 0.0f;
    GXCullMode cull = GX_CULL_BACK;
};

std::vector<ShadowVertex> sShadowStream;   // copia compacta de los draws en streaming
std::vector<float> sShadowPalettes;        // paletas de los draws con skinning
std::vector<ShadowDraw> sShadowDraws;
bool sShadowHaveLight = false;
float sShadowLightDir[3] = { 0.0f, 1.0f, 0.0f }; // hacia la luz, espacio de vista
float sSunColour[3] = { 1.0f, 0.95f, 0.8f };

// El sol: primera luz del canal 0 de un draw iluminado. Lo llama también la
// subida de luces del draw.
void sun_capture_from_channel0() {
    if (sShadowHaveLight || !sChannels[0].enabled) return;
    const u32 mask = sChannels[0].lightMask;
    for (int i = 0; i < 8; i++) {
        if (!(mask & (1u << i))) continue;
        const float* p = sLights[i].pos;
        const float len = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        if (len > 1.0f) {
            sShadowLightDir[0] = p[0] / len; sShadowLightDir[1] = p[1] / len; sShadowLightDir[2] = p[2] / len;
            sSunColour[0] = sLights[i].color[0]; sSunColour[1] = sLights[i].color[1]; sSunColour[2] = sLights[i].color[2];
            sShadowHaveLight = true;
        }
        break;
    }
}

GLuint sShadowFbo = 0, sShadowDepthTex = 0, sShadowMaskFbo = 0, sShadowMaskTex = 0;
int sShadowMapSize = 0, sShadowMaskW = 0, sShadowMaskH = 0;
GLuint sShadowDepthProgram = 0, sShadowMaskProgram = 0;
GLuint sShadowStreamVBO = 0, sShadowStreamVAO = 0, sShadowArenaVAO = 0;
bool sShadowMaskReady = false;
bool sShadowProjPerspective = false;
bool sShadowExclude = false;

bool shadow_recording_wanted() {
    return pc_post_shadows_active(sPostEffects) && sNativeFramebufferReady && !sPostRanThisFrame
        && sShadowProjPerspective && sProjOffX == 0.0f && sProjOffY == 0.0f; // sin pantalla partida
}

void shadow_frame_reset() {
    sShadowStream.clear();
    sShadowPalettes.clear();
    sShadowDraws.clear();
    sShadowHaveLight = false;
    sShadowMaskReady = false;
}

// Anota el estado común de un draw; devuelve false si no proyecta sombra.
bool shadow_record_state(ShadowDraw& d) {
    if (sShadowExclude) return false;          // cursor, marcadores (pc_gfx_shadow_exclude)
    if (!sPipelineState.zUpdate) return false; // partículas, efectos: sin profundidad
    if (!sPipelineState.zCompare) return false; // cursor, anillos, marcadores: se pintan por encima
    if (sPipelineState.blendType == GX_BM_BLEND && sPipelineState.blendDst == GX_BL_ONE) return false; // aditivo
    const bool blended = sPipelineState.blendType == GX_BM_BLEND
        && !(sPipelineState.blendSrc == GX_BL_ONE && sPipelineState.blendDst == GX_BL_ZERO);
    const bool alphaTest = sAlphaComp0 != GX_ALWAYS && sAlphaComp0 != GX_NEVER && sBoundTextures[0] != 0;
    if (sPipelineState.blendType == GX_BM_LOGIC || sPipelineState.blendType == GX_BM_SUBTRACT) return false;
    if (blended && !alphaTest) return false; // agua, cristales: no proyectan
    d.alphaTest = alphaTest;
    d.alphaRef = sAlphaRef0;
    d.tex = sBoundTextures[0];
    d.cull = sPipelineState.cull;
    d.palette = sVertexUsesPalette && !sVerticesPretransformed;
    if (d.palette) {
        int slots = sPaletteSlotsNeeded;
        if (slots < 1) slots = 1;
        if (slots > kPaletteSlots) slots = kPaletteSlots;
        d.paletteSlots = slots;
        d.paletteOffset = (int)sShadowPalettes.size();
        for (int slot = 0; slot < slots; ++slot) {
            const float* m = sPosMatrix[slot * 3];
            for (int r = 0; r < 3; ++r) {
                sShadowPalettes.push_back(m[r]);
                sShadowPalettes.push_back(m[4 + r]);
                sShadowPalettes.push_back(m[8 + r]);
                sShadowPalettes.push_back(m[12 + r]);
            }
        }
    } else {
        const float* m = sVerticesPretransformed ? nullptr : sPosMatrix[sCurrentPosMtxId];
        for (int i = 0; i < 16; i++) d.mtx[i] = m ? m[i] : ((i % 5 == 0) ? 1.0f : 0.0f);
    }
    sun_capture_from_channel0();
    return true;
}

void shadow_record_stream(const std::vector<Vertex>& verts, GLenum mode) {
    if (!shadow_recording_wanted() || verts.empty()) return;
    ShadowDraw d;
    if (!shadow_record_state(d)) return;
    d.resident = false;
    d.mode = mode;
    d.first = (GLint)sShadowStream.size();
    d.count = (GLsizei)verts.size();
    sShadowStream.reserve(sShadowStream.size() + verts.size());
    for (const Vertex& v : verts) sShadowStream.push_back({ v.x, v.y, v.z, v.tex[0][0], v.tex[0][1], v.matrixSlot });
    sShadowDraws.push_back(d);
}

void shadow_record_resident(GLint first, GLsizei count) {
    if (!shadow_recording_wanted() || count <= 0) return;
    ShadowDraw d;
    if (!shadow_record_state(d)) return;
    d.resident = true;
    d.mode = GL_TRIANGLES;
    d.first = first;
    d.count = count;
    sShadowDraws.push_back(d);
}

GLuint shadow_link(const std::string& vsSrc, const std::string& fsSrc, const char* what) {
    GLuint vs = post_compile(GL_VERTEX_SHADER, vsSrc.c_str(), what);
    GLuint fs = vs ? post_compile(GL_FRAGMENT_SHADER, fsSrc.c_str(), what) : 0;
    GLuint program = 0;
    if (vs && fs) {
        program = glCreateProgram_ptr();
        bind_fixed_attrib_locations(program);
        glAttachShader_ptr(program, vs);
        glAttachShader_ptr(program, fs);
        glLinkProgram_ptr(program);
        GLint ok = 0;
        if (glGetProgramiv_ptr) glGetProgramiv_ptr(program, GL_LINK_STATUS, &ok);
        if (ok != GL_TRUE) {
            printf("[PC Port] %s failed to link; shadows are off\n", what);
            glDeleteProgram_ptr(program);
            program = 0;
        }
    }
    if (vs) glDeleteShader_ptr(vs);
    if (fs) glDeleteShader_ptr(fs);
    return program;
}

bool shadow_ensure_resources() {
    if (!glGenFramebuffers_ptr || !glFramebufferTexture2D_ptr || !glGenVertexArrays_ptr) return false;
    if (!sShadowDepthProgram) {
        sShadowDepthProgram = shadow_link(pc_post_build_shadow_depth_vertex(), pc_post_build_shadow_depth_fragment(), "shadow depth");
        if (!sShadowDepthProgram) return false;
    }
    if (!sShadowMaskProgram) {
        sShadowMaskProgram = shadow_link(pc_post_vertex_shader(), pc_post_build_shadow_mask_shader(), "shadow mask");
        if (!sShadowMaskProgram) return false;
    }
    const int size = std::max(256, sPostEffects.shadowMapSize);
    if (!sShadowFbo) {
        glGenFramebuffers_ptr(1, &sShadowFbo);
        glGenTextures(1, &sShadowDepthTex);
    }
    if (sShadowMapSize != size) {
        glBindTexture(GL_TEXTURE_2D, sShadowDepthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        // Sampler de comparación con filtrado lineal: cada tap ya devuelve una
        // comparación bilineal (PCF 2x2 en hardware); con el kernel 3x3 del
        // shader el borde sale suave en vez de a escalones.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sShadowFbo);
        glFramebufferTexture2D_ptr(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sShadowDepthTex, 0);
        // Sin adjunto de color: en GLES hay que declararlo explícitamente.
        if (glDrawBuffers_ptr) { GLenum none = GL_NONE; glDrawBuffers_ptr(1, &none); }
        sShadowMapSize = size;
    }
    if (!sShadowMaskFbo) {
        glGenFramebuffers_ptr(1, &sShadowMaskFbo);
        glGenTextures(1, &sShadowMaskTex);
    }
    if (sShadowMaskW != sRenderWidth || sShadowMaskH != sRenderHeight) {
        glBindTexture(GL_TEXTURE_2D, sShadowMaskTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, sRenderWidth, sRenderHeight, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sShadowMaskFbo);
        glFramebufferTexture2D_ptr(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sShadowMaskTex, 0);
        sShadowMaskW = sRenderWidth;
        sShadowMaskH = sRenderHeight;
    }
    if (!sShadowStreamVBO) {
        glGenBuffers_ptr(1, &sShadowStreamVBO);
        glGenVertexArrays_ptr(1, &sShadowStreamVAO);
        glBindVertexArray_ptr(sShadowStreamVAO);
        glBindBuffer_ptr(GL_ARRAY_BUFFER, sShadowStreamVBO);
        glEnableVertexAttribArray_ptr(kAttrPosIndex);
        glVertexAttribPointer_ptr(kAttrPosIndex, 3, GL_FLOAT, GL_FALSE, sizeof(ShadowVertex), (void*)offsetof(ShadowVertex, x));
        glEnableVertexAttribArray_ptr(kAttrTexCoord0Index);
        glVertexAttribPointer_ptr(kAttrTexCoord0Index, 2, GL_FLOAT, GL_FALSE, sizeof(ShadowVertex), (void*)offsetof(ShadowVertex, u));
        glEnableVertexAttribArray_ptr(kAttrMatrixSlotIndex);
        glVertexAttribPointer_ptr(kAttrMatrixSlotIndex, 1, GL_FLOAT, GL_FALSE, sizeof(ShadowVertex), (void*)offsetof(ShadowVertex, slot));
        glBindVertexArray_ptr(0);
    }
    if (!sShadowArenaVAO && sMeshArena) {
        glGenVertexArrays_ptr(1, &sShadowArenaVAO);
        glBindVertexArray_ptr(sShadowArenaVAO);
        glBindBuffer_ptr(GL_ARRAY_BUFFER, sMeshArena);
        glEnableVertexAttribArray_ptr(kAttrPosIndex);
        glVertexAttribPointer_ptr(kAttrPosIndex, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));
        glEnableVertexAttribArray_ptr(kAttrTexCoord0Index);
        glVertexAttribPointer_ptr(kAttrTexCoord0Index, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, tex));
        glEnableVertexAttribArray_ptr(kAttrMatrixSlotIndex);
        glVertexAttribPointer_ptr(kAttrMatrixSlotIndex, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, matrixSlot));
        glBindVertexArray_ptr(0);
    }
    glBindBuffer_ptr(GL_ARRAY_BUFFER, sVBO);
    return true;
}

// Matriz ortográfica de la luz (espacio de vista -> clip de la luz) que
// abarca el tramo cercano del frustum de la cámara.
void shadow_light_matrix(float out[16]) {
    const float range = sPostEffects.shadowRange;
    const float n = sViewNear, f = std::min(sViewFar, range);
    // Base de la luz: Z hacia la luz (la cámara de sombra mira -Z = hacia la escena).
    float lz[3] = { sShadowLightDir[0], sShadowLightDir[1], sShadowLightDir[2] };
    float up[3] = { 0.0f, 1.0f, 0.0f };
    if (std::fabs(lz[1]) > 0.9f) { up[0] = 1.0f; up[1] = 0.0f; }
    float lx[3] = { up[1] * lz[2] - up[2] * lz[1], up[2] * lz[0] - up[0] * lz[2], up[0] * lz[1] - up[1] * lz[0] };
    float lxl = std::sqrt(lx[0] * lx[0] + lx[1] * lx[1] + lx[2] * lx[2]);
    for (float& c : lx) c /= (lxl > 1e-5f ? lxl : 1.0f);
    float ly[3] = { lz[1] * lx[2] - lz[2] * lx[1], lz[2] * lx[0] - lz[0] * lx[2], lz[0] * lx[1] - lz[1] * lx[0] };
    // Esquinas del tramo del frustum en espacio de vista.
    float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f, minZ = 1e30f, maxZ = -1e30f;
    for (int i = 0; i < 8; i++) {
        const float z = (i < 4) ? n : f;
        const float sx = (i & 1) ? 1.0f : -1.0f, sy = (i & 2) ? 1.0f : -1.0f;
        const float p[3] = { sx * z * sViewInvP00, sy * z * sViewInvP11, -z };
        const float px = p[0] * lx[0] + p[1] * lx[1] + p[2] * lx[2];
        const float py = p[0] * ly[0] + p[1] * ly[1] + p[2] * ly[2];
        const float pz = p[0] * lz[0] + p[1] * lz[1] + p[2] * lz[2];
        minX = std::min(minX, px); maxX = std::max(maxX, px);
        minY = std::min(minY, py); maxY = std::max(maxY, py);
        minZ = std::min(minZ, pz); maxZ = std::max(maxZ, pz);
    }
    // Margen en profundidad para lo que proyecta desde fuera del frustum
    // (árboles altos por encima de la cámara).
    const float pad = (maxZ - minZ) * 2.0f + 500.0f;
    maxZ += pad;
    minZ -= 200.0f;
    // Ajuste a texel: evita que el borde de la sombra tiemble al mover la cámara.
    const float texel = (maxX - minX) / float(std::max(1, sShadowMapSize));
    minX = std::floor(minX / texel) * texel; maxX = std::floor(maxX / texel) * texel;
    minY = std::floor(minY / texel) * texel; maxY = std::floor(maxY / texel) * texel;
    // Ortográfica sobre la base: x' = dot(lx,p), y' = dot(ly,p), z' = -dot(lz,p) (mira hacia -lz).
    const float sxm = 2.0f / (maxX - minX), sym = 2.0f / (maxY - minY), szm = 2.0f / (maxZ - minZ);
    // columna-mayor
    out[0] = lx[0] * sxm; out[4] = lx[1] * sxm; out[8] = lx[2] * sxm;   out[12] = -(maxX + minX) / (maxX - minX);
    out[1] = ly[0] * sym; out[5] = ly[1] * sym; out[9] = ly[2] * sym;   out[13] = -(maxY + minY) / (maxY - minY);
    // z clip: puntos cercanos a la luz (pz grande) -> profundidad menor.
    out[2] = -lz[0] * szm; out[6] = -lz[1] * szm; out[10] = -lz[2] * szm; out[14] = (maxZ + minZ) / (maxZ - minZ);
    out[3] = 0.0f; out[7] = 0.0f; out[11] = 0.0f; out[15] = 1.0f;
}

// Dibuja el mapa y la máscara. Se llama desde post_apply con el mundo ya
// terminado y su profundidad en sNativeDepthTexture.
bool shadow_build() {
    sShadowMaskReady = false;
    if (!pc_post_shadows_active(sPostEffects) || sShadowDraws.empty() || !sShadowHaveLight) return false;
    if (!sDepthIsTexture || !sNativeDepthTexture) return false;
    if (!shadow_ensure_resources()) return false;

    float lightVP[16];
    shadow_light_matrix(lightVP);

    // 1) Mapa de profundidad desde la luz.
    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sShadowFbo);
    glViewport(0, 0, sShadowMapSize, sShadowMapSize);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
#if PIKI_USE_GLES
    glClearDepthf(1.0f);
#else
    glClearDepth(1.0f);
#endif
    glClear(GL_DEPTH_BUFFER_BIT);
    post_bind_program(sShadowDepthProgram);
    const GLint locVP = glGetUniformLocation_ptr(sShadowDepthProgram, "uLightVP");
    const GLint locPos = glGetUniformLocation_ptr(sShadowDepthProgram, "uPosMtx");
    const GLint locUsePal = glGetUniformLocation_ptr(sShadowDepthProgram, "uUsePalette");
    const GLint locPal = glGetUniformLocation_ptr(sShadowDepthProgram, "uPosPalette");
    const GLint locTex = glGetUniformLocation_ptr(sShadowDepthProgram, "uTex");
    const GLint locAT = glGetUniformLocation_ptr(sShadowDepthProgram, "uAlphaTest");
    const GLint locRef = glGetUniformLocation_ptr(sShadowDepthProgram, "uAlphaRef");
    glUniformMatrix4fv_ptr(locVP, 1, GL_FALSE, lightVP);
    glUniform1i_ptr(locTex, 0);
    if (glActiveTexture_ptr) glActiveTexture_ptr(GL_TEXTURE0);
    // Subida de la copia en streaming.
    if (!sShadowStream.empty()) {
        glBindBuffer_ptr(GL_ARRAY_BUFFER, sShadowStreamVBO);
        glBufferData_ptr(GL_ARRAY_BUFFER, GLsizeiptr(sShadowStream.size() * sizeof(ShadowVertex)), sShadowStream.data(), GL_STREAM_DRAW);
    }
    bool residentBound = false;
    GLuint lastTex = ~0u;
    int lastAT = -1;
    GXCullMode lastCull = GX_CULL_NONE;
    bool cullValid = false;
    for (const ShadowDraw& d : sShadowDraws) {
        if (d.resident && !sShadowArenaVAO) continue;
        if (d.resident != residentBound || (!residentBound && &d == &sShadowDraws.front())) {
            glBindVertexArray_ptr(d.resident ? sShadowArenaVAO : sShadowStreamVAO);
            residentBound = d.resident;
        }
        if (!cullValid || lastCull != d.cull) {
            if (d.cull == GX_CULL_NONE) glDisable(GL_CULL_FACE);
            else { glEnable(GL_CULL_FACE); glCullFace(d.cull == GX_CULL_FRONT ? GL_FRONT : GL_BACK); }
            lastCull = d.cull;
            cullValid = true;
        }
        glUniform1i_ptr(locUsePal, d.palette ? 1 : 0);
        if (d.palette) {
            glUniform4fv_ptr(locPal, d.paletteSlots * 3, &sShadowPalettes[d.paletteOffset]);
        } else {
            glUniformMatrix4fv_ptr(locPos, 1, GL_FALSE, d.mtx);
        }
        const int at = d.alphaTest ? 1 : 0;
        if (at != lastAT) { glUniform1i_ptr(locAT, at); lastAT = at; }
        if (d.alphaTest) {
            glUniform1f_ptr(locRef, d.alphaRef);
            if (d.tex != lastTex) { glBindTexture(GL_TEXTURE_2D, d.tex); lastTex = d.tex; }
        }
        glDrawArrays(d.mode, d.first, d.count);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_CULL_FACE);

    // 2) Máscara en pantalla.
    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sShadowMaskFbo);
    glViewport(0, 0, sShadowMaskW, sShadowMaskH);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray_ptr(sPostVAO);
    post_bind_program(sShadowMaskProgram);
    if (glActiveTexture_ptr) {
        glActiveTexture_ptr(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sNativeDepthTexture);
        glUniform1i_ptr(glGetUniformLocation_ptr(sShadowMaskProgram, "uDepth"), 0);
        glActiveTexture_ptr(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, sShadowDepthTex);
        glUniform1i_ptr(glGetUniformLocation_ptr(sShadowMaskProgram, "uShadowMap"), 1);
        glActiveTexture_ptr(GL_TEXTURE0);
    }
    glUniform4f_ptr(glGetUniformLocation_ptr(sShadowMaskProgram, "uProjInfo"), sViewInvP00, sViewInvP11, sViewNear, sViewFar);
    glUniformMatrix4fv_ptr(glGetUniformLocation_ptr(sShadowMaskProgram, "uLightVP"), 1, GL_FALSE, lightVP);
    glUniform4f_ptr(glGetUniformLocation_ptr(sShadowMaskProgram, "uShadowParams"),
                    sPostEffects.shadowStrength, 1.0f / float(sShadowMapSize), 0.0015f, sPostEffects.shadowRange);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray_ptr(0);
    glDepthMask(GL_TRUE);
    glBindBuffer_ptr(GL_ARRAY_BUFFER, sVBO);
    sBoundTextures[0] = 0;
    sShadowMaskReady = true;
    return true;
}

// Rayos de sol. Devuelve false (y el compuesto usa negro) si el sol queda
// detrás de la cámara o falta algo.
} // namespace

void pc_gfx_shadow_exclude(int on) {
    // El lote en curso pertenece al estado anterior: se cierra antes de cambiar.
    pc_gfx_flush_batch();
    sShadowExclude = on != 0;
}
