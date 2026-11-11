#pragma once

// Dibujo de la vista 3D del editor (OpenGL 3.3): modelos .mod con su textura
// base y color de vértice (sin TEV: lo exacto se ve al probar en el juego),
// formas simples para marcadores y líneas.

#include "gl.h"
#include "math3d.h"

#include "nectar_assets/mod.h"

#include <memory>
#include <string>
#include <vector>

namespace nm {

struct Rgba {
	float r = 1, g = 1, b = 1, a = 1;
};

struct GpuModel {
	struct Part {
		GLuint vao = 0, vbo = 0;
		int count  = 0;
		GLuint tex = 0;
		bool cutout = false, blend = false;
		float colour[3] = { 1, 1, 1 }; ///< ModMaterial::shade
		bool texAdd     = false;       ///< ModMaterial::texAdditive
		bool envMap     = false;       ///< ModMaterial::envMap
	};
	std::vector<Part> parts;
	std::vector<GLuint> textures;
	V3 boundsMin, boundsMax;
	~GpuModel();
};

/// Destino de dibujo de la vista (se muestra como imagen dentro de ImGui).
struct RenderTarget {
	GLuint fbo = 0, color = 0, depth = 0;
	int width = 0, height = 0;
	void resize(int w, int h);
	~RenderTarget();
};

class Renderer {
public:
	bool init(std::string& error);

	std::shared_ptr<GpuModel> upload(const nectar::Model& model);

	void begin(RenderTarget& target, const M4& view, const M4& proj, const Rgba& clear);
	void end();

	void drawModel(const GpuModel& m, const M4& model, const Rgba& tint = Rgba());
	/// Cubo unidad centrado en el origen (escálalo con `model`).
	void drawBox(const M4& model, const Rgba& c);
	/// Cilindro de radio 1 y alto 1 apoyado en y = 0.
	void drawCylinder(const M4& model, const Rgba& c);
	void drawLines(const std::vector<V3>& points, const Rgba& c); ///< pares
	void drawTriangles(const std::vector<V3>& points, const Rgba& c, bool lit = true);

private:
	struct Vertex {
		float p[3], n[3], uv[2];
		unsigned char c[4];
	};
	GLuint program_ = 0;
	GLint uMvp_ = -1, uModel_ = -1, uTint_ = -1, uUseTex_ = -1, uCut_ = -1, uLit_ = -1, uTex_ = -1, uEnvMap_ = -1;
	GLuint boxVao_ = 0, boxVbo_ = 0, cylVao_ = 0, cylVbo_ = 0, dynVao_ = 0, dynVbo_ = 0;
	int boxCount_ = 0, cylCount_ = 0;
	M4 viewProj_;

	GLuint makeMesh(const std::vector<Vertex>& v, GLuint& vbo);
	void setup(const M4& model, const Rgba& tint, int tex, float cut, bool lit);
	void drawDynamic(const std::vector<Vertex>& v, GLenum mode);
};

/// Textura de una imagen (muestras de materiales). 0 si está vacía.
GLuint uploadTexture(const nectar::Image& img, bool repeat);

/// Textura RGBA desde un PNG (miniaturas del catálogo). 0 si falla.
GLuint loadPngTexture(const std::string& path, int* w = nullptr, int* h = nullptr);

} // namespace nm
