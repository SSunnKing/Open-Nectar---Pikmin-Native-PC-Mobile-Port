#pragma once

// Nectar Map Maker: pantalla de inicio (pide la carpeta assets y prepara el
// catálogo), bienvenida y el editor. Pensado para que cualquiera lo entienda:
// herramientas con nombres claros, ayuda en pantalla, tutorial y una lista de
// lo que falta para poder jugar.

#include "config.h"
#include "course_import.h"
#include "templates.h"
#include "document.h"
#include "math3d.h"
#include "preview.h"
#include "renderer.h"

#include "nectar_assets/catalog.h"
#include "nectar_mapcore/campaign.h"

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

struct ImVec2;

namespace nm {

class App {
public:
	App();
	~App();
	/// `openPath`: un map.json para abrir directamente (vacío = bienvenida).
	bool init(const std::string& openPath, std::string& error);
	/// Un fotograma de interfaz (entre NewFrame y Render de ImGui).
	void frame();
	/// La ventana pide cerrarse: true si se puede (si no, pregunta).
	bool requestQuit();
	bool wantsQuit() const { return quit_; }

private:
	enum class Screen { Setup, Building, Welcome, Editor, Campaign };
	enum class Tool { Move, Place, Floor, Height, Plateau, Paint, Water, Walls, Ramp, Scatter, Trace };
	enum class TransformMode { Move, Rotate, Scale };
	enum class HeightOp { Raise, Lower, Flatten, Smooth, Noise, Hill, Crater, Mesa };
	enum class BrushShape { Circle, Square };
	enum class BrushFalloff { Smooth, Hard };

	struct Rect {
		float x = 0, y = 0, w = 0, h = 0;
	};
	struct Toast {
		int level; // 0 bien, 1 aviso, 2 error
		std::string text;
		double until;
	};
	enum class IssueFix { None, ClampInside, NearestGround, DeleteItem, NormalizeScale };
	struct Issue {
		bool blocking;
		std::string text;
		bool hasItem;
		ItemRef item;
		IssueFix fix = IssueFix::None;
	};
	/// Lo que se coloca con «Poner cosas».
	struct PlaceChoice {
		ItemType type = ItemType::Piece;
		std::string asset; // decoración
		std::string label;
	};

	// ── Pantallas ──
	void drawSetup();
	void drawBuilding();
	void drawWelcome();
	void drawStartHome();      ///< paso 1: modo, abrir, importar y recientes
	void drawStartNew();       ///< paso 2: plantillas del modo elegido
	bool modeCard(int mode, float w, float h);
	void recentList(float w);
	void applyStartMode();     ///< pone el modo elegido al mapa recién creado
	void rememberRecent(const std::string& path);
	void openMapFile();
	void startCatalog();
	void enterEditor();
	bool shapeButton(const char* id, nectar::TerrainShape shape, const char* label, float size);

	// ── Editor ──
	void drawEditor();
	void drawTopBar(const Rect& r);
	void drawToolHeader(const Rect& r);      ///< fila de opciones de la herramienta, sobre el visor
	void drawStatusBar(const Rect& r);       ///< abajo: ratón, coordenadas, selección, guardado y presupuesto
	void drawReadyPill();                    ///< «Listo para jugar» / «N por arreglar», con la lista al pulsar
	void drawHistoryMenu();                  ///< historial de deshacer
	void drawNavGizmo(const ImVec2& origin, const ImVec2& size, bool& hovered); ///< brújula de vistas
	void drawIssueMarkers(const ImVec2& origin, const ImVec2& size, bool& hovered); ///< avisos en el mundo
	void drawRecentsBar(const ImVec2& origin, const ImVec2& size, bool& hovered);   ///< lo último puesto
	void drawRadialMenu();                   ///< Mayús+A: lo último puesto en un círculo
	void drawCatalogDrawer(const Rect& r);   ///< «Poner cosas» a lo ancho, bajo el visor
	void drawGrid();                         ///< cuadrícula de encaje bajo el ratón
	void viewWholeMap();
	void rememberPlace(const PlaceChoice& c);
	std::vector<PlaceChoice> placeRecents() const;
	const std::vector<Issue>& issues();      ///< checkMap(), recalculado solo si cambia el mapa
	const char* toolName(Tool t) const;
	void drawToolbox(const Rect& r);
	void drawViewport(const Rect& r);
	void drawSidePanel(const Rect& r);
	void drawSelected();
	void drawMapPanel();
	void drawMinimap(const ImVec2& o, float w, float h, bool& hovered); ///< en el visor, abajo a la derecha
	void drawHierarchy();
	void drawChecklist();
	void drawTechnicalReport();
	void drawModals();
	void drawTutorial();
	void drawHelp();
	void drawMaterialBrowser();
	void drawToolOptions();
	void drawPlaceOptions();
	void drawFloorOptions();
	void drawHeightOptions();
	void drawPlateauOptions();
	void drawPaintOptions();
	void drawWaterOptions();
	void drawWallOptions();
	void drawRampOptions();
	// Trazo: caminos, ríos y calzadas por una línea de puntos, o zonas de
	// suelo (y huecos) con un contorno dibujado a mano.
	enum class TraceMode { Path, River, Causeway, Area, Hole, RouteLink, RouteBlock };
	void drawTraceOptions();
	void applyTrace();
	void paintCliffs();
	std::vector<V3> traceNodes_;
	TraceMode traceMode_ = TraceMode::Path;
	float traceWidth_ = 120.0f, traceDepth_ = 40.0f, cliffSlope_ = 1.0f;
	void drawScatterOptions();
	void drawBrushSettings();
	void scatter(const V3& at);
	void drawSwatches(bool allowAdd);
	void handleShortcuts();
	const char* toolHelp() const;

	// ── Vista ──
	void renderScene(int w, int h);
	void drawItem(const ItemRef& r, bool ghost);
	void drawSelection();
	void drawSlopes();
	void drawWater();
	void drawRoutes();
	void drawLabels(const ImVec2& origin, const ImVec2& size);
	void drawMoveGizmo(const ImVec2& origin, const ImVec2& size);
	void viewportMouse(const Ray& ray, const ImVec2& origin, const ImVec2& size);
	bool selectionPivot(V3& pivot) const;
	bool selectionCanRotate() const;
	bool selectionCanScale() const;
	V3 moveAxis(int axis) const;
	int moveGizmoAxis(const ImVec2& origin, const ImVec2& size, const ImVec2& mouse) const;
	bool pick(const Ray& ray, ItemRef& hit, float& t, int cycle = 0);
	bool groundHit(const Ray& ray, V3& hit) const;
	bool terrainRay(const Ray& ray, V3& hit) const;
	float docHeight(float x, float z) const;
	bool docSolid(float x, float z) const;
	bool project(const V3& p, const ImVec2& origin, const ImVec2& size, ImVec2& out) const;

	// ── Terreno ──
	bool ensureTerrain();
	void applyBrush(const V3& at, float dt);
	bool applyRamp(const V3& from, const V3& to);
	void growMap(int dx, int dz);
	std::shared_ptr<nectar::Model> courseModel(const std::string& source);
	unsigned swatch(const std::string& source, int index);

	// ── Recursos ──
	std::shared_ptr<GpuModel> model(const std::string& asset);
	/// Recorte de un escenario ("cut:..."), ya sacado y leído (nullptr si no se puede).
	std::shared_ptr<nectar::Model> cutModel(const std::string& asset);
	unsigned cutThumb(const std::string& asset);
	void drawCutOptions();
	/// Con mallas que se pisan: la altura del suelo sale del mapa compilado.
	CourseField meshField_;
	bool hasGroundProps() const;
	bool cutBox_ = false;
	V3 cutLo_ { -200, -100, -200 }, cutHi_ { 200, 400, 200 };
	char cutName_[64] = "";
	std::map<std::string, std::shared_ptr<nectar::Model>> cutModels_;
	/// Carpeta del mapa abierto (vacía si no se ha guardado).
	std::string mapDir() const;
	/// Archivo en disco de un modelo: del juego o "map:..." de la carpeta del mapa.
	std::string assetFile(const std::string& asset) const;
	/// El documento para compilar (con su carpeta, para las mallas propias).
	nectar::MapDoc docToCompile() const;
	std::string markerAsset(const ItemRef& r) const;
	std::string placeAsset(const PlaceChoice& c) const;
	Rgba markerTint(const ItemRef& r) const;
	M4 itemMatrix(const ItemRef& r, bool twin) const;
	unsigned thumb(const std::string& path);
	unsigned assetThumb(const std::string& asset);

	// ── Campañas (user/campaigns/<id>/) ──
	std::string campaignsDir() const;
	void drawCampaignPickers();
	void drawCampaign();
	void drawCampaignCanvas(const ImVec2& origin, const ImVec2& size);
	void drawCampaignPanel();
	void drawCampaignZone(int zone);
	bool drawUnlockList(std::vector<nectar::UnlockRule>& rules, const char* id);
	void newCampaign(const std::string& name);
	void openCampaign(const std::string& dir);
	bool saveCampaign();
	void addCampaignZone();
	void removeCampaignZone(int zone);
	void editCampaignZone(int zone);
	void testCampaignZone(int zone);
	void checkCampaign();
	unsigned campaignMapTexture(int& w, int& h);
	void writeCampaignThumb();
	/// Copia una PNG elegida al paquete con ese nombre; true si se copió.
	bool pickPackImage(const char* title, const std::string& as, std::string& field);
	/// Exporta una carpeta (campaña o mapa) a un .nectarpack, eligiendo dónde.
	void exportPackOf(const std::string& dir);

	// ── Archivo ──
	void newMap(nectar::TerrainShape shape, double scale = 1.0, bool vsWalls = true); ///< scale: tamaño respecto al de siempre (3200 × 1800); vsWalls: las murallas de las bases
	/// Mapa sin suelo propio: el suelo es un nivel entero de un .obj o .dae.
	void newMapFromModel();
	bool coursePickers();
	bool templatePickers(float size, unsigned modes = ~0u); ///< solo las plantillas de esos modos
	unsigned templateThumb(const TemplateInfo& t);
	bool courseMode() const { return !ed_.doc.course.empty(); }
	void openMap(const std::string& path);
	void openBuiltIn();
	bool saveMap(const std::string& path);
	void saveOrAsk(bool thenTest);
	/// Prueba el mapa en el juego en testMode_ (o en el primer modo que tenga).
	void testInGame(bool fromHere = false); ///< fromHere: el capitán empieza donde mira la cámara
	unsigned testMode_ = nectar::MAPMODE_VS;
	void autosave();
	void notify(int level, const std::string& text);
	void refreshOpenList();

	// ── Edición ──
	void deleteSelection();
	void duplicateSelection();
	void copySelection();
	void paste();
	void focusSelection();
	void rotateSelection(double degrees);
	void liftSelection(double dy);
	void placeSelectionOnGround();
	bool selectionCanGround() const;
	bool itemHidden(const ItemRef& r) const;
	bool itemLocked(const ItemRef& r) const;
	void toggleHidden(const ItemRef& r);
	void toggleLocked(const ItemRef& r);
	void remapSessionItemsAfterRemoval(const std::vector<ItemRef>& removed);
	void placeAt(const V3& at);
	std::vector<Issue> checkMap() const;
	void applyIssueFix(const Issue& issue);
	void createGroundUnder(const ItemRef& item);
	double snap(double v) const;

	Config cfg_;
	Renderer renderer_;
	RenderTarget target_;
	Camera cam_;
	Screen screen_ = Screen::Setup;
	bool quit_ = false, confirmQuit_ = false;

	// Inicio
	char assetsBuf_[1024] = {};
	char gameBuf_[1024]   = {};

	// Catálogo
	nectar::Catalog catalog_;
	std::thread catalogThread_;
	std::atomic<int> catalogDone_ { 0 }, catalogTotal_ { 0 };
	std::atomic<bool> catalogFinished_ { false };
	std::string catalogError_;
	std::map<std::string, unsigned> thumbs_;
	int thumbLoadsThisFrame_ = 0;
	char search_[128]        = {};
	char hierarchySearch_[128] = {};

	// Modelos ya subidos a la GPU (marcadores y decoración).
	std::map<std::string, std::shared_ptr<GpuModel>> models_;
	std::map<std::string, std::string> modelErrors_;

	// Documento
	EditorDoc ed_;
	nectar::MapDoc clipboard_;
	bool hasClipboard_ = false;
	Xform transformClipboard_;
	ItemCaps transformClipboardCaps_;
	bool hasTransformClipboard_ = false;
	Preview preview_;
	PreviewResult compiled_;
	std::shared_ptr<GpuModel> baseGpu_;
	std::vector<V3> waterTris_; ///< superficie del agua del mapa compilado
	std::vector<V3> collisionTris_; ///< geometría compilada que usa el juego
	CourseField courseField_;   ///< suelo del escenario del juego (mapas con escenario)
	int requestedLive_  = -1;
	double lastRequest_ = 0;
	std::vector<Toast> toasts_;
	double lastAutosave_ = 0;

	// Herramientas
	Tool tool_         = Tool::Move;
	TransformMode transformMode_ = TransformMode::Move;
	HeightOp heightOp_ = HeightOp::Raise;
	BrushShape brushShape_ = BrushShape::Circle;
	BrushFalloff brushFalloff_ = BrushFalloff::Smooth;
	float brushRadius_ = 150.0f, brushStrength_ = 150.0f, flattenTarget_ = 0.0f;
	bool floorErase_ = false, brushValid_ = false, brushWasDown_ = false;
	bool waterErase_ = false, plateauUp_ = true;
	float plateauAmount_ = 100.0f;
	int paintMaterial_ = 0;
	V3 brushPos_;
	PlaceChoice place_;
	/// Arrastrar desde el catálogo al mapa (carga «NM_PLACE»): lo que lleva el ratón.
	struct PlacePayload {
		int type;
		char asset[200];
		char label[48];
	};
	void beginPlaceDrag(const PlaceChoice& c, unsigned thumb, const std::string& name, int flags = 0); ///< flags: ImGuiDragDropFlags
	bool drawerDragArmed_ = false; ///< se pulsó una casilla de la barra de recientes
	PlaceChoice drawerDragChoice_;
	bool placeValid_ = false;
	V3 placePos_;
	float wallHeight_ = 150.0f, wallThickness_ = 40.0f;
	// Pincel de decoración
	std::vector<std::string> scatterSet_ { "objects/weeds/weed1.mod", "objects/plants/hutaba.mod", "objects/plants/hae.mod" };
	float scatterDensity_ = 6.0f, scatterSizeVar_ = 0.25f;
	bool scatterErase_ = false, scatterCollide_ = false, scatterFullWarned_ = false;
	unsigned scatterSeed_ = 12345u;
	bool drawingWall_ = false;
	V3 wallStart_;
	bool drawingRamp_ = false, rampPreviewValid_ = false;
	V3 rampStart_, rampEnd_;
	float rampWidth_ = 250.0f;
	bool rampSmooth_ = true;
	// Arrastre con «Mover»
	bool dragging_ = false, dragArmed_ = false;
	bool boxSelecting_ = false;
	int dragHandle_ = -1; // extremo del muro que se arrastra (0 A, 1 B) o -1
	int dragAxis_ = 0; ///< 0 libre, 1 eje X, 2 eje Z, 3 giro Y, 4 escala uniforme
	V3 dragGrab_;
	V3 transformPivot_;
	float rotateStartAngle_ = 0;
	float scaleStartDistance_ = 1;
	float pressX_ = 0, pressY_ = 0, boxEndX_ = 0, boxEndY_ = 0;
	float lastAltPickX_ = -1000, lastAltPickY_ = -1000;
	int altPickCycle_ = 0;
	std::vector<std::pair<ItemRef, Xform>> dragStart_;
	std::vector<ItemRef> boxSelectionBase_;
	std::vector<ItemRef> hiddenItems_, lockedItems_; ///< solo sesión; nunca se guardan en map.json

	// Materiales
	bool openMatBrowser_  = false;
	int matBrowserTarget_ = -1; // -1 añadir a la paleta, -2 paredes del borde, k cambiar el k
	std::string matBrowserCourse_;
	std::map<std::string, std::shared_ptr<nectar::Model>> courseModels_;
	std::map<std::string, unsigned> swatches_;
	std::map<std::string, std::vector<std::array<int, 3>>> courseStats_; // (material, triángulos, de suelo)

	// Vista
	bool showTwins_ = true, mirrorEdits_ = true;
	int viewDay_ = 0; ///< misiones: ver solo lo que hay ese día (0 = todo)
	bool showCollision_ = false, showRoutes_ = false, showSlopesOverlay_ = false, showWaterOverlay_ = true, showLabels_ = false;
	bool viewportHovered_ = false;
	int splitterDrag_     = 0; ///< 0 ninguno, 1 herramientas, 2 propiedades
	float viewAspect_     = 1.0f;
	Rect viewRect_, toolboxRect_, checklistRect_, testButtonRect_, floorButtonRect_, placeButtonRect_;

	// Diálogos y ayuda
	bool openSaveAs_ = false, openOpen_ = false, openNew_ = false, openHelp_ = false, testAfterSave_ = false;
	char saveName_[128] = {};
	std::vector<std::pair<std::string, std::string>> openList_; // (nombre, ruta)
	int tutorialStep_ = -1;
	std::string openAtStart_;
	// Interfaz: barra de estado, avisos, recientes y catálogo.
	std::vector<Issue> issuesCache_;
	int issuesGen_ = -1, issuesCompiledGen_ = -1;
	V3 hoverPos_;
	bool hoverValid_ = false;
	double lastSaved_ = -1;
	bool catalogOpen_ = false;
	bool focusToolTab_ = false;
	bool showMinimap_ = true; ///< abrir la pestaña «Herramienta» del panel derecho
	bool radialOpen_ = false;
	float radialX_ = 0, radialY_ = 0;
	unsigned startMode_ = 0; ///< inicio: 0 = elegir modo; si no, MapMode o kStartCampaign
	int startSize_ = 1; ///< inicio: tamaño del mapa en blanco (0 pequeño, 1 mediano, 2 grande)
	static constexpr unsigned kStartCampaign = 8u;

	// Campaña en edición
	nectar::CampaignDoc camp_;
	std::string campDir_;        ///< carpeta del paquete; vacía si no hay campaña abierta
	bool campDirty_    = false;
	int campZone_      = -1;     ///< zona elegida
	int campDrag_      = -1;     ///< zona que se arrastra
	int campLinkFrom_  = -1;     ///< «unir»: la primera zona elegida
	int campPoi_       = -1;     ///< punto de interés elegido
	// «Probar con un estado»
	bool campTestState_ = false;
	int campTestDay_ = 1, campTestParts_ = 0;
	int campTestPikmin_[3]   = { 0, 20, 0 };
	bool campTestOnions_[3]  = { false, true, false };
	std::string campCheck_;      ///< lo que dice la comprobación (vacío = bien)
	bool campCheckDirty_ = true;
	unsigned campTex_   = 0;     ///< la imagen del mapa en la GPU
	std::string campTexKey_;
	int campTexW_ = 4, campTexH_ = 3;
	std::string editingZoneOf_;  ///< editando el nivel de una zona: la campaña a la que volver
	char campNewName_[96] = {};
	bool openNewCampaign_ = false;
	std::vector<std::pair<std::string, std::string>> campaignList_; // (nombre, carpeta)
};

} // namespace nm
