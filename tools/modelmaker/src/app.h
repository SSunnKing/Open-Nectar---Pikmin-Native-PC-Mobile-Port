#pragma once

// Ventana del creador de modelos: un asistente de cinco pasos a la izquierda
// (modelo, colocar, esqueleto, animaciones, guardar) y la vista 3D con Olimar
// al lado.

#include "config.h"
#include "export_nhm.h"
#include "import.h"
#include "olimar.h"
#include "rig.h"
#include "view.h"

#include "imgui.h"
#include "nectar_assets/charpack.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace mm {

class App {
public:
	bool init(const std::string& openPath, std::string& error);
	void frame();
	void dropFile(const std::string& path);
	bool wantsQuit() const { return quit_; }
	void requestQuit() { quit_ = true; }
	~App();

private:
	// Carpeta del juego y Olimar.
	nm::Config cfg_;
	std::string assetsDir_;
	bool gameReady_ = false;
	OlimarRig olimar_;
	std::vector<Anim> anims_;
	std::vector<int> animOrder_; ///< índices de anims_: primero las conocidas

	// El modelo (y el original, para simplificarlo otra vez).
	bool hasModel_ = false;
	ImpModel model_, source_;
	int targetTris_ = 30000;

	// Abrir y simplificar va en otro hilo (un modelo de 2 millones de
	// triángulos tarda medio minuto).
	std::thread worker_;
	std::atomic<bool> working_ { false }, workDone_ { false };
	ImpModel pending_, pendingSource_;
	std::string pendingError_, pendingPath_;
	bool pendingRestore_ = false, pendingResimplify_ = false;
	FitSettings fit_;
	RigResult rig_;
	ExportOptions exportOpt_;
	ExportResult export_;
	bool rigDirty_ = false, exportDirty_ = false;
	double lastRebuild_ = 0;
	std::vector<std::size_t> order_; ///< triángulos ordenados por material

	// Vista.
	View view_;
	OrbitCamera cam_;
	nm::M4 viewProj_;
	ImVec2 viewOrigin_, viewSize_;
	DynamicMesh modelMesh_, olimarMesh_;
	std::vector<GLuint> modelTex_, olimarTex_;
	/// Olimar, los tres Pikmin y (con Pikmin 2) morado, blanco y Bulbmin,
	/// renderizados desde los assets de los juegos.
	GLuint startPortrait_[7] = {};
	int anim_      = -1; ///< -1: pose de reposo, con los puntos para editar
	float frame_   = 0;
	bool playing_  = true;
	float speed_   = 1.0f;
	int olimarMode_ = 1; ///< 0 oculto, 1 al lado, 2 encima (transparente)
	bool showSkeleton_ = true, showWeights_ = false;
	int dragMarker_ = -1, hoverMarker_ = -1;
	bool orbiting_  = false, panning_ = false;
	bool showStart_ = true;
	int editorStep_ = 0; ///< modelo, colocar, esqueleto, animar, comprobar, publicar

	// A quién reemplaza el modelo: un capitán (Olimar o un personaje propio)
	// o los Pikmin de un color (o los tres, teñidos por el juego).
	// Morado y blanco solo existen en Pikmin 2.
	enum Target { T_CAPTAIN, T_RED, T_YELLOW, T_BLUE, T_ALL, T_PURPLE, T_WHITE, T_BULBMIN, T_COUNT };
	/// Morado, blanco y Bulbmin: solo existen en Pikmin 2.
	static bool targetP2Only(int t) { return t == T_PURPLE || t == T_WHITE || t == T_BULBMIN; }
	/// Se puede elegir: los de los dos juegos con cualquiera instalado; los de
	/// Pikmin 2, con Pikmin 2.
	bool targetAvailable(int t) const { return targetP2Only(t) ? p2Ready_ : (p1Ready_ || p2Ready_); }
	int target_ = T_CAPTAIN;
	LeafModel leaves_[3]; ///< hoja, capullo, flor (los pone el juego en la punta del tallo)
	std::vector<GLuint> leafTex_[3];
	int leafKind_ = 0;
	DynamicMesh leafMesh_;
	std::vector<nectar::CharacterInfo> installedPikmin_;
	bool isPikmin() const { return target_ != T_CAPTAIN; }
	bool loadReference(std::string& error);
	void setTarget(int target);
	void ensureStemMaterial();
	void speciesColour(float out[3]) const;
	void appendLeaf(DynamicMesh& mesh, const Mat34& world, float offsetX, const float tint[3]);
	bool buildStartPortraits(std::string& error);
	void useNowInGame(const nectar::CharacterInfo& info);

	// Personaje propio: su nombre en el selector de capitán, el autor y los
	// que ya hay instalados en el juego (Load/Characters).
	char charName_[41]   = {};
	char charAuthor_[41] = {};
	std::vector<nectar::CharacterInfo> installed_;
	std::string removeId_; ///< el que se va a quitar (pide confirmación)
	// Volver a editar uno ya creado: el id con el que se reinstala (aunque
	// cambie el nombre) y los ajustes de su proyecto para cuando acabe de abrir.
	std::string editingId_, editingName_;
	bool editingOlimar_ = false;
	std::string restoreJson_;
	std::vector<nectar::CharacterInfo> creations_; ///< capitanes y Pikmin instalados (para editar)

	std::string status_;
	bool statusError_ = false;
	bool quit_        = false;
	bool askGameDir_  = false;

	bool setupGame(std::string& error);
	void loadModel(const std::string& path, bool restoreProject);
	void resimplify();
	void finishLoad();
	void rebuild();
	void uploadTextures();
	void fillMeshes();
	void fillModelMesh(DynamicMesh& mesh, const std::vector<Mat34>* skin, bool unbent, bool weights);
	void resetCamera();

	void drawStartScreen(float width, float height);
	void drawTopBar(float width);
	void drawWorkflow(float x, float y, float width, float height);
	void drawPanel(float x, float y, float width, float height);
	void drawView(float x, float y, float width, float height);
	void drawOverlay(const std::vector<Mat34>& skin);
	bool project(const Vec3& p, ImVec2& out) const;
	void dragMarker(const ImVec2& mouse);

	std::string customDir() const;
	std::string projectPath() const;
	void saveProject() const;
	nlohmann::json projectJson(const std::string& source) const;
	/// El proyecto para guardarlo con el personaje: project.json y una copia
	/// del modelo original con todo lo que abrió (texturas, .bin, .mtl).
	void projectFiles(std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& out) const;
	void editCreation(const std::string& dir, const nectar::CharacterInfo& info, bool olimar);
	void openPackForEdit(const std::string& path);
	void drawCreations();
	bool loadProject(std::string& source, int& target);

	bool characterFiles(nectar::CharacterFiles& files, std::string& error);
	bool renderPortrait(std::vector<std::uint8_t>& png);
	/// Guarda en Pikmin (game 0) o en Pikmin 2 (game 1).
	void installAsCharacter(int game);
	/// Los dos juegos (docs/PLAN_P2_CAPITANES_PROPIOS.md): Pikmin si se
	/// encontraron sus assets; Pikmin 2 por su carpeta (la que recuerda el
	/// launcher, ../pikmin2 o la carpeta donde se abrió) y su pikis.szs.
	bool p1Ready_ = false, p2Ready_ = false;
	std::string p2Dir_;
	std::vector<unsigned char> p2Arc_; ///< pikis.szs ya descomprimido
	std::vector<Anim> animsP1_, animsP2_;
	LeafModel leavesP1_[3], leavesP2_[3];
	std::string pikmin2Dir() const;
	std::string pikmin1Dir() const { return p1Ready_ ? cfg_.gameDir() : std::string(); }
	/// Carpeta de trabajo (proyecto del creador): Pikmin, o Pikmin 2 sin él.
	std::string workDir() const { return p1Ready_ ? cfg_.gameDir() : p2Dir_; }
	/// El modelo de referencia de un objetivo: el de Pikmin 1 si está y el
	/// objetivo existe en él; si no, el .bmd de Pikmin 2. `fromP2` dice cuál.
	bool loadRigFor(int target, OlimarRig& rig, bool& fromP2, std::string& error);
	bool setupPikmin2(std::string& error);
	void useAnims(bool p2);
	void exportCharacter();
	void installPack(const std::string& path);
	void refreshInstalled();
	void drawCharacters();
	void say(const std::string& text, bool error = false);
};

} // namespace mm
