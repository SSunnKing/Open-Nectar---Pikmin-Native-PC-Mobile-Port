#include "app.h"

#include "lang.h"
#include "launcher_platform.h"
#include "simplify.h"

#include <SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

namespace mm {

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace {

bool sSpanish = false;

constexpr float kPi = 3.14159265358979f;

// Animaciones con nombre para mostrar, en el orden en que salen.
struct KnownAnim {
	const char* file;
	const char* es;
	const char* en;
};
const KnownAnim kKnownAnims[] = {
	{ "walk", "Andar", "Walk" },          { "run2", "Correr", "Run" },          { "wait", "Esperar", "Idle" },
	{ "throw", "Lanzar", "Throw" },       { "fue", "Silbar", "Whistle" },       { "nuku", "Arrancar", "Pluck" },
	{ "punch", "Puñetazo", "Punch" },     { "damage", "Daño", "Hurt" },         { "korobu", "Tropezar", "Trip" },
	{ "dead", "Caer KO", "Knocked out" }, { "jump", "Saltar", "Jump" },         { "oboreru", "Ahogarse", "Drown" },
	{ "akubi", "Bostezar", "Yawn" },      { "neru", "Dormir", "Sleep" },        { "chatting", "Charlar", "Chat" },
	{ "gakkari", "Decepción", "Let down" }, { "kaifuku", "Recuperarse", "Recover" }, { "sagasu", "Buscar", "Look around" },
};

const char* animLabel(const Anim& a)
{
	for (const KnownAnim& k : kKnownAnims)
		if (a.name == k.file) return L(k.es, k.en);
	return a.name.c_str();
}

// Color de cada articulación en la vista de pesos.
const float kJointColour[kJoints][3] = {
	{ 0.5f, 0.5f, 0.5f }, { 0.35f, 0.75f, 0.3f }, { 0.95f, 0.85f, 0.25f }, { 0.95f, 0.85f, 0.25f },
	{ 0.75f, 0.4f, 0.9f }, { 0.75f, 0.4f, 0.9f }, { 0.75f, 0.4f, 0.9f },  { 0.3f, 0.55f, 0.95f },
	{ 0.95f, 0.35f, 0.3f }, { 0.5f, 0.5f, 0.5f }, { 0.3f, 0.85f, 0.85f }, { 0.97f, 0.6f, 0.2f },
};

ImU32 levelColour(Check::Level l)
{
	switch (l) {
	case Check::Ok: return IM_COL32(120, 210, 90, 255);
	case Check::Warn: return IM_COL32(240, 200, 70, 255);
	default: return IM_COL32(235, 85, 70, 255);
	}
}

bool bigButton(const char* label, float width = -1, float height = 40)
{
	return ImGui::Button(label, ImVec2(width < 0 ? ImGui::GetContentRegionAvail().x : width, height));
}

void stepHeader(int n, const char* title)
{
	ImGui::Spacing();
	ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 205, 90, 255));
	ImGui::Text("%d  %s", n, title);
	ImGui::PopStyleColor();
	ImGui::Separator();
}

void hint(const char* text)
{
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	ImGui::TextWrapped("%s", text);
	ImGui::PopStyleColor();
}

} // namespace

void setSpanish(bool spanish) { sSpanish = spanish; }
bool spanish() { return sSpanish; }

App::~App()
{
	if (worker_.joinable()) worker_.join();
	for (GLuint t : modelTex_)
		if (t) glDeleteTextures(1, &t);
	for (GLuint t : olimarTex_)
		if (t) glDeleteTextures(1, &t);
	for (GLuint& t : startPortrait_)
		if (t) glDeleteTextures(1, &t);
}

bool App::init(const std::string& openPath, std::string& error)
{
	cfg_.load();
	setSpanish(cfg_.language == "es");
	if (!view_.init(error)) return false;
	std::string why;
	if (!setupGame(why)) {
		askGameDir_ = true;
		say(why, true);
	}
	if (!openPath.empty()) {
		showStart_ = false;
		loadModel(openPath, false);
	}
	return true;
}

bool App::setupGame(std::string& error)
{
	gameReady_ = false;
	std::string why;
	// El launcher nos abre en la carpeta del juego; si no, la del editor de mapas.
	std::string assets = nm::resolveAssetsDir((fs::current_path() / "assets").string(), why);
	if (assets.empty() && !cfg_.assetsDir.empty()) assets = nm::resolveAssetsDir(cfg_.assetsDir, why);
	if (assets.empty()) assets = nm::guessAssetsDir();
	if (assets.empty()) {
		error = L("No encuentro el juego. Elige la carpeta donde está instalado Pikmin.", "Can't find the game. Choose the folder where Pikmin is installed.");
		return false;
	}
	std::vector<Anim> anims;
	if (!loadAnimBundle(assets + "/dataDir/pikis/bluModel.anm", anims, error)) return false;
	anims_     = std::move(anims);
	assetsDir_ = assets;
	if (!loadReference(error)) return false;
	// Hoja, capullo y flor, para ver dónde los pondrá el juego.
	static const char* const kLeaves[3] = { "leaf", "bud", "flower" };
	for (int i = 0; i < 3; ++i) {
		std::string ignored;
		leaves_[i].load(assets + "/dataDir/pikis/happas/" + kLeaves[i] + ".mod", ignored);
		for (GLuint t : leafTex_[i])
			if (t) glDeleteTextures(1, &t);
		leafTex_[i].clear();
		for (const nectar::Image& img : leaves_[i].materialTex) leafTex_[i].push_back(uploadTexture(img, false));
	}
	// Las tarjetas de inicio son retratos de los modelos reales, no dibujos
	// aproximados. Si faltase un asset, la pantalla conserva su fallback.
	std::string portraitError;
	buildStartPortraits(portraitError);
	if (cfg_.assetsDir != assets) {
		cfg_.assetsDir = assets;
		cfg_.save();
	}
	animOrder_.clear();
	for (const KnownAnim& k : kKnownAnims)
		for (std::size_t i = 0; i < anims_.size(); ++i)
			if (anims_[i].name == k.file) animOrder_.push_back(int(i));
	for (std::size_t i = 0; i < anims_.size(); ++i)
		if (std::find(animOrder_.begin(), animOrder_.end(), int(i)) == animOrder_.end()) animOrder_.push_back(int(i));

	gameReady_ = true;
	refreshInstalled();
	resetCamera();
	return true;
}

// El modelo del juego que se reemplaza. Los tres Pikmin comparten esqueleto,
// pero no geometría: rojo tiene nariz, amarillo orejas y azul branquias.
bool App::loadReference(std::string& error)
{
	OlimarRig rig;
	const char* model = "pikis/nv3Model.mod";
	if (target_ == T_RED || target_ == T_ALL) model = "pikis/redModel.mod";
	else if (target_ == T_YELLOW) model = "pikis/yelModel.mod";
	else if (target_ == T_BLUE) model = "pikis/bluModel.mod";
	if (!rig.load(assetsDir_ + "/dataDir", model, error)) return false;
	olimar_ = std::move(rig);
	for (GLuint t : olimarTex_)
		if (t) glDeleteTextures(1, &t);
	olimarTex_.clear();
	for (const nectar::Image& img : olimar_.materialTex) olimarTex_.push_back(uploadTexture(img, true));
	return true;
}

// El color con el que el juego pinta a cada Pikmin (Piki::pikiColors); los
// tres a la vez se ven de rojo aquí y el juego tiñe cada uno del suyo.
void App::speciesColour(float out[3]) const
{
	static const float kColours[5][3] = { { 1, 1, 1 }, { 1.0f, 0.12f, 0.0f }, { 1.0f, 0.82f, 0.0f }, { 0.0f, 0.2f, 1.0f }, { 1.0f, 0.12f, 0.0f } };
	for (int i = 0; i < 3; ++i) out[i] = kColours[target_][i];
}

// Material liso para el tallo original (el del juego no tiene textura: se tiñe
// del color del Pikmin). Va el último, detrás de los del modelo.
static const char* const kStemMaterial = "stem (Pikmin)";
void App::ensureStemMaterial()
{
	if (!model_.materials.empty() && model_.materials.back().name == kStemMaterial) model_.materials.pop_back();
	fit_.stemMaterial = -1;
	if (isPikmin()) {
		ImpMaterial stem;
		stem.name = kStemMaterial;
		float c[3];
		speciesColour(c);
		// Para los tres colores, blanco: el juego lo tiñe.
		for (int i = 0; i < 3; ++i) stem.color[i] = target_ == T_ALL ? 1.0f : c[i];
		fit_.stemMaterial = int(model_.materials.size());
		model_.materials.push_back(stem);
	}
	exportOpt_.noTint.resize(model_.materials.size(), 0);
	exportOpt_.pikmin = isPikmin();
}

void App::setTarget(int target)
{
	if (target == target_ || !gameReady_) {
		target_ = target;
		return;
	}
	const bool wasPikmin = isPikmin();
	target_              = target;
	std::string error;
	if (!loadReference(error)) say(error, true);
	fit_.pikmin = isPikmin();
	// Presupuesto de triángulos: un capitán o hasta 100 Pikmin a la vez.
	if (wasPikmin != isPikmin()) targetTris_ = isPikmin() ? 1500 : 30000;
	refreshInstalled();
	if (!hasModel_) {
		resetCamera();
		return;
	}
	ensureStemMaterial();
	autoFit(model_, olimar_, fit_);
	if (std::size_t(targetTris_) < model_.triangles() || source_.triangles() > model_.triangles()) {
		resimplify(); // termina en finishLoad, que vuelve a montar todo
	} else {
		uploadTextures();
		rebuild();
	}
	resetCamera();
}

std::string App::customDir() const { return (fs::path(cfg_.gameDir()) / "Load" / "Models" / "Custom").string(); }
std::string App::projectPath() const { return (fs::path(customDir()) / "olimar_project.json").string(); }

void App::say(const std::string& text, bool error)
{
	status_      = text;
	statusError_ = error;
}

// Por encima de esto se simplifica solo al abrir.
constexpr std::size_t kAutoSimplify = 40000;

void App::loadModel(const std::string& path, bool restoreProject)
{
	if (!gameReady_) {
		say(L("Primero elige la carpeta del juego.", "Choose the game folder first."), true);
		return;
	}
	if (working_) return;
	if (worker_.joinable()) worker_.join();
	pendingPath_       = path;
	pendingRestore_    = restoreProject;
	pendingResimplify_ = false;
	working_           = true;
	workDone_          = false;
	const std::size_t target = std::size_t(targetTris_);
	const std::size_t limit  = isPikmin() ? 3000 : kAutoSimplify;
	worker_ = std::thread([this, path, target, limit] {
		ImpModel m;
		std::string error;
		if (importModel(path, m, error)) {
			pendingSource_ = m;
			if (m.triangles() > limit) simplifyModel(m, target);
		}
		pending_      = std::move(m);
		pendingError_ = error;
		workDone_     = true;
	});
}

void App::resimplify()
{
	if (working_ || !hasModel_) return;
	if (worker_.joinable()) worker_.join();
	pendingResimplify_ = true;
	working_           = true;
	workDone_          = false;
	pendingSource_     = source_;
	const std::size_t target = std::size_t(targetTris_);
	worker_ = std::thread([this, target] {
		ImpModel m = pendingSource_;
		simplifyModel(m, target);
		pending_      = std::move(m);
		pendingError_.clear();
		workDone_ = true;
	});
}

void App::finishLoad()
{
	if (worker_.joinable()) worker_.join();
	working_ = false;
	if (!pendingError_.empty()) {
		say(std::string(L("No se pudo abrir: ", "Could not open: ")) + pendingError_, true);
		return;
	}
	if (pendingResimplify_) {
		// Misma colocación y puntos: solo cambia la malla.
		model_ = std::move(pending_);
		ensureStemMaterial();
		uploadTextures();
		rebuild();
		say(std::to_string(model_.triangles()) + L(" triángulos.", " triangles."));
		return;
	}
	const std::string path     = pendingPath_;
	const bool restoreProject  = pendingRestore_;
	model_    = std::move(pending_);
	source_   = std::move(pendingSource_);
	hasModel_ = true;
	// Nombre del personaje: el del archivo, hasta que el usuario ponga otro.
	std::snprintf(charName_, sizeof(charName_), "%s", fs::u8path(path).stem().u8string().c_str());
	fit_                 = FitSettings();
	fit_.useSkeleton     = model_.skinned;
	fit_.pikmin          = isPikmin();
	fit_.keepPlacement   = model_.format == "NHM";
	exportOpt_.noTint.assign(model_.materials.size(), 0);
	ensureStemMaterial();
	autoFit(model_, olimar_, fit_);
	exportOpt_.textureMax = 1024;
	if (restoreProject) {
		// Los ajustes guardados con el proyecto (el de un personaje que se
		// vuelve a editar, o el de la última sesión).
		std::string text = restoreJson_;
		if (text.empty()) {
			std::ifstream f(fs::u8path(projectPath()));
			text.assign(std::istreambuf_iterator<char>(f), {});
		}
		restoreJson_.clear();
		try {
			const json j = json::parse(text);
			targetTris_         = j.value("targetTris", targetTris_);
			fit_.scale          = j.value("scale", 1.0f);
			fit_.quarter        = j.value("quarter", 0);
			fit_.lyingDown      = j.value("lyingDown", false);
			fit_.offsetY        = j.value("offsetY", 0.0f);
			fit_.offsetZ        = j.value("offsetZ", 0.0f);
			fit_.useSkeleton    = j.value("useSkeleton", model_.skinned) && model_.skinned;
			fit_.straightenArms = j.value("straightenArms", true);
			fit_.straightenLegs = j.value("straightenLegs", false);
			fit_.symmetry       = j.value("symmetry", true);
			fit_.markersCustom  = j.value("markersCustom", false);
			fit_.addStem        = j.value("addStem", true);
			exportOpt_.textureMax = j.value("textureMax", 1024);
			std::snprintf(charName_, sizeof(charName_), "%s", j.value("charName", std::string(charName_)).c_str());
			std::snprintf(charAuthor_, sizeof(charAuthor_), "%s", j.value("charAuthor", std::string()).c_str());
			if (fit_.markersCustom && j.contains("markers") && j["markers"].size() == M_COUNT) {
				for (int i = 0; i < M_COUNT; ++i)
					fit_.markers[std::size_t(i)] = { j["markers"][std::size_t(i)][0].get<float>(), j["markers"][std::size_t(i)][1].get<float>(),
						                             j["markers"][std::size_t(i)][2].get<float>() };
			} else {
				fit_.markers = estimateMarkers(model_, olimar_, fit_);
			}
			const std::vector<int> nt = j.value("noTint", std::vector<int>());
			for (std::size_t i = 0; i < nt.size() && i < exportOpt_.noTint.size(); ++i) exportOpt_.noTint[i] = char(nt[i] != 0);
		} catch (const std::exception&) {
		}
		if (editingId_.empty() && !editingOlimar_) say(L("Seguimos con tu modelo de la última vez.", "Picking up your model from last time."));
		else say(std::string(L("Editando \"", "Editing \"")) + editingName_ + L("\": al instalar se reemplaza.", "\": installing replaces it."));
	} else if (model_.format == "NHM") {
		// Sin proyecto: el pack tal cual, ya colocado y con sus pivotes; los
		// brazos ya van como los de Olimar.
		fit_.keepPlacement  = true;
		fit_.straightenArms = false;
		fit_.addStem        = false;
		fit_.markers        = estimateMarkers(model_, olimar_, fit_);
		say(std::string(L("Editando \"", "Editing \"")) + editingName_
		    + L("\" desde el modelo guardado (no tenía el original): se pueden retocar los puntos y la colocación.",
		        "\" from the saved model (it had no original): the dots and placement can be adjusted."));
	} else {
		say(std::string(L("Listo: ", "Loaded: ")) + fs::u8path(path).filename().u8string());
	}
	// Por defecto, andando: se ve enseguida cómo queda.
	anim_ = -1;
	for (std::size_t i = 0; i < anims_.size(); ++i)
		if (anims_[i].name == "walk") anim_ = int(i);
	frame_ = 0;
	uploadTextures();
	rebuild();
	resetCamera();
	// Tras importar, el siguiente trabajo útil es revisar escala y orientación.
	editorStep_ = 1;
}

void App::uploadTextures()
{
	for (GLuint t : modelTex_)
		if (t) glDeleteTextures(1, &t);
	modelTex_.clear();
	for (const ImpMaterial& m : model_.materials) {
		bool repeat = true;
		modelTex_.push_back(m.texture.empty() ? 0 : uploadTexture(m.texture, repeat));
	}
}

void App::rebuild()
{
	if (!hasModel_) return;
	rig_          = buildRig(model_, olimar_, fit_);
	// Triángulos por material (los del modelo y, en un Pikmin, el tallo añadido).
	order_.clear();
	for (std::size_t t = 0; t < rig_.triMaterial.size(); ++t) order_.push_back(t);
	std::stable_sort(order_.begin(), order_.end(), [&](std::size_t a, std::size_t b) { return rig_.triMaterial[a] < rig_.triMaterial[b]; });
	rigDirty_     = false;
	exportDirty_  = true;
	lastRebuild_  = ImGui::GetTime();
}

void App::resetCamera()
{
	const float h = hasModel_ ? std::max(rig_.max.y - rig_.min.y, olimar_.bodyTop) : olimar_.max.y - olimar_.min.y;
	cam_.target   = { olimarMode_ == 1 && hasModel_ ? -(olimar_.max.x - olimar_.min.x) * 0.6f : 0.0f, h * 0.5f, 0 };
	cam_.distance = h * 2.4f + 10.0f;
	cam_.yaw      = 0.35f;
	cam_.pitch    = 0.15f;
}

// Vértices de las dos mallas para este fotograma (con la piel en la CPU).
void App::fillMeshes()
{
	std::vector<Mat34> world, skin, olimarSkin(kJoints);
	const bool animated = anim_ >= 0 && std::size_t(anim_) < anims_.size();
	if (animated) anims_[std::size_t(anim_)].pose(frame_, olimar_.parent, world);
	else world = olimar_.bind;
	for (int j = 0; j < kJoints; ++j) olimarSkin[std::size_t(j)] = world[std::size_t(j)] * inverse(olimar_.bind[std::size_t(j)]);

	// Olimar: al lado (a la izquierda de la pantalla) o encima.
	const float gap    = 3.0f;
	const float offset = olimarMode_ == 1 && hasModel_ ? rig_.min.x - olimar_.max.x - gap : 0.0f;
	olimarMesh_.data.clear();
	olimarMesh_.ranges.clear();
	if (olimarMode_ != 0) {
		int lastMat = -2;
		for (std::size_t t = 0; t < olimar_.triMaterial.size(); ++t) {
			const int mat = olimar_.triMaterial[t];
			if (mat != lastMat) {
				DynamicMesh::Range r;
				r.first   = int(olimarMesh_.data.size());
				r.texture = std::size_t(mat) < olimarTex_.size() ? olimarTex_[std::size_t(mat)] : 0;
				olimarMesh_.ranges.push_back(r);
				lastMat = mat;
			}
			nectar::Rgba8 mc = std::size_t(mat) < olimar_.materialColour.size() ? olimar_.materialColour[std::size_t(mat)] : nectar::Rgba8 {};
			if (isPikmin()) {
				// Los Pikmin del juego son grises: los pinta el color de su especie.
				float sc[3];
				speciesColour(sc);
				mc.r = std::uint8_t(mc.r * sc[0]), mc.g = std::uint8_t(mc.g * sc[1]), mc.b = std::uint8_t(mc.b * sc[2]);
			}
			for (int k = 0; k < 3; ++k) {
				const SkinVertex& v = olimar_.verts[t * 3 + std::size_t(k)];
				Vec3 p, n;
				for (int i = 0; i < 4; ++i) {
					if (v.weight[i] <= 0) continue;
					p += olimarSkin[std::size_t(v.joint[i])].point(v.p) * v.weight[i];
					n += olimarSkin[std::size_t(v.joint[i])].dir(v.n) * v.weight[i];
				}
				n = normalize(n);
				GpuVertex g {};
				g.p[0] = p.x + offset, g.p[1] = p.y, g.p[2] = p.z;
				g.n[0] = n.x, g.n[1] = n.y, g.n[2] = n.z;
				g.uv[0] = v.uv.x, g.uv[1] = v.uv.y;
				g.c[0] = v.col[0] * mc.r / 255.0f, g.c[1] = v.col[1] * mc.g / 255.0f, g.c[2] = v.col[2] * mc.b / 255.0f, g.c[3] = 1;
				olimarMesh_.data.push_back(g);
			}
			olimarMesh_.ranges.back().count = int(olimarMesh_.data.size()) - olimarMesh_.ranges.back().first;
		}
		olimarMesh_.upload();
	}

	// Hoja de los Pikmin: en la articulación 6 de la referencia y, con los
	// pivotes propios, en la punta del tallo del modelo.
	leafMesh_.data.clear();
	leafMesh_.ranges.clear();
	const float white[3] = { 1, 1, 1 };
	if (isPikmin() && olimarMode_ != 0) appendLeaf(leafMesh_, world[J_ANT3], offset, white);

	// El modelo: en reposo sin enderezar (para colocar los puntos) o animado
	// como en el juego.
	if (!hasModel_ || rig_.verts.empty()) {
		modelMesh_.data.clear();
		modelMesh_.ranges.clear();
		leafMesh_.upload();
		return;
	}
	if (animated) retargetSkin(olimar_, rig_.pivots, world, skin);
	fillModelMesh(modelMesh_, animated ? &skin : nullptr, !animated, showWeights_);
	if (isPikmin()) {
		Mat34 tip = olimar_.bind[J_ANT3];
		setTranslation(tip, rig_.pivots[J_ANT3]);
		if (animated) tip = skin[J_ANT3] * tip;
		appendLeaf(leafMesh_, tip, 0.0f, white);
	}
	leafMesh_.upload();
}

void App::appendLeaf(DynamicMesh& mesh, const Mat34& world, float offsetX, const float tint[3])
{
	const int kind        = std::clamp(leafKind_, 0, 2);
	const LeafModel& leaf = leaves_[kind];
	int lastMat           = -2;
	for (std::size_t t = 0; t < leaf.triMaterial.size(); ++t) {
		const int mat = leaf.triMaterial[t];
		if (mat != lastMat || mesh.ranges.empty()) {
			DynamicMesh::Range r;
			r.first   = int(mesh.data.size());
			r.texture = mat >= 0 && std::size_t(mat) < leafTex_[kind].size() ? leafTex_[kind][std::size_t(mat)] : 0;
			mesh.ranges.push_back(r);
			lastMat = mat;
		}
		const nectar::Rgba8 mc = mat >= 0 && std::size_t(mat) < leaf.materialColour.size() ? leaf.materialColour[std::size_t(mat)] : nectar::Rgba8 {};
		for (int k = 0; k < 3; ++k) {
			const SkinVertex& v = leaf.verts[t * 3 + std::size_t(k)];
			const Vec3 p = world.point(v.p), n = normalize(world.dir(v.n));
			GpuVertex g {};
			g.p[0] = p.x + offsetX, g.p[1] = p.y, g.p[2] = p.z;
			g.n[0] = n.x, g.n[1] = n.y, g.n[2] = n.z;
			g.uv[0] = v.uv.x, g.uv[1] = v.uv.y;
			g.c[0] = v.col[0] * mc.r / 255.0f * tint[0], g.c[1] = v.col[1] * mc.g / 255.0f * tint[1], g.c[2] = v.col[2] * mc.b / 255.0f * tint[2];
			g.c[3] = 1;
			mesh.data.push_back(g);
		}
		mesh.ranges.back().count = int(mesh.data.size()) - mesh.ranges.back().first;
	}
}

bool App::buildStartPortraits(std::string& error)
{
	for (GLuint& portrait : startPortrait_) {
		if (portrait) glDeleteTextures(1, &portrait);
		portrait = 0;
	}
	OlimarRig captain, redPikmin, yellowPikmin, bluePikmin;
	if (!captain.load(assetsDir_ + "/dataDir", "pikis/nv3Model.mod", error)
	    || !redPikmin.load(assetsDir_ + "/dataDir", "pikis/redModel.mod", error)
	    || !yellowPikmin.load(assetsDir_ + "/dataDir", "pikis/yelModel.mod", error)
	    || !bluePikmin.load(assetsDir_ + "/dataDir", "pikis/bluModel.mod", error))
		return false;

	auto portrait = [&](int slot, const OlimarRig& source, const float tint[3], bool addLeaf) {
		std::vector<GLuint> textures;
		for (const nectar::Image& img : source.materialTex) textures.push_back(uploadTexture(img, true));
		DynamicMesh mesh;
		int lastMat = -2;
		for (std::size_t tri = 0; tri < source.triMaterial.size(); ++tri) {
			const int mat = source.triMaterial[tri];
			if (mat != lastMat) {
				DynamicMesh::Range range;
				range.first   = int(mesh.data.size());
				range.texture = mat >= 0 && std::size_t(mat) < textures.size() ? textures[std::size_t(mat)] : 0;
				mesh.ranges.push_back(range);
				lastMat = mat;
			}
			const nectar::Rgba8 mc = mat >= 0 && std::size_t(mat) < source.materialColour.size() ? source.materialColour[std::size_t(mat)] : nectar::Rgba8 {};
			for (int k = 0; k < 3; ++k) {
				const SkinVertex& v = source.verts[tri * 3 + std::size_t(k)];
				GpuVertex g {};
				g.p[0] = v.p.x, g.p[1] = v.p.y, g.p[2] = v.p.z;
				g.n[0] = v.n.x, g.n[1] = v.n.y, g.n[2] = v.n.z;
				g.uv[0] = v.uv.x, g.uv[1] = v.uv.y;
				g.c[0] = v.col[0] * mc.r / 255.0f * tint[0];
				g.c[1] = v.col[1] * mc.g / 255.0f * tint[1];
				g.c[2] = v.col[2] * mc.b / 255.0f * tint[2];
				g.c[3] = v.col[3];
				mesh.data.push_back(g);
			}
			mesh.ranges.back().count = int(mesh.data.size()) - mesh.ranges.back().first;
		}
		if (addLeaf && source.bind.size() > J_ANT3) {
			const float white[3] = { 1, 1, 1 };
			appendLeaf(mesh, source.bind[J_ANT3], 0.0f, white);
		}
		mesh.upload();

		Vec3 mn { 1e9f, 1e9f, 1e9f }, mx { -1e9f, -1e9f, -1e9f };
		for (const GpuVertex& v : mesh.data) {
			mn.x = std::min(mn.x, v.p[0]), mn.y = std::min(mn.y, v.p[1]), mn.z = std::min(mn.z, v.p[2]);
			mx.x = std::max(mx.x, v.p[0]), mx.y = std::max(mx.y, v.p[1]), mx.z = std::max(mx.z, v.p[2]);
		}
		const float h = std::max(mx.y - mn.y, 1.0f);
		const float w = std::max({ mx.x - mn.x, mx.z - mn.z, 1.0f });
		OrbitCamera camera;
		camera.target   = (mn + mx) * 0.5f;
		camera.yaw      = 0.20f;
		camera.pitch    = 0.04f;
		camera.fovY     = 0.58f;
		camera.distance = std::max(h, w) * 0.58f / std::tan(camera.fovY * 0.5f) + std::max(h, w) * 0.12f;
		nectar::Image image;
		if (view_.renderImage(384, 384, camera.projection(1.0f) * camera.view(), mesh, image)) startPortrait_[slot] = uploadTexture(image, false);
		for (GLuint texture : textures)
			if (texture) glDeleteTextures(1, &texture);
	};
	const float white[3]  = { 1, 1, 1 };
	const float red[3]    = { 1.0f, 0.12f, 0.0f };
	const float yellow[3] = { 1.0f, 0.82f, 0.0f };
	const float blue[3]   = { 0.0f, 0.2f, 1.0f };
	portrait(0, captain, white, false);
	portrait(1, redPikmin, red, false);
	portrait(2, yellowPikmin, yellow, false);
	portrait(3, bluePikmin, blue, false);
	return startPortrait_[0] && startPortrait_[1] && startPortrait_[2] && startPortrait_[3];
}

// `skin` nulo: en reposo (enderezado o, con `unbent`, como se importó).
void App::fillModelMesh(DynamicMesh& mesh, const std::vector<Mat34>* skin, bool unbent, bool weights)
{
	mesh.data.clear();
	mesh.ranges.clear();
	int lastMat = -2;
	for (std::size_t t : order_) {
		if (t * 3 + 2 >= rig_.verts.size()) continue;
		const int mat         = rig_.triMaterial[t];
		const ImpMaterial& im = model_.materials[std::size_t(mat)];
		if (mat != lastMat) {
			DynamicMesh::Range r;
			r.first   = int(mesh.data.size());
			r.texture = weights ? 0 : modelTex_[std::size_t(mat)];
			r.blend   = im.alpha == ImpMaterial::Blend && !weights;
			mesh.ranges.push_back(r);
			lastMat = mat;
		}
		for (int k = 0; k < 3; ++k) {
			const SkinVertex& v = rig_.verts[t * 3 + std::size_t(k)];
			Vec3 p = v.p, n = v.n;
			if (skin) {
				p = n = Vec3 {};
				for (int i = 0; i < 4; ++i) {
					if (v.weight[i] <= 0) continue;
					p += (*skin)[std::size_t(v.joint[i])].point(v.p) * v.weight[i];
					n += (*skin)[std::size_t(v.joint[i])].dir(v.n) * v.weight[i];
				}
				n = normalize(n);
			} else if (unbent && t * 3 + std::size_t(k) < rig_.unbent.size()) {
				p = rig_.unbent[t * 3 + std::size_t(k)];
			}
			GpuVertex g {};
			g.p[0] = p.x, g.p[1] = p.y, g.p[2] = p.z;
			g.n[0] = n.x, g.n[1] = n.y, g.n[2] = n.z;
			g.uv[0] = v.uv.x, g.uv[1] = v.uv.y;
			if (weights) {
				for (int c = 0; c < 3; ++c) {
					g.c[c] = 0;
					for (int i = 0; i < 4; ++i) g.c[c] += kJointColour[v.joint[i]][c] * v.weight[i];
				}
				g.c[3] = 1;
			} else {
				const bool useVertexColour = im.texture.empty() && im.vertexColors;
				for (int c = 0; c < 4; ++c) g.c[c] = im.color[c] * (useVertexColour ? v.col[c] : 1.0f);
				if (im.alpha == ImpMaterial::Opaque) g.c[3] = 1;
			}
			mesh.data.push_back(g);
		}
		mesh.ranges.back().count = int(mesh.data.size()) - mesh.ranges.back().first;
	}
	mesh.upload();
}

void App::frame()
{
	ImGuiIO& io = ImGui::GetIO();
	if (working_ && workDone_) finishLoad();
	if (playing_ && anim_ >= 0 && std::size_t(anim_) < anims_.size()) {
		frame_ += io.DeltaTime * 30.0f * speed_;
		const float frames = float(anims_[std::size_t(anim_)].frames);
		if (frames > 0) frame_ = std::fmod(frame_, frames);
	}
	// Reconstruir cuando se suelta el ratón (o cada poco mientras se arrastra).
	if (rigDirty_ && (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::GetTime() - lastRebuild_ > 0.2)) rebuild();
	if (exportDirty_ && hasModel_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		export_      = buildParts(model_, rig_, olimar_, fit_, exportOpt_);
		exportDirty_ = false;
	}

	const ImGuiViewport* vp = ImGui::GetMainViewport();
	if (showStart_) {
		drawStartScreen(vp->WorkSize.x, vp->WorkSize.y);
	} else {
		const float topH       = 64.0f;
		const float workflowW  = std::clamp(vp->WorkSize.x * 0.13f, 205.0f, 250.0f);
		const float inspectorW = std::clamp(vp->WorkSize.x * 0.22f, 350.0f, 430.0f);
		drawTopBar(vp->WorkSize.x);
		drawWorkflow(vp->WorkPos.x, vp->WorkPos.y + topH, workflowW, vp->WorkSize.y - topH);
		drawPanel(vp->WorkPos.x + vp->WorkSize.x - inspectorW, vp->WorkPos.y + topH, inspectorW, vp->WorkSize.y - topH);
		drawView(vp->WorkPos.x + workflowW, vp->WorkPos.y + topH, vp->WorkSize.x - workflowW - inspectorW, vp->WorkSize.y - topH);
	}

	if (working_) {
		ImGui::OpenPopup("##working");
	}
	if (ImGui::BeginPopupModal("##working", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
		static const char* dots[] = { "", ".", "..", "..." };
		ImGui::Text("%s%s", L("Abriendo y simplificando el modelo", "Opening and simplifying the model"), dots[int(ImGui::GetTime() * 3) % 4]);
		hint(L("Los modelos muy detallados tardan un poco.", "Very detailed models take a moment."));
		if (!working_) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	if (askGameDir_) {
		ImGui::OpenPopup("##gamedir");
		askGameDir_ = false;
	}
	if (ImGui::BeginPopupModal("##gamedir", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
		ImGui::TextUnformatted(L("¿Dónde está el juego?", "Where is the game?"));
		ImGui::Spacing();
		ImGui::PushTextWrapPos(420);
		hint(L("Abre el creador desde el launcher o elige la carpeta donde instalaste Pikmin (la que tiene \"assets\").",
		       "Open the model maker from the launcher, or choose the folder where Pikmin is installed (the one with \"assets\")."));
		ImGui::PopTextWrapPos();
		if (!status_.empty() && statusError_) ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1), "%s", status_.c_str());
		if (bigButton(L("Elegir carpeta…", "Choose folder…"), 420)) {
			const fs::path p = pikmin::launcher::platform::askForInstallDirectory(L("Carpeta de Pikmin", "Pikmin folder"));
			if (!p.empty()) {
				std::string why;
				const std::string assets = nm::resolveAssetsDir(p.string(), why);
				if (assets.empty()) {
					say(why, true);
				} else {
					cfg_.assetsDir = assets;
					cfg_.save();
					std::string err;
					if (setupGame(err)) {
						say(L("Juego encontrado.", "Game found."));
						ImGui::CloseCurrentPopup();
					} else {
						say(err, true);
					}
				}
			}
		}
		if (ImGui::Button(L("Salir", "Quit"), ImVec2(420, 0))) quit_ = true;
		ImGui::EndPopup();
	}
}

void App::drawStartScreen(float width, float height)
{
	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->WorkPos);
	ImGui::SetNextWindowSize(ImVec2(width, height));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("##start", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	ImGui::PopStyleVar();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 origin = ImGui::GetWindowPos();
	dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(24, 29, 27, 255));
	// Una mancha suave separa el contenido del fondo sin competir con las tarjetas.
	dl->AddCircleFilled(ImVec2(origin.x + width * 0.5f, origin.y + height * 0.43f), std::min(width, height) * 0.48f, IM_COL32(42, 56, 42, 150), 96);

	const float contentW = std::min(width - 80.0f, 1120.0f);
	ImGui::SetCursorPos(ImVec2((width - contentW) * 0.5f, std::max(42.0f, height * 0.08f)));
	ImGui::BeginGroup();
	ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(181, 222, 92, 255));
	ImGui::SetWindowFontScale(1.65f);
	ImGui::TextUnformatted("NECTAR  /  MODEL MAKER");
	ImGui::SetWindowFontScale(1.0f);
	ImGui::PopStyleColor();
	ImGui::Spacing();
	ImGui::SetWindowFontScale(2.05f);
	ImGui::TextUnformatted(L("¿Qué personaje quieres crear?", "Which character do you want to create?"));
	ImGui::SetWindowFontScale(1.0f);
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	ImGui::TextUnformatted(L("Elige una base. Podrás importar tu modelo y probar todas sus animaciones.",
	                          "Choose a base. You can import your model and test all of its animations."));
	ImGui::PopStyleColor();
	ImGui::Dummy(ImVec2(0, 26));

	const char* names[4] = { "Olimar", L("Pikmin rojo", "Red Pikmin"), L("Pikmin amarillo", "Yellow Pikmin"), L("Pikmin azul", "Blue Pikmin") };
	const char* desc[4] = { L("Capitán", "Captain"), L("Fuego", "Fire"), L("Electricidad", "Electricity"), L("Agua", "Water") };
	const ImU32 colours[4] = { IM_COL32(222, 217, 174, 255), IM_COL32(225, 75, 62, 255), IM_COL32(242, 202, 70, 255), IM_COL32(67, 132, 230, 255) };
	const int targets[4] = { T_CAPTAIN, T_RED, T_YELLOW, T_BLUE };
	const float gap = 16.0f;
	const float cardW = (contentW - gap * 3.0f) * 0.25f;
	const float cardH = std::clamp(height * 0.32f, 230.0f, 300.0f);
	for (int i = 0; i < 4; ++i) {
		ImGui::PushID(i);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("card", ImVec2(cardW, cardH));
		const bool hot = ImGui::IsItemHovered();
		const bool pressed = ImGui::IsItemClicked();
		dl->AddRectFilled(p, ImVec2(p.x + cardW, p.y + cardH), hot ? IM_COL32(61, 72, 61, 255) : IM_COL32(45, 53, 47, 255), 16);
		dl->AddRect(p, ImVec2(p.x + cardW, p.y + cardH), hot ? colours[i] : IM_COL32(83, 98, 84, 255), 16, 0, hot ? 2.5f : 1.0f);
		const ImVec2 c(p.x + cardW * 0.5f, p.y + cardH * 0.40f);
		if (startPortrait_[i]) {
			const float portraitSize = std::min(cardW - 28.0f, cardH - 82.0f);
			const ImVec2 a(p.x + (cardW - portraitSize) * 0.5f, p.y + 10.0f);
			dl->AddImage(ImTextureID(std::intptr_t(startPortrait_[i])), a, ImVec2(a.x + portraitSize, a.y + portraitSize));
		} else if (i == 0) {
			// Fallback por si la instalación del juego está incompleta.
			dl->AddCircleFilled(ImVec2(c.x, c.y - 20), 43, colours[i], 48);
			dl->AddRectFilled(ImVec2(c.x - 48, c.y - 35), ImVec2(c.x + 48, c.y - 5), IM_COL32(184, 208, 219, 255), 18);
			dl->AddCircleFilled(ImVec2(c.x - 17, c.y - 20), 7, IM_COL32(37, 45, 42, 255));
			dl->AddCircleFilled(ImVec2(c.x + 17, c.y - 20), 7, IM_COL32(37, 45, 42, 255));
			dl->AddRectFilled(ImVec2(c.x - 30, c.y + 25), ImVec2(c.x + 30, c.y + 78), colours[i], 24);
		} else {
			dl->AddCircleFilled(ImVec2(c.x, c.y), 42, colours[i], 48);
			dl->AddTriangleFilled(ImVec2(c.x, c.y - 91), ImVec2(c.x - 13, c.y - 36), ImVec2(c.x + 13, c.y - 36), colours[i]);
			dl->AddCircleFilled(ImVec2(c.x - 15, c.y - 3), 6, IM_COL32(28, 32, 31, 255));
			dl->AddCircleFilled(ImVec2(c.x + 15, c.y - 3), 6, IM_COL32(28, 32, 31, 255));
			dl->AddRectFilled(ImVec2(c.x - 23, c.y + 34), ImVec2(c.x + 23, c.y + 88), colours[i], 20);
		}
		const ImVec2 titleSize = ImGui::CalcTextSize(names[i]);
		dl->AddText(ImVec2(p.x + (cardW - titleSize.x) * 0.5f, p.y + cardH - 61), IM_COL32(245, 244, 232, 255), names[i]);
		const ImVec2 descSize = ImGui::CalcTextSize(desc[i]);
		dl->AddText(ImVec2(p.x + (cardW - descSize.x) * 0.5f, p.y + cardH - 33), IM_COL32(158, 166, 158, 255), desc[i]);
		if (pressed && gameReady_) {
			setTarget(targets[i]);
			editorStep_ = 0;
			showStart_ = false;
			resetCamera();
		}
		if (i != 3) ImGui::SameLine(0, gap);
		ImGui::PopID();
	}
	ImGui::Dummy(ImVec2(0, 22));
	std::string lastSource;
	int lastTarget = T_CAPTAIN;
	const bool hasRecent = gameReady_ && loadProject(lastSource, lastTarget);
	ImGui::BeginDisabled(!hasRecent);
	if (ImGui::Button(L("Continuar el último proyecto", "Continue last project"), ImVec2(contentW * 0.58f, 48))) {
		setTarget(lastTarget);
		showStart_ = false;
		editorStep_ = 0;
		loadModel(lastSource, true);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button(spanish() ? "English" : "Español", ImVec2(contentW * 0.18f, 48))) {
		setSpanish(!spanish());
		cfg_.language = spanish() ? "es" : "en";
		cfg_.save();
	}
	ImGui::SameLine();
	if (ImGui::Button(L("Salir", "Quit"), ImVec2(-1, 48))) quit_ = true;
	if (!gameReady_) {
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1), "%s", L("Selecciona primero la carpeta del juego.", "Select the game folder first."));
	}
	ImGui::EndGroup();
	ImGui::End();
}

void App::drawTopBar(float width)
{
	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->WorkPos);
	ImGui::SetNextWindowSize(ImVec2(width, 64));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 10));
	ImGui::Begin("##topbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	ImGui::PopStyleVar();
	if (ImGui::Button(L("Inicio", "Home"), ImVec2(82, 42))) showStart_ = true;
	ImGui::SameLine();
	if (ImGui::Button(L("Abrir modelo…", "Open model…"), ImVec2(142, 42))) {
		const fs::path p = pikmin::launcher::platform::askForFile(L("Abrir modelo", "Open model"), L("Modelo 3D", "3D model"), "*.glb *.gltf *.vrm *.fbx *.obj");
		if (!p.empty()) {
			editingId_.clear();
			editingOlimar_ = false;
			editorStep_ = 0;
			loadModel(p.u8string(), false);
		}
	}
	ImGui::SameLine();
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
	ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(181, 222, 92, 255));
	ImGui::TextUnformatted("NECTAR MODEL MAKER");
	ImGui::PopStyleColor();
	ImGui::SameLine();
	ImGui::TextDisabled("  /  %s", hasModel_ ? fs::u8path(model_.path).filename().u8string().c_str() : L("Sin modelo", "No model"));
	const float right = spanish() ? 132.0f : 124.0f;
	ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0f, width - right));
	if (ImGui::Button(spanish() ? "EN" : "ES", ImVec2(48, 42))) {
		setSpanish(!spanish());
		cfg_.language = spanish() ? "es" : "en";
		cfg_.save();
	}
	ImGui::SameLine();
	if (ImGui::Button("?", ImVec2(48, 42))) ImGui::OpenPopup("##help");
	if (ImGui::BeginPopup("##help")) {
		ImGui::TextUnformatted(L("Navegación 3D", "3D navigation"));
		ImGui::Separator();
		ImGui::TextUnformatted(L("Arrastrar: orbitar", "Drag: orbit"));
		ImGui::TextUnformatted(L("Mayús o botón central: desplazar", "Shift or middle button: pan"));
		ImGui::TextUnformatted(L("Rueda: zoom", "Wheel: zoom"));
		ImGui::EndPopup();
	}
	ImGui::End();
}

void App::drawWorkflow(float x, float y, float width, float height)
{
	ImGui::SetNextWindowPos(ImVec2(x, y));
	ImGui::SetNextWindowSize(ImVec2(width, height));
	ImGui::Begin("##workflow", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	ImGui::TextDisabled("%s", L("FLUJO DE TRABAJO", "WORKFLOW"));
	ImGui::Dummy(ImVec2(0, 8));
	const char* targetNames[] = { "Olimar", L("Pikmin rojo", "Red Pikmin"), L("Pikmin amarillo", "Yellow Pikmin"), L("Pikmin azul", "Blue Pikmin"), L("Los tres Pikmin", "All Pikmin") };
	ImGui::SetWindowFontScale(1.18f);
	ImGui::TextUnformatted(targetNames[target_]);
	ImGui::SetWindowFontScale(1.0f);
	ImGui::TextDisabled("%s", isPikmin() ? L("Esqueleto Pikmin", "Pikmin skeleton") : L("Esqueleto de capitán", "Captain skeleton"));
	ImGui::Dummy(ImVec2(0, 14));
	const char* steps[6] = { L("1  Modelo", "1  Model"), L("2  Colocación", "2  Placement"), L("3  Esqueleto", "3  Skeleton"),
	                         L("4  Animación", "4  Animation"), L("5  Comprobar", "5  Validate"), L("6  Publicar", "6  Publish") };
	for (int i = 0; i < 6; ++i) {
		ImGui::PushID(i);
		if (i == editorStep_) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.43f, 0.57f, 0.19f, 1.0f));
		ImGui::BeginDisabled(i > 0 && !hasModel_);
		if (ImGui::Button(steps[i], ImVec2(-1, 46))) editorStep_ = i;
		ImGui::EndDisabled();
		if (i == editorStep_) ImGui::PopStyleColor();
		ImGui::PopID();
	}
	ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY() + 12.0f, height - 152.0f));
	ImGui::Separator();
	ImGui::TextDisabled("%s", L("OBJETIVO", "TARGET"));
	ImGui::SetNextItemWidth(-1);
	int t = target_;
	ImGui::BeginDisabled(working_);
	if (ImGui::Combo("##workflowtarget", &t, targetNames, 5)) setTarget(t);
	ImGui::EndDisabled();
	if (hasModel_) {
		const ImU32 col = exportDirty_ ? IM_COL32(232, 188, 72, 255) : export_.ok() ? IM_COL32(125, 211, 99, 255) : IM_COL32(231, 91, 75, 255);
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "%s", exportDirty_ ? L("Analizando cambios…", "Analysing changes…") : export_.ok() ? L("Listo para publicar", "Ready to publish") : L("Requiere atención", "Needs attention"));
	}
	ImGui::End();
}

void App::drawPanel(float x, float y, float width, float height)
{
	ImGui::SetNextWindowPos(ImVec2(x, y));
	ImGui::SetNextWindowSize(ImVec2(width, height));
	ImGui::Begin("##panel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	const char* panelTitles[6] = { L("Modelo de origen", "Source model"), L("Colocación", "Placement"), L("Esqueleto y pesos", "Skeleton & weights"),
	                               L("Vista de animación", "Animation preview"), L("Comprobación", "Validation"), L("Publicar en el juego", "Publish to game") };
	ImGui::SetWindowFontScale(1.35f);
	ImGui::TextUnformatted(panelTitles[editorStep_]);
	ImGui::SetWindowFontScale(1.0f);
	ImGui::Separator();
	ImGui::Dummy(ImVec2(0, 5));

	if (editorStep_ == 0) {
	hint(L("Importa el personaje que se adaptará automáticamente al esqueleto elegido.",
	       "Import the character that will be fitted automatically to the selected skeleton."));
	if (bigButton(L("Abrir modelo 3D…", "Open 3D model…"), -1, 46)) {
		const fs::path p = pikmin::launcher::platform::askForFile(L("Abrir modelo", "Open model"), L("Modelo 3D", "3D model"), "*.glb *.gltf *.vrm *.fbx *.obj");
		if (!p.empty()) {
			editingId_.clear();
			editingOlimar_ = false;
			loadModel(p.u8string(), false);
		}
	}
	hint(L("O arrástralo a la ventana. Valen .glb, .gltf, .vrm (VRoid), .fbx (Mixamo) y .obj.",
	       "Or drop it on the window. Works with .glb, .gltf, .vrm (VRoid), .fbx (Mixamo) and .obj."));
	drawCreations();
	if (!editingId_.empty() || editingOlimar_) {
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 220, 120, 255));
		ImGui::TextWrapped(L("Editando: %s", "Editing: %s"), editingName_.c_str());
		ImGui::PopStyleColor();
	}
	if (hasModel_) {
		ImGui::Text("%s  ·  %s", fs::u8path(model_.path).filename().u8string().c_str(), model_.format.c_str());
		ImGui::Text(L("%zu triángulos, %zu materiales, %zu huesos", "%zu triangles, %zu materials, %zu bones"), model_.triangles(), model_.materials.size(),
		            model_.bones.size());
		if (source_.triangles() > model_.triangles())
			hint((std::string(L("Simplificado solo (tenía ", "Simplified automatically (it had ")) + std::to_string(source_.triangles()) + L(").", ").")).c_str());
		if (source_.triangles() > 5000) {
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 110);
			ImGui::SliderInt("##target", &targetTris_, 5000, int(std::min<std::size_t>(source_.triangles(), 150000)), L("%d triángulos", "%d triangles"));
			ImGui::SameLine();
			if (ImGui::Button(L("Aplicar", "Apply"), ImVec2(-1, 0))) resimplify();
		}
	}
	if (target_ == T_ALL)
		hint(L("El juego teñirá este modelo de rojo, amarillo y azul. Usa colores claros o neutros.",
		       "The game will tint this model red, yellow and blue. Use light or neutral colours."));
	else if (isPikmin())
		hint(L("La hoja, el capullo y la flor se añaden automáticamente en la punta del tallo.",
		       "The leaf, bud and flower are added automatically at the tip of the stem."));
	}
	if (!hasModel_) {
		ImGui::Dummy(ImVec2(0, 18));
		ImGui::TextDisabled("%s", L("Importa un modelo para desbloquear los siguientes pasos.", "Import a model to unlock the next steps."));
		ImGui::End();
		return;
	}

	// 2 · Colocar.
	if (editorStep_ == 1) {
	hint(L("Alinea el modelo con la referencia. Empieza con Ajuste automático y corrige solo lo necesario.",
	       "Align the model with the reference. Start with Auto-fit and correct only what is needed."));
	if (bigButton(L("Ajustar solo", "Auto-fit"))) {
		autoFit(model_, olimar_, fit_);
		rigDirty_ = true;
		resetCamera();
	}
	float pct = fit_.scale * 100.0f;
	ImGui::SetNextItemWidth(-1);
	if (ImGui::SliderFloat("##scale", &pct, 30.0f, 300.0f, isPikmin() ? L("Tamaño %.0f%% del Pikmin", "Size %.0f%% of a Pikmin") : L("Tamaño %.0f%% de Olimar", "Size %.0f%% of Olimar"))) {
		const float old = fit_.scale;
		fit_.scale      = pct / 100.0f;
		// Los puntos crecen con el modelo (desde el suelo).
		const float k = fit_.scale / std::max(old, 1e-3f);
		for (Vec3& m : fit_.markers) m = { m.x * k, olimar_.min.y + (m.y - olimar_.min.y) * k, m.z * k };
		rigDirty_ = true;
	}
	const float half = ImGui::GetContentRegionAvail().x * 0.5f - 4;
	auto turn        = [&](int q) {
        fit_.quarter = (fit_.quarter + q + 4) % 4;
        fit_.markers = estimateMarkers(model_, olimar_, fit_);
        fit_.markersCustom = false;
        rigDirty_          = true;
	};
	if (ImGui::Button(L("Girar a la izq.", "Turn left"), ImVec2(half, 0))) turn(1);
	ImGui::SameLine();
	if (ImGui::Button(L("Girar a la der.", "Turn right"), ImVec2(half, 0))) turn(3);
	if (ImGui::Checkbox(L("Viene tumbado (Z arriba)", "Lying down (Z up)"), &fit_.lyingDown)) {
		fit_.markers       = estimateMarkers(model_, olimar_, fit_);
		fit_.markersCustom = false;
		rigDirty_          = true;
	}
	const float H = std::max(olimar_.bodyTop - olimar_.min.y, 1.0f);
	ImGui::SetNextItemWidth(-1);
	float oy = fit_.offsetY;
	if (ImGui::SliderFloat("##oy", &oy, -H * 0.5f, H * 0.5f, L("Subir / bajar %.1f", "Up / down %.1f"))) {
		for (Vec3& m : fit_.markers) m.y += oy - fit_.offsetY;
		fit_.offsetY = oy;
		rigDirty_    = true;
	}
	ImGui::SetNextItemWidth(-1);
	float oz = fit_.offsetZ;
	if (ImGui::SliderFloat("##oz", &oz, -H * 0.5f, H * 0.5f, L("Adelante / atrás %.1f", "Forward / back %.1f"))) {
		for (Vec3& m : fit_.markers) m.z += oz - fit_.offsetZ;
		fit_.offsetZ = oz;
		rigDirty_    = true;
	}
	}

	// 3 · Esqueleto.
	if (editorStep_ == 2) {
	const bool editing = anim_ < 0;
	if (bigButton(editing ? L("Ver cómo se mueve", "See it move") : L("Mover los puntos del esqueleto", "Move the skeleton dots"))) {
		if (editing) {
			for (std::size_t i = 0; i < anims_.size(); ++i)
				if (anims_[i].name == "walk") anim_ = int(i);
			if (anim_ < 0 && !anims_.empty()) anim_ = 0;
		} else {
			anim_ = -1;
		}
	}
	if (editing)
		hint(L("Arrastra cada punto a su sitio: hombros, manos, cadera, piernas, pies y cuello. Los brazos giran desde los hombros que marques.",
		       "Drag each dot into place: shoulders, hands, pelvis, hips, feet and neck. The arms swing from the shoulders you mark."));
	ImGui::Checkbox(L("Simetría (izquierda = derecha)", "Symmetry (left = right)"), &fit_.symmetry);
	if (ImGui::Button(L("Recolocar los puntos solos", "Place the dots automatically"), ImVec2(-1, 0))) {
		fit_.markers       = estimateMarkers(model_, olimar_, fit_);
		fit_.markersCustom = false;
		rigDirty_          = true;
	}
	ImGui::BeginDisabled(!model_.skinned);
	int weights = fit_.useSkeleton ? 0 : 1;
	if (ImGui::RadioButton(L("Pesos del modelo", "Model's weights"), &weights, 0)) {
		fit_.useSkeleton = true;
		if (!fit_.markersCustom) fit_.markers = estimateMarkers(model_, olimar_, fit_);
		rigDirty_ = true;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::RadioButton(L("Automáticos", "Automatic"), &weights, 1)) {
		fit_.useSkeleton = false;
		rigDirty_        = true;
	}
	if (!model_.skinned) hint(L("El modelo no trae esqueleto: los pesos se calculan con los puntos.", "The model has no skeleton: weights come from the dots."));
	if (ImGui::Checkbox(L("Brazos como los de Olimar", "Arms like Olimar's"), &fit_.straightenArms)) rigDirty_ = true;
	ImGui::SameLine();
	if (ImGui::Checkbox(L("Piernas", "Legs"), &fit_.straightenLegs)) rigDirty_ = true;
	ImGui::Checkbox(L("Ver pesos (colores)", "Show weights (colours)"), &showWeights_);
	if (isPikmin()) {
		if (ImGui::Checkbox(L("Añadir el tallo del Pikmin si el modelo no trae", "Add the Pikmin's stem if the model has none"), &fit_.addStem)) rigDirty_ = true;
		if (rig_.stemTriangles)
			hint(L("Tallo original añadido: va de la coronilla a la punta del tallo (puntos amarillos).",
			       "Original stem added: it goes from the head top to the stem tip (yellow dots)."));
	}
	}

	// 4 · Animaciones.
	if (editorStep_ == 3) {
	hint(L("Prueba movimientos reales del juego y compara la silueta con el personaje original.",
	       "Test real in-game motions and compare the silhouette with the original character."));
	ImGui::SetNextItemWidth(-1);
	const char* current = anim_ >= 0 ? animLabel(anims_[std::size_t(anim_)]) : L("Pose de reposo (editar puntos)", "Rest pose (edit dots)");
	if (ImGui::BeginCombo("##anim", current, ImGuiComboFlags_HeightLarge)) {
		if (ImGui::Selectable(L("Pose de reposo (editar puntos)", "Rest pose (edit dots)"), anim_ < 0)) anim_ = -1;
		for (int i : animOrder_) {
			ImGui::PushID(i);
			if (ImGui::Selectable(animLabel(anims_[std::size_t(i)]), anim_ == i)) {
				anim_  = i;
				frame_ = 0;
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	if (ImGui::Button(playing_ ? L("Pausa", "Pause") : L("Seguir", "Play"), ImVec2(90, 0))) playing_ = !playing_;
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-1);
	ImGui::SliderFloat("##speed", &speed_, 0.1f, 2.0f, L("Velocidad %.1fx", "Speed %.1fx"));
	if (isPikmin()) {
		const char* leaves[] = { L("En la punta: hoja", "On the tip: leaf"), L("En la punta: capullo", "On the tip: bud"), L("En la punta: flor", "On the tip: flower") };
		ImGui::SetNextItemWidth(-1);
		ImGui::Combo("##leaf", &leafKind_, leaves, 3);
	}
	const char* modes[] = { isPikmin() ? L("Pikmin original: oculto", "Original Pikmin: hidden") : L("Olimar: oculto", "Olimar: hidden"),
		                    isPikmin() ? L("Pikmin original: al lado", "Original Pikmin: beside") : L("Olimar: al lado", "Olimar: beside"),
		                    isPikmin() ? L("Pikmin original: encima", "Original Pikmin: overlaid") : L("Olimar: encima", "Olimar: overlaid") };
	ImGui::SetNextItemWidth(-1);
	if (ImGui::Combo("##olimar", &olimarMode_, modes, 3)) resetCamera();
	ImGui::Checkbox(L("Ver esqueleto", "Show skeleton"), &showSkeleton_);
	}

	// 5 · Guardar.
	if (editorStep_ == 4) {
	hint(L("Estos controles detectan problemas antes de instalar el personaje.",
	       "These checks detect problems before the character is installed."));
	if (exportDirty_) hint(L("Comprobando…", "Checking…"));
	for (const Check& c : export_.checks) {
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		dl->AddCircleFilled(ImVec2(p.x + 7, p.y + ImGui::GetTextLineHeight() * 0.5f + 1), 6, levelColour(c.level));
		ImGui::Dummy(ImVec2(18, 0));
		ImGui::SameLine();
		ImGui::TextWrapped("%s", c.text.c_str());
	}
	if (ImGui::TreeNode(L("Más opciones", "More options"))) {
		const int sizes[] = { 512, 1024, 2048 };
		int sel           = exportOpt_.textureMax <= 512 ? 0 : exportOpt_.textureMax <= 1024 ? 1 : 2;
		const char* names[] = { L("Texturas: 512 (Android)", "Textures: 512 (Android)"), L("Texturas: 1024", "Textures: 1024"), L("Texturas: 2048 (PC)", "Textures: 2048 (PC)") };
		ImGui::SetNextItemWidth(-1);
		if (ImGui::Combo("##tex", &sel, names, 3)) {
			exportOpt_.textureMax = sizes[sel];
			exportDirty_          = true;
		}
		ImGui::TextUnformatted(L("Sin tinte en cooperativo (cara, visor):", "No co-op tint (face, visor):"));
		for (std::size_t i = 0; i < model_.materials.size(); ++i) {
			bool on = exportOpt_.noTint[i] != 0;
			ImGui::PushID(int(i));
			if (ImGui::Checkbox(model_.materials[i].name.c_str(), &on)) {
				exportOpt_.noTint[i] = char(on);
				exportDirty_         = true;
			}
			ImGui::PopID();
		}
		ImGui::TreePop();
	}
	}
	if (editorStep_ == 5) drawCharacters();
	if (!status_.empty()) {
		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Text, statusError_ ? IM_COL32(240, 110, 90, 255) : IM_COL32(150, 220, 120, 255));
		ImGui::TextWrapped("%s", status_.c_str());
		ImGui::PopStyleColor();
	}
	ImGui::Dummy(ImVec2(0, 10));
	ImGui::Separator();
	ImGui::Dummy(ImVec2(0, 4));
	const float navW = (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;
	ImGui::BeginDisabled(editorStep_ == 0);
	if (ImGui::Button(L("Anterior", "Previous"), ImVec2(navW, 40))) --editorStep_;
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(editorStep_ == 5 || !hasModel_);
	if (ImGui::Button(editorStep_ == 4 ? L("Ir a publicar", "Go to publish") : L("Siguiente", "Next"), ImVec2(-1, 40))) ++editorStep_;
	ImGui::EndDisabled();
	ImGui::End();
}

bool App::project(const Vec3& p, ImVec2& out) const
{
	const nm::M4& m = viewProj_;
	const float x   = m.at(0, 0) * p.x + m.at(0, 1) * p.y + m.at(0, 2) * p.z + m.at(0, 3);
	const float y   = m.at(1, 0) * p.x + m.at(1, 1) * p.y + m.at(1, 2) * p.z + m.at(1, 3);
	const float w   = m.at(3, 0) * p.x + m.at(3, 1) * p.y + m.at(3, 2) * p.z + m.at(3, 3);
	if (w <= 1e-5f) return false;
	out = ImVec2(viewOrigin_.x + (x / w * 0.5f + 0.5f) * viewSize_.x, viewOrigin_.y + (0.5f - y / w * 0.5f) * viewSize_.y);
	return true;
}

void App::dragMarker(const ImVec2& mouse)
{
	// Se mueve en el plano de la pantalla que pasa por el punto.
	const nm::M4 inv = viewProj_.inverse();
	const float nx   = (mouse.x - viewOrigin_.x) / viewSize_.x * 2 - 1;
	const float ny   = 1 - (mouse.y - viewOrigin_.y) / viewSize_.y * 2;
	const nm::V3 a = inv.point({ nx, ny, -1 }), b = inv.point({ nx, ny, 1 });
	const Vec3 o { a.x, a.y, a.z }, d = normalize(Vec3 { b.x - a.x, b.y - a.y, b.z - a.z });
	const Vec3 f     = normalize(cam_.target - cam_.eye());
	Vec3& m          = fit_.markers[std::size_t(dragMarker_)];
	const float den  = dot(d, f);
	if (std::fabs(den) < 1e-6f) return;
	const float t = dot(m - o, f) / den;
	m             = o + d * t;
	if (fit_.symmetry) {
		const int mirror = markerMirror(dragMarker_);
		if (mirror >= 0) fit_.markers[std::size_t(mirror)] = { -m.x, m.y, m.z };
		else m.x = 0;
	}
	fit_.markersCustom = true;
	rigDirty_          = true;
}

void App::drawView(float x, float y, float width, float height)
{
	ImGui::SetNextWindowPos(ImVec2(x, y));
	ImGui::SetNextWindowSize(ImVec2(width, height));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("##view", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
	ImGui::PopStyleVar();
	viewOrigin_ = ImGui::GetCursorScreenPos();
	viewSize_   = ImGui::GetContentRegionAvail();
	viewSize_.x = std::max(viewSize_.x, 1.0f);
	viewSize_.y = std::max(viewSize_.y, 1.0f);
	view_.resize(int(viewSize_.x), int(viewSize_.y));

	if (gameReady_) {
		fillMeshes();
		viewProj_ = cam_.projection(viewSize_.x / viewSize_.y) * cam_.view();
		view_.begin(viewProj_);
		// Suelo: rejilla.
		std::vector<Vec3> grid;
		const float step = 2.5f, ext = 25.0f;
		for (float g = -ext; g <= ext + 0.01f; g += step) {
			grid.push_back({ g, olimar_.min.y, -ext });
			grid.push_back({ g, olimar_.min.y, ext });
			grid.push_back({ -ext, olimar_.min.y, g });
			grid.push_back({ ext, olimar_.min.y, g });
		}
		const float gridColour[4] = { 0.25f, 0.30f, 0.28f, 1 };
		view_.drawLines(grid, gridColour);
		if (olimarMode_ == 1 || (olimarMode_ == 2 && !hasModel_)) view_.drawMesh(olimarMesh_, true);
		view_.drawMesh(modelMesh_, !showWeights_);
		if (isPikmin()) view_.drawMesh(leafMesh_, true);
		if (olimarMode_ == 2 && hasModel_) view_.drawMesh(olimarMesh_, true, 0.35f);
		view_.end();
	}
	ImGui::Image(ImTextureID(std::intptr_t(view_.colorTexture())), viewSize_, ImVec2(0, 1), ImVec2(1, 0));
	const bool hovered = ImGui::IsItemHovered();

	// Esqueleto encima.
	std::vector<Mat34> skin;
	if (hasModel_ && anim_ >= 0 && std::size_t(anim_) < anims_.size()) {
		std::vector<Mat34> world;
		anims_[std::size_t(anim_)].pose(frame_, olimar_.parent, world);
		retargetSkin(olimar_, rig_.pivots, world, skin);
	}
	if (hasModel_) drawOverlay(skin);

	// Ratón: arrastrar un punto, girar (izquierdo/derecho), desplazar (medio
	// o Mayús), rueda para acercar.
	ImGuiIO& io = ImGui::GetIO();
	if (hovered && io.MouseWheel != 0) cam_.distance = std::clamp(cam_.distance * std::pow(0.88f, io.MouseWheel), 5.0f, 400.0f);
	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		if (hoverMarker_ >= 0) dragMarker_ = hoverMarker_;
		else if (io.KeyShift) panning_ = true;
		else orbiting_ = true;
	}
	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) orbiting_ = true;
	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) panning_ = true;
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
		dragMarker_ = -1;
		orbiting_ = panning_ = false;
	}
	if (dragMarker_ >= 0) {
		dragMarker(io.MousePos);
	} else if (orbiting_) {
		cam_.yaw -= io.MouseDelta.x * 0.008f;
		cam_.pitch = std::clamp(cam_.pitch + io.MouseDelta.y * 0.008f, -1.4f, 1.4f);
	} else if (panning_) {
		const Vec3 f = normalize(cam_.target - cam_.eye());
		const Vec3 r = normalize(cross(f, { 0, 1, 0 }));
		const Vec3 u = cross(r, f);
		const float k = cam_.distance * 0.0016f;
		cam_.target   = cam_.target - r * (io.MouseDelta.x * k) + u * (io.MouseDelta.y * k);
	}

	// Botones de cámara.
	ImGui::SetCursorScreenPos(ImVec2(viewOrigin_.x + 12, viewOrigin_.y + 12));
	if (ImGui::Button(L("De frente", "Front"))) {
		cam_.yaw   = 0;
		cam_.pitch = 0.05f;
	}
	ImGui::SameLine();
	if (ImGui::Button(L("De lado", "Side"))) {
		cam_.yaw   = kPi / 2;
		cam_.pitch = 0.05f;
	}
	ImGui::SameLine();
	if (ImGui::Button(L("Centrar", "Recentre"))) resetCamera();
	if (!hasModel_) {
		const char* msg = L("Abre o arrastra aquí un modelo 3D", "Open or drop a 3D model here");
		const ImVec2 ts = ImGui::CalcTextSize(msg);
		ImGui::GetWindowDrawList()->AddText(ImVec2(viewOrigin_.x + (viewSize_.x - ts.x) * 0.5f, viewOrigin_.y + viewSize_.y * 0.85f),
		                                    IM_COL32(240, 230, 190, 230), msg);
	} else {
		ImGui::SetCursorScreenPos(ImVec2(viewOrigin_.x + 12, viewOrigin_.y + viewSize_.y - 30));
		hint(L("Izq./der.: girar · Mayús o rueda pulsada: mover · Rueda: acercar", "Left/right drag: orbit · Shift or middle: pan · Wheel: zoom"));
	}
	ImGui::End();
}

void App::drawOverlay(const std::vector<Mat34>& skin)
{
	ImDrawList* dl   = ImGui::GetWindowDrawList();
	hoverMarker_     = -1;
	const bool edit  = skin.empty();
	auto pos         = [&](int marker) -> Vec3 {
        const Vec3& m = fit_.markers[std::size_t(marker)];
        if (edit) return m;
        // Animado: cada punto con la articulación que lo mueve.
        int joint = J_SPINE;
        switch (marker) {
        case M_NECK: joint = J_HEAD; break;
        case M_HEADTOP: joint = isPikmin() ? J_ANT1 : J_HEAD; break;
        case M_STEMTOP: joint = J_ANT3; break;
        case M_RSHOULDER:
        case M_RHAND: joint = J_RARM; break;
        case M_LSHOULDER:
        case M_LHAND: joint = J_LARM; break;
        case M_RHIP:
        case M_RFOOT: joint = J_RLEG; break;
        case M_LHIP:
        case M_LFOOT: joint = J_LLEG; break;
        default: break;
        }
        return skin[std::size_t(joint)].point(rig_.markers[std::size_t(marker)]);
	};
	if (!showSkeleton_ && !edit) return;
	const int bones[][2] = { { M_PELVIS, M_NECK },       { M_NECK, M_HEADTOP },   { M_NECK, M_RSHOULDER }, { M_NECK, M_LSHOULDER },
		                     { M_RSHOULDER, M_RHAND },   { M_LSHOULDER, M_LHAND }, { M_PELVIS, M_RHIP },    { M_PELVIS, M_LHIP },
		                     { M_RHIP, M_RFOOT },        { M_LHIP, M_LFOOT } };
	for (const auto& b : bones) {
		ImVec2 a, c;
		if (project(pos(b[0]), a) && project(pos(b[1]), c)) dl->AddLine(a, c, IM_COL32(255, 235, 140, 200), 2.5f);
	}
	// El tallo del Pikmin, hasta donde va la hoja.
	if (isPikmin()) {
		ImVec2 a, c;
		if (project(pos(M_HEADTOP), a) && project(pos(M_STEMTOP), c)) dl->AddLine(a, c, IM_COL32(150, 230, 110, 220), 2.5f);
	}
	// Solo los Pikmin tienen punta del tallo.
	const int markers = isPikmin() ? M_COUNT : M_STEMTOP;
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	float best         = 12.0f * 12.0f;
	for (int i = 0; i < markers; ++i) {
		ImVec2 s;
		if (!project(pos(i), s)) continue;
		if (edit) {
			const float d = (s.x - mouse.x) * (s.x - mouse.x) + (s.y - mouse.y) * (s.y - mouse.y);
			if (d < best && dragMarker_ < 0) {
				best         = d;
				hoverMarker_ = i;
			}
		}
	}
	for (int i = 0; i < markers; ++i) {
		ImVec2 s;
		if (!project(pos(i), s)) continue;
		const bool hot   = i == hoverMarker_ || i == dragMarker_;
		const float r    = edit ? (hot ? 9.0f : 7.0f) : 4.0f;
		const ImU32 fill = i == M_STEMTOP ? IM_COL32(150, 230, 110, 255)
		                 : i == M_PELVIS || i == M_NECK || i == M_HEADTOP ? IM_COL32(250, 210, 80, 255)
		                 : (i == M_LSHOULDER || i == M_LHAND || i == M_LHIP || i == M_LFOOT) ? IM_COL32(240, 100, 80, 255)
		                                                                                      : IM_COL32(90, 150, 245, 255);
		dl->AddCircleFilled(s, r, fill);
		dl->AddCircle(s, r, IM_COL32(20, 25, 15, 255), 0, 1.5f);
		if (edit && hot) dl->AddText(ImVec2(s.x + 12, s.y - 8), IM_COL32(255, 250, 230, 255), markerName(i));
	}
}

void App::dropFile(const std::string& path)
{
	std::string ext = fs::u8path(path).extension().u8string();
	for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
	if (ext == nectar::kCharacterExtension) {
		installPack(path);
		return;
	}
	editingId_.clear();
	editingOlimar_ = false;
	showStart_ = false;
	editorStep_ = 0;
	loadModel(path, false);
}

json App::projectJson(const std::string& source) const
{
	json j;
	j["source"]         = source;
	j["targetTris"]     = targetTris_;
	j["scale"]          = fit_.scale;
	j["quarter"]        = fit_.quarter;
	j["lyingDown"]      = fit_.lyingDown;
	j["offsetY"]        = fit_.offsetY;
	j["offsetZ"]        = fit_.offsetZ;
	j["useSkeleton"]    = fit_.useSkeleton;
	j["straightenArms"] = fit_.straightenArms;
	j["straightenLegs"] = fit_.straightenLegs;
	j["symmetry"]       = fit_.symmetry;
	j["markersCustom"]  = fit_.markersCustom;
	j["textureMax"]     = exportOpt_.textureMax;
	j["charName"]       = std::string(charName_);
	j["charAuthor"]     = std::string(charAuthor_);
	j["target"]         = target_;
	j["addStem"]        = fit_.addStem;
	json markers        = json::array();
	for (const Vec3& m : fit_.markers) markers.push_back({ m.x, m.y, m.z });
	j["markers"] = markers;
	std::vector<int> nt;
	for (char c : exportOpt_.noTint) nt.push_back(c ? 1 : 0);
	j["noTint"] = nt;
	return j;
}

// La sesión: para seguir donde se quedó al volver a abrir el creador.
void App::saveProject() const
{
	std::error_code ec;
	fs::create_directories(fs::u8path(customDir()), ec);
	std::ofstream f(fs::u8path(projectPath()));
	f << projectJson(model_.path).dump(1);
}

void App::projectFiles(std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& out) const
{
	out.clear();
	const fs::path base = fs::u8path(model_.path).parent_path();
	std::string mainRel;
	std::size_t total = 0;
	std::vector<std::string> seen;
	for (const std::string& file : source_.files.empty() ? model_.files : source_.files) {
		const fs::path p = fs::u8path(file);
		std::error_code ec;
		// Dentro de la carpeta del modelo, con su ruta (los .gltf y .mtl las
		// nombran así); fuera, al lado del modelo (los FBX las buscan ahí).
		std::string rel = fs::relative(p, base, ec).generic_u8string();
		if (ec || rel.empty() || rel.rfind("..", 0) == 0 || !nectar::safeProjectPath(rel)) rel = p.filename().u8string();
		rel = "source/" + rel;
		if (std::find(seen.begin(), seen.end(), rel) != seen.end()) continue;
		std::ifstream f(p, std::ios::binary);
		std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(f)), {});
		if (data.empty() || !nectar::safeProjectPath(rel)) continue;
		total += data.size();
		if (total > 200u * 1024u * 1024u) break; // un original enorme: el pack se queda sin él
		if (file == model_.path) mainRel = rel;
		seen.push_back(rel);
		out.push_back({ rel, std::move(data) });
	}
	const std::string text = projectJson(mainRel).dump(1);
	out.push_back({ "project.json", std::vector<std::uint8_t>(text.begin(), text.end()) });
}

// Vuelve a abrir un personaje (o Mi Olimar) para editarlo: con su proyecto,
// el modelo original y los ajustes; sin él, su model.nhm.
void App::editCreation(const std::string& dir, const nectar::CharacterInfo& info, bool olimar)
{
	if (working_) return;
	const fs::path root = fs::u8path(dir);
	std::ifstream f(root / "project" / "project.json");
	std::string text((std::istreambuf_iterator<char>(f)), {});
	int target = T_CAPTAIN;
	if (info.target == nectar::kTargetPikminRed) target = T_RED;
	else if (info.target == nectar::kTargetPikminYellow) target = T_YELLOW;
	else if (info.target == nectar::kTargetPikminBlue) target = T_BLUE;
	else if (info.target == nectar::kTargetPikminAll) target = T_ALL;
	std::string source;
	try {
		if (!text.empty()) source = json::parse(text).value("source", "");
	} catch (const std::exception&) {
		text.clear();
	}
	std::error_code ec;
	const fs::path sourcePath = root / "project" / fs::u8path(source);
	// Mi Olimar guardado antes de que hubiera proyecto: la ruta de la sesión.
	if (olimar && source.empty()) {
		std::ifstream old(fs::u8path(projectPath()));
		std::string oldText((std::istreambuf_iterator<char>(old)), {});
		try {
			const std::string abs = oldText.empty() ? std::string() : json::parse(oldText).value("source", "");
			std::error_code oc;
			if (!abs.empty() && fs::is_regular_file(fs::u8path(abs), oc)) {
				editingOlimar_ = true;
				editingId_.clear();
				editingName_ = info.name;
				hasModel_    = false;
				setTarget(T_CAPTAIN);
				restoreJson_ = oldText;
				loadModel(abs, true);
				return;
			}
		} catch (const std::exception&) {
		}
	}
	editingId_     = olimar ? std::string() : info.id;
	editingOlimar_ = olimar;
	editingName_   = info.name;
	// Se abre otro: el de ahora deja de contar (y cambiar de objetivo no
	// tiene que simplificarlo).
	hasModel_ = false;
	setTarget(target);
	try {
		if (!text.empty()) targetTris_ = json::parse(text).value("targetTris", targetTris_);
	} catch (const std::exception&) {
	}
	if (!source.empty() && nectar::safeProjectPath(source) && fs::is_regular_file(sourcePath, ec)) {
		restoreJson_ = text;
		loadModel(sourcePath.u8string(), true);
	} else {
		restoreJson_.clear();
		loadModel((root / (olimar ? "olimar.nhm" : "model.nhm")).u8string(), false);
	}
	std::snprintf(charName_, sizeof(charName_), "%s", info.name.c_str());
	std::snprintf(charAuthor_, sizeof(charAuthor_), "%s", info.author.c_str());
}

// Un .nectarchar que no está instalado: su proyecto va a una carpeta de
// trabajo y se edita desde ahí (instalarlo después lo copia al juego).
void App::openPackForEdit(const std::string& path)
{
	nectar::CharacterFiles files;
	std::string error;
	if (!nectar::readCharacterPack(path, files, error)) {
		say(std::string(L("No se pudo abrir: ", "Could not open: ")) + error, true);
		return;
	}
	const fs::path work = fs::u8path(nm::Config::directory()) / "modelmaker_edit" / files.info.id;
	std::error_code ec;
	fs::remove_all(work, ec);
	fs::create_directories(work, ec);
	auto write = [&](const fs::path& p, const std::vector<std::uint8_t>& data) {
		fs::create_directories(p.parent_path(), ec);
		std::ofstream o(p, std::ios::binary);
		o.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
	};
	write(work / "model.nhm", files.nhm);
	for (const auto& [rel, data] : files.project)
		if (nectar::safeProjectPath(rel)) write(work / "project" / fs::u8path(rel), data);
	editCreation(work.u8string(), files.info, false);
}

void App::drawCreations()
{
	if (!gameReady_) return;
	std::error_code ec;
	// Olimar ya no se reemplaza; uno hecho antes se abre para guardarlo como
	// personaje con su propio hueco.
	const bool olimar = fs::exists(fs::path(customDir()) / "olimar.nhm", ec);
	if (!ImGui::TreeNode(L("Abrir uno ya creado", "Open one you made"))) return;
	hint(L("Para retocarlo: se abre con su modelo original y sus ajustes.", "To touch it up: it opens with its original model and settings."));
	if (olimar && ImGui::Button(L("Olimar propio antiguo (guárdalo como personaje)", "Old custom Olimar (save it as a character)"), ImVec2(-1, 0))) {
		nectar::CharacterInfo info;
		info.name = L("Mi Olimar", "My Olimar");
		editCreation(customDir(), info, true);
	}
	for (const nectar::CharacterInfo& c : creations_) {
		ImGui::PushID(c.dir.c_str());
		std::string label = c.name;
		if (c.isPikmin()) label += std::string("  (Pikmin)");
		if (ImGui::Button(label.c_str(), ImVec2(-1, 0))) editCreation(c.dir, c, false);
		ImGui::PopID();
	}
	if (!olimar && creations_.empty()) hint(L("Todavía no hay ninguno instalado.", "None installed yet."));
	if (ImGui::Button(L("Abrir un .nectarchar para editar…", "Open a .nectarchar to edit…"), ImVec2(-1, 0))) {
		const fs::path p = pikmin::launcher::platform::askForFile(L("Abrir personaje", "Open character"), L("Personaje", "Character"), "*.nectarchar");
		if (!p.empty()) openPackForEdit(p.u8string());
	}
	ImGui::TreePop();
}

bool App::loadProject(std::string& source, int& target)
{
	std::ifstream f(fs::u8path(projectPath()));
	if (!f) return false;
	try {
		const json j = json::parse(f);
		source       = j.value("source", "");
		target       = std::clamp(j.value("target", 0), 0, 4);
	} catch (const std::exception&) {
		return false;
	}
	std::error_code ec;
	return !source.empty() && fs::exists(fs::u8path(source), ec);
}

// ── Personajes propios ──────────────────────────────────────────────────

void App::refreshInstalled()
{
	installed_ = isPikmin() ? nectar::listPikminModels(cfg_.gameDir()) : nectar::listCharacters(cfg_.gameDir());
	creations_ = nectar::listCharacters(cfg_.gameDir());
	for (const nectar::CharacterInfo& c : nectar::listPikminModels(cfg_.gameDir())) creations_.push_back(c);
}

namespace {
const char* const kTargets[5] = { nectar::kTargetCaptain, nectar::kTargetPikminRed, nectar::kTargetPikminYellow, nectar::kTargetPikminBlue,
	                              nectar::kTargetPikminAll };
const char* targetLabel(const std::string& target)
{
	if (target == nectar::kTargetPikminRed) return L("rojos", "red");
	if (target == nectar::kTargetPikminYellow) return L("amarillos", "yellow");
	if (target == nectar::kTargetPikminBlue) return L("azules", "blue");
	if (target == nectar::kTargetPikminAll) return L("los tres", "all three");
	return "";
}
} // namespace

// Elige el pack en el juego para sus colores: lo hace el propio juego
// (--pikmin-model), que es quien guarda pikmin_settings.conf.
void App::useNowInGame(const nectar::CharacterInfo& info)
{
	std::string exe = cfg_.gameExe;
	std::error_code ec;
	if (exe.empty() || !fs::exists(exe, ec)) exe = nm::guessGameExe();
	if (exe.empty()) {
		const fs::path guess = fs::path(cfg_.gameDir()) /
#ifdef _WIN32
		                       "nectar.exe";
#else
		                       "nectar";
#endif
		if (fs::exists(guess, ec)) exe = guess.string();
	}
	if (exe.empty()) {
		say(L("Instalado. Elígelo en el juego: Opciones > Gráficos > modelos de Pikmin.", "Installed. Pick it in the game: Options > Graphics > Pikmin models."));
		return;
	}
	std::vector<std::string> colours;
	if (info.target == nectar::kTargetPikminAll) colours = { "red", "yellow", "blue" };
	else colours = { info.target.substr(std::string("pikmin_").size()) };
	for (const std::string& c : colours) {
		std::string output, error;
		if (!pikmin::launcher::platform::runAndCapture(exe, { "--pikmin-model", c, info.id }, cfg_.gameDir(), output, error)) {
			say(std::string(L("Instalado, pero no se pudo elegir: ", "Installed, but it could not be selected: ")) + error, true);
			return;
		}
	}
	say(std::string("\"") + info.name + L("\" instalado y elegido: los Pikmin ", "\" installed and selected: the ") + targetLabel(info.target)
	    + L(" ya lo usan.", " Pikmin use it now."));
}

// Tarjeta del selector: el modelo de frente y un poco girado, en reposo.
bool App::renderPortrait(std::vector<std::uint8_t>& png)
{
	png.clear();
	DynamicMesh mesh;
	fillModelMesh(mesh, nullptr, false, false);
	const Vec3 mn = rig_.min, mx = rig_.max;
	const float h = mx.y - mn.y, w = std::max(mx.x - mn.x, mx.z - mn.z);
	OrbitCamera cam;
	cam.target   = (mn + mx) * 0.5f;
	cam.yaw      = 0.35f;
	cam.pitch    = 0.1f;
	cam.fovY     = 0.6f;
	cam.distance = std::max(h, w) * 0.55f / std::tan(cam.fovY * 0.5f) + std::max(h, w) * 0.2f;
	nectar::Image img;
	if (!view_.renderImage(256, 256, cam.projection(1.0f) * cam.view(), mesh, img)) return false;
	std::error_code ec;
	const fs::path tmp = fs::temp_directory_path(ec) / "nectar_modelmaker_portrait.png";
	if (!nectar::writePng(tmp.u8string(), img)) return false;
	std::ifstream f(tmp, std::ios::binary);
	png.assign(std::istreambuf_iterator<char>(f), {});
	fs::remove(tmp, ec);
	return !png.empty();
}

bool App::characterFiles(nectar::CharacterFiles& files, std::string& error)
{
	if (exportDirty_) {
		export_      = buildParts(model_, rig_, olimar_, fit_, exportOpt_);
		exportDirty_ = false;
	}
	if (!buildNhm(export_, rig_, olimar_, files.nhm, error)) return false;
	files.info          = nectar::CharacterInfo();
	files.info.name     = charName_[0] ? charName_ : "Custom";
	files.info.author   = charAuthor_;
	files.info.target   = kTargets[target_];
	files.info.skeleton = isPikmin() ? "pikmin" : "olimar";
	// Mismo nombre y autor: la misma carpeta (instalar otra vez lo reemplaza).
	const std::string key = files.info.name + "\n" + files.info.author;
	files.info.id         = editingId_.empty() ? nectar::characterId(files.info.name, std::vector<std::uint8_t>(key.begin(), key.end())) : editingId_;
	renderPortrait(files.portrait);
	projectFiles(files.project);
	saveProject();
	return true;
}

void App::installAsCharacter()
{
	nectar::CharacterFiles files;
	std::string error, dir;
	if (!characterFiles(files, error) || !nectar::installCharacter(cfg_.gameDir(), files, dir, error)) {
		say(std::string(L("No se pudo instalar: ", "Could not install: ")) + error, true);
		return;
	}
	refreshInstalled();
	if (isPikmin()) {
		files.info.dir = dir;
		useNowInGame(files.info);
		return;
	}
	say(std::string(L("\"", "\"")) + files.info.name
	    + L("\" ya está en el selector de capitán del juego.", "\" is now in the game's captain selector."));
}

void App::exportCharacter()
{
	nectar::CharacterFiles files;
	std::string error;
	if (!characterFiles(files, error)) {
		say(std::string(L("No se pudo exportar: ", "Could not export: ")) + error, true);
		return;
	}
	const fs::path where = pikmin::launcher::platform::askForInstallDirectory(L("Carpeta donde guardar el personaje", "Folder to save the character in"));
	if (where.empty()) return;
	const fs::path path = where / (files.info.id + nectar::kCharacterExtension);
	if (!nectar::writeCharacterPack(path.u8string(), files, error)) {
		say(std::string(L("No se pudo exportar: ", "Could not export: ")) + error, true);
		return;
	}
	say(std::string(L("Exportado: ", "Exported: ")) + path.u8string()
	    + L(" (se instala con el launcher: botón Characters).", " (install it from the launcher: Characters button)."));
}

void App::installPack(const std::string& path)
{
	if (!gameReady_) {
		say(L("Primero elige la carpeta del juego.", "Choose the game folder first."), true);
		return;
	}
	nectar::CharacterInfo info;
	std::string error;
	if (!nectar::installCharacterPack(cfg_.gameDir(), path, info, error)) {
		say(std::string(L("No se pudo instalar: ", "Could not install: ")) + error, true);
		return;
	}
	refreshInstalled();
	if (info.isPikmin())
		say(std::string("\"") + info.name + L("\" instalado: elígelo en Opciones > Gráficos del juego (Pikmin ", "\" installed: pick it in the game's Options > Graphics (")
		    + targetLabel(info.target) + L(").", " Pikmin)."));
	else
		say(std::string("\"") + info.name + L("\" instalado: sale en el selector de capitán.", "\" installed: it is in the captain selector."));
}

void App::drawCharacters()
{
	if (isPikmin()) {
		stepHeader(6, L("Tus Pikmin en el juego", "Your Pikmin in the game"));
		hint(L("Se instala como modelo de Pikmin y se elige por color en Opciones > Gráficos del juego o en el launcher. Se puede compartir como archivo .nectarchar.",
		       "It installs as a Pikmin model, picked per colour in the game's Options > Graphics or in the launcher. It can be shared as a .nectarchar file."));
	} else {
		stepHeader(6, L("Personaje propio", "Custom character"));
		hint(L("Con su propio hueco en el selector de capitán, sin quitar a Olimar. Se puede compartir como archivo .nectarchar.",
		       "With its own slot in the captain selector, keeping Olimar. It can be shared as a .nectarchar file."));
	}
	if (hasModel_) {
		ImGui::SetNextItemWidth(-1);
		ImGui::InputTextWithHint("##charname", isPikmin() ? L("Nombre del modelo", "Model name") : L("Nombre en el selector", "Name in the selector"), charName_,
		                         sizeof(charName_));
		ImGui::SetNextItemWidth(-1);
		ImGui::InputTextWithHint("##charauthor", L("Autor (opcional)", "Author (optional)"), charAuthor_, sizeof(charAuthor_));
		// Dónde se guarda: un hueco nuevo o en lugar de uno propio ya instalado.
		{
			const char* newLabel = isPikmin() ? L("Modelo nuevo", "New model") : L("Hueco nuevo", "New slot");
			std::string current  = newLabel;
			for (const nectar::CharacterInfo& c : installed_)
				if (c.id == editingId_) current = std::string(L("Reemplazar: ", "Replace: ")) + c.name;
			ImGui::SetNextItemWidth(-1);
			if (ImGui::BeginCombo("##slot", current.c_str())) {
				if (ImGui::Selectable(newLabel, editingId_.empty())) {
					editingId_.clear();
					editingName_.clear();
				}
				for (const nectar::CharacterInfo& c : installed_) {
					ImGui::PushID(c.id.c_str());
					const std::string label = std::string(L("Reemplazar: ", "Replace: ")) + c.name + (c.isPikmin() ? std::string("  [") + targetLabel(c.target) + "]" : "");
					if (ImGui::Selectable(label.c_str(), c.id == editingId_)) {
						editingId_   = c.id;
						editingName_ = c.name;
					}
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}
		}
		bool replacing = false;
		for (const nectar::CharacterInfo& c : installed_) replacing = replacing || c.id == editingId_;
		ImGui::BeginDisabled(exportDirty_ || !export_.ok() || working_);
		if (bigButton(replacing ? (isPikmin() ? L("Reemplazar y usar ya", "Replace and use now") : L("Reemplazar ese personaje", "Replace that character"))
		                        : (isPikmin() ? L("Instalar y usar ya", "Install and use now") : L("Instalar como personaje nuevo", "Install as a new character"))))
			installAsCharacter();
		if (ImGui::Button(L("Exportar archivo .nectarchar…", "Export .nectarchar file…"), ImVec2(-1, 0))) exportCharacter();
		ImGui::EndDisabled();
	}
	if (ImGui::Button(L("Instalar un personaje (.nectarchar)…", "Install a character (.nectarchar)…"), ImVec2(-1, 0))) {
		const fs::path p = pikmin::launcher::platform::askForFile(L("Instalar personaje", "Install character"), L("Personaje", "Character"), "*.nectarchar");
		if (!p.empty()) installPack(p.u8string());
	}
	if (installed_.empty()) return;
	ImGui::TextUnformatted(L("Instalados:", "Installed:"));
	for (const nectar::CharacterInfo& c : installed_) {
		ImGui::PushID(c.id.c_str());
		ImGui::Bullet();
		ImGui::SameLine();
		if (c.isPikmin()) ImGui::Text("%s  [%s]", c.name.c_str(), targetLabel(c.target));
		else if (c.author.empty()) ImGui::TextUnformatted(c.name.c_str());
		else ImGui::Text("%s  (%s)", c.name.c_str(), c.author.c_str());
		ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 70);
		if (ImGui::SmallButton(L("Quitar", "Remove"))) {
			removeId_ = c.id;
			ImGui::OpenPopup("##removechar");
		}
		if (ImGui::BeginPopup("##removechar")) {
			ImGui::Text(L("¿Quitar \"%s\" del juego?", "Remove \"%s\" from the game?"), c.name.c_str());
			if (ImGui::Button(L("Sí, quitar", "Yes, remove"))) {
				std::string error;
				if (nectar::removeCharacter(cfg_.gameDir(), removeId_, error)) say(std::string(L("Quitado: ", "Removed: ")) + c.name);
				else say(error, true);
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				ImGui::PopID();
				refreshInstalled();
				return;
			}
			ImGui::SameLine();
			if (ImGui::Button(L("No", "No"))) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
}

} // namespace mm
