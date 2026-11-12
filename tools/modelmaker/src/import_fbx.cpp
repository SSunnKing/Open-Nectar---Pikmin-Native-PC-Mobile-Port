// FBX (Mixamo, Unity, Maya, 3ds Max...) con ufbx: mallas con piel, huesos y
// color base con su textura, en la pose que trae el archivo.

#include "import.h"

#include "lang.h"

#include "stb_image.h"
#include "ufbx.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <unordered_map>

namespace mm {

namespace fs = std::filesystem;

namespace {

constexpr int kImportTextureMax = 2048;

Vec3 toVec(const ufbx_vec3& v) { return { float(v.x), float(v.y), float(v.z) }; }

nectar::Image decode(const unsigned char* data, std::size_t size)
{
	nectar::Image img;
	int w = 0, h = 0, n = 0;
	unsigned char* px = size ? stbi_load_from_memory(data, int(size), &w, &h, &n, 4) : nullptr;
	if (!px) return img;
	img.resize(w, h);
	std::memcpy(img.rgba.data(), px, std::size_t(w) * h * 4);
	stbi_image_free(px);
	if (w > kImportTextureMax || h > kImportTextureMax) img = nectar::shrinkToFit(img, kImportTextureMax, kImportTextureMax);
	return img;
}

std::string str(const ufbx_string& s) { return std::string(s.data, s.length); }

// La textura: dentro del FBX, en su ruta absoluta, en la relativa o junto al
// archivo (lo normal cuando se descarga de Mixamo o Sketchfab).
nectar::Image loadTexture(const ufbx_texture* tex, const fs::path& dir, std::string& warnings, std::vector<std::string>& files)
{
	if (!tex) return {};
	if (tex->content.size) return decode(static_cast<const unsigned char*>(tex->content.data), tex->content.size);
	std::vector<fs::path> tries;
	const std::string abs = str(tex->absolute_filename), rel = str(tex->relative_filename), name = str(tex->filename);
	for (const std::string& p : { abs, rel, name }) {
		if (p.empty()) continue;
		std::string norm = p;
		for (char& c : norm)
			if (c == '\\') c = '/';
		tries.push_back(fs::u8path(norm));
		tries.push_back(dir / fs::u8path(norm));
		tries.push_back(dir / fs::u8path(norm).filename());
		tries.push_back(dir / "textures" / fs::u8path(norm).filename());
	}
	std::error_code ec;
	for (const fs::path& p : tries) {
		if (!fs::is_regular_file(p, ec)) continue;
		std::ifstream f(p, std::ios::binary);
		std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), {});
		nectar::Image img = decode(bytes.data(), bytes.size());
		if (!img.empty()) {
			files.push_back(p.u8string());
			return img;
		}
	}
	const std::string shown = !name.empty() ? name : !rel.empty() ? rel : abs;
	const std::string w     = std::string(L("falta la textura: ", "missing texture: ")) + fs::u8path(shown).filename().u8string();
	if (warnings.find(w) == std::string::npos) warnings += w + "\n";
	return {};
}

} // namespace

bool importFbx(const std::string& path, ImpModel& out, std::string& error)
{
	ufbx_load_opts opts           = {};
	opts.target_axes              = ufbx_axes_right_handed_y_up;
	opts.space_conversion         = UFBX_SPACE_CONVERSION_TRANSFORM_ROOT;
	opts.generate_missing_normals = true;
	ufbx_error err;
	ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &err);
	if (!scene) {
		error = std::string(L("no es un FBX válido (", "not a valid FBX file (")) + str(err.description) + ")";
		return false;
	}
	const fs::path dir = fs::u8path(path).parent_path();
	out.format         = "FBX";
	out.files.push_back(path);

	// Huesos: los que mueven alguna piel y los nodos "bone" (con sus padres
	// de esqueleto), en el orden de la escena (los padres antes).
	std::vector<char> isBone(scene->nodes.count, 0);
	for (std::size_t i = 0; i < scene->meshes.count; ++i) {
		const ufbx_mesh* mesh = scene->meshes.data[i];
		for (std::size_t s = 0; s < mesh->skin_deformers.count; ++s)
			for (std::size_t c = 0; c < mesh->skin_deformers.data[s]->clusters.count; ++c)
				if (const ufbx_node* b = mesh->skin_deformers.data[s]->clusters.data[c]->bone_node) isBone[b->typed_id] = 1;
	}
	for (std::size_t i = 0; i < scene->nodes.count; ++i)
		if (scene->nodes.data[i]->bone) isBone[i] = 1;
	std::vector<int> boneOfNode(scene->nodes.count, -1);
	for (std::size_t i = 0; i < scene->nodes.count; ++i) {
		if (!isBone[i]) continue;
		const ufbx_node* n = scene->nodes.data[i];
		boneOfNode[i]      = int(out.bones.size());
		ImpBone b;
		b.name = str(n->name);
		b.head = toVec(n->node_to_world.cols[3]);
		out.bones.push_back(b);
	}
	for (std::size_t i = 0; i < scene->nodes.count; ++i) {
		if (boneOfNode[i] < 0) continue;
		for (const ufbx_node* p = scene->nodes.data[i]->parent; p; p = p->parent) {
			if (boneOfNode[p->typed_id] >= 0) {
				out.bones[std::size_t(boneOfNode[i])].parent = boneOfNode[p->typed_id];
				break;
			}
		}
	}

	// Materiales de la escena.
	std::unordered_map<const ufbx_material*, int> materialIndex;
	for (std::size_t i = 0; i < scene->materials.count; ++i) {
		const ufbx_material* m = scene->materials.data[i];
		ImpMaterial mat;
		mat.name = str(m->name);
		const ufbx_material_map& base = m->pbr.base_color.has_value ? m->pbr.base_color : m->fbx.diffuse_color;
		if (base.has_value)
			for (int k = 0; k < 3; ++k) mat.color[k] = float(base.value_vec4.v[k]);
		const ufbx_texture* tex = m->pbr.base_color.texture ? m->pbr.base_color.texture : m->fbx.diffuse_color.texture;
		mat.texture             = loadTexture(tex, dir, out.warnings, out.files);
		// Muchos exportadores dejan el color base en negro o gris si hay textura.
		if (!mat.texture.empty()) mat.color[0] = mat.color[1] = mat.color[2] = 1.0f;
		const ufbx_material_map& opacity = m->pbr.opacity;
		if (opacity.has_value && opacity.value_real < 0.99) {
			mat.color[3] = float(opacity.value_real);
			mat.alpha    = ImpMaterial::Blend;
		}
		// Texturas con alfa (pelo, pestañas): recorte.
		if (mat.alpha == ImpMaterial::Opaque && !mat.texture.empty()) {
			std::size_t cut = 0;
			for (std::size_t p = 3; p < mat.texture.rgba.size(); p += 4) cut += mat.texture.rgba[p] < 128;
			if (cut * 50 > mat.texture.rgba.size() / 4) mat.alpha = ImpMaterial::Mask;
		}
		mat.doubleSided   = m->features.double_sided.enabled;
		materialIndex[m]  = int(out.materials.size());
		out.materials.push_back(std::move(mat));
	}
	const int defaultMaterial = int(out.materials.size());
	out.materials.push_back(ImpMaterial { "default" });

	std::vector<uint32_t> tri;
	for (std::size_t ni = 0; ni < scene->nodes.count; ++ni) {
		const ufbx_node* node = scene->nodes.data[ni];
		const ufbx_mesh* mesh = node->mesh;
		if (!mesh || !mesh->num_faces) continue;
		const ufbx_skin_deformer* skin = mesh->skin_deformers.count ? mesh->skin_deformers.data[0] : nullptr;
		const ufbx_matrix normalMtx    = ufbx_matrix_for_normals(&node->geometry_to_world);
		// Malla rígida colgada de un hueso: va con él.
		int rigidBone = -1;
		for (const ufbx_node* p = node; p && rigidBone < 0; p = p->parent) rigidBone = boneOfNode[p->typed_id];
		const bool mirrored = node->geometry_to_world.m00 * (node->geometry_to_world.m11 * node->geometry_to_world.m22 - node->geometry_to_world.m12 * node->geometry_to_world.m21)
		                        - node->geometry_to_world.m01 * (node->geometry_to_world.m10 * node->geometry_to_world.m22 - node->geometry_to_world.m12 * node->geometry_to_world.m20)
		                        + node->geometry_to_world.m02 * (node->geometry_to_world.m10 * node->geometry_to_world.m21 - node->geometry_to_world.m11 * node->geometry_to_world.m20)
		                    < 0;
		tri.resize(mesh->max_face_triangles * 3);
		for (std::size_t fi = 0; fi < mesh->num_faces; ++fi) {
			const ufbx_face face = mesh->faces.data[fi];
			const uint32_t n     = ufbx_triangulate_face(tri.data(), tri.size(), mesh, face);
			int material         = defaultMaterial;
			if (mesh->face_material.count > fi && mesh->materials.count) {
				const uint32_t mi = mesh->face_material.data[fi];
				// El material de la instancia (nodo) manda sobre el de la malla.
				const ufbx_material* m = mi < node->materials.count ? node->materials.data[mi] : mi < mesh->materials.count ? mesh->materials.data[mi] : nullptr;
				const auto it          = m ? materialIndex.find(m) : materialIndex.end();
				if (it != materialIndex.end()) material = it->second;
			}
			for (uint32_t t = 0; t < n; ++t) {
				SkinVertex tv[3];
				for (int k = 0; k < 3; ++k) {
					const uint32_t index  = tri[t * 3 + uint32_t(k)];
					const uint32_t vertex = mesh->vertex_indices.data[index];
					SkinVertex& o         = tv[k];
					const ufbx_vec3 p     = ufbx_get_vertex_vec3(&mesh->vertex_position, index);
					const ufbx_vec3 nn    = mesh->vertex_normal.exists ? ufbx_get_vertex_vec3(&mesh->vertex_normal, index) : ufbx_vec3 { { { 0, 1, 0 } } };
					if (mesh->vertex_uv.exists) {
						const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, index);
						o.uv               = { float(uv.x), 1.0f - float(uv.y) }; // FBX: origen abajo
					}
					if (mesh->vertex_color.exists) {
						const ufbx_vec4 c = ufbx_get_vertex_vec4(&mesh->vertex_color, index);
						for (int i = 0; i < 4; ++i) o.col[i] = float(c.v[i]);
						out.materials[std::size_t(material)].vertexColors = true;
					}
					o.weight[0] = 0;
					if (skin && vertex < skin->vertices.count) {
						// Posición con la piel (la pose que trae el archivo, la
						// misma que la de los huesos).
						const ufbx_matrix m  = ufbx_get_skin_vertex_matrix(skin, vertex, &node->geometry_to_world);
						const ufbx_matrix nm = ufbx_matrix_for_normals(&m);
						o.p                  = toVec(ufbx_transform_position(&m, p));
						o.n                  = normalize(toVec(ufbx_transform_direction(&nm, nn)));
						const ufbx_skin_vertex sv = skin->vertices.data[vertex];
						int used                  = 0;
						for (uint32_t w = 0; w < sv.num_weights && used < 4; ++w) {
							const ufbx_skin_weight sw = skin->weights.data[sv.weight_begin + w];
							if (sw.cluster_index >= skin->clusters.count || sw.weight <= 0) continue;
							const ufbx_node* bone = skin->clusters.data[sw.cluster_index]->bone_node;
							if (!bone || boneOfNode[bone->typed_id] < 0) continue;
							o.joint[used]  = boneOfNode[bone->typed_id];
							o.weight[used] = float(sw.weight);
							++used;
						}
					} else {
						o.p = toVec(ufbx_transform_position(&node->geometry_to_world, p));
						o.n = normalize(toVec(ufbx_transform_direction(&normalMtx, nn)));
						if (rigidBone >= 0) {
							o.joint[0]  = rigidBone;
							o.weight[0] = 1;
						}
					}
				}
				if (!skin && mirrored) std::swap(tv[1], tv[2]);
				out.verts.insert(out.verts.end(), tv, tv + 3);
				out.triMaterial.push_back(material);
			}
		}
	}
	ufbx_free_scene(scene);
	out.skinned = !out.bones.empty();
	if (out.verts.empty()) {
		error = L("el archivo no tiene triángulos", "no triangles found in the file");
		return false;
	}
	return true;
}

} // namespace mm
