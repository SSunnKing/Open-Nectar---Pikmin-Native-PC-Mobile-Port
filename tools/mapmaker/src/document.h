#pragma once

// El mapa que se edita: el MapDoc (map.json), la selección y el historial de
// deshacer/rehacer. Cada cosa que se puede elegir en la vista es un ItemRef.

#include "nectar_mapcore/map.h"

#include <string>
#include <vector>

namespace nm {

// Captain, Ship, StoryOnion y ShipPart son de la misión (solo y coop, sección
// "story"); Base, Rocket, Gate, BombPile, Onion, Piece y BigPiece, del VS.
enum class ItemType { Base, Rocket, Gate, BombPile, Onion, Pellet, Piece, BigPiece, Guard, Prop, Wall, Pond, Enemy, Object, Gen,
	                  Captain, Ship, StoryOnion, ShipPart };

struct ItemRef {
	ItemType type = ItemType::Base;
	int index     = 0; ///< jugador (bases, cohetes, compuertas, bombas, cebollas) o posición en la lista
	int sub       = 0; ///< color de la cebolla
	bool operator==(const ItemRef& o) const { return type == o.type && index == o.index && sub == o.sub; }
};

/// Transformación común para el gizmo y el panel de propiedades.
struct Xform {
	double x = 0, y = 0, z = 0;
	double rx = 0, ry = 0, rz = 0; ///< grados
	double scale = 1;
};

/// Qué se puede tocar de cada tipo.
struct ItemCaps {
	bool rotateY = false, rotateAll = false, scale = false, height = false;
	bool removable = false; ///< está en una lista (se puede borrar y duplicar)
	bool perPlayer = false; ///< tiene dueño (J1/J2)
	bool twinned   = false; ///< con simetría, su gemelo de J2 se genera solo
};
ItemCaps itemCaps(ItemType t);

bool itemExists(const nectar::MapDoc& d, const ItemRef& r);
Xform getXform(const nectar::MapDoc& d, const ItemRef& r);
void setXform(nectar::MapDoc& d, const ItemRef& r, const Xform& x);
std::string itemName(const nectar::MapDoc& d, const ItemRef& r);
/// Nombre para mostrar de un modelo del juego ("Bloque de roca"); nullptr si
/// no está en la lista de decoración.
const char* friendlyAssetName(const std::string& path);

/// Decoración que se ofrece de primeras: (ruta, nombre, grupo).
struct DecorItem {
	const char* path;
	const char* name;
	const char* group;
};
const std::vector<DecorItem>& decorItems();

/// Todo lo que hay en el mapa, en orden (para la lista de capas). Lo del VS,
/// solo si el mapa es para VS; lo de la misión, solo si tiene sección "story".
std::vector<ItemRef> allItems(const nectar::MapDoc& d);
/// Es de la misión (solo y coop) / del VS.
bool isStoryItem(ItemType t);
bool isVsItem(ItemType t);
/// Prepara la sección "story" (capitanes, nave, una cebolla roja) cerca del
/// centro o de la salida del Jugador 1, si aún no la tiene; en coop, con dos
/// capitanes.
void ensureStory(nectar::MapDoc& d, bool coop);

/// El otro jugador del mismo marcador (J1 <-> J2), si lo tiene.
bool counterpart(const ItemRef& r, ItemRef& out);
/// Coloca el del otro jugador girado 180° alrededor del centro.
void mirrorToCounterpart(nectar::MapDoc& d, const ItemRef& r);

/// Añade uno nuevo en (x, z) y devuelve cuál es.
ItemRef addItem(nectar::MapDoc& d, ItemType t, double x, double z, const std::string& asset = "");
/// Borra los de las listas (los demás no se pueden quitar). Ajusta índices.
void removeItems(nectar::MapDoc& d, std::vector<ItemRef> items);
/// Copia los elementos de lista a un MapDoc "portapapeles" (solo sus listas).
nectar::MapDoc copyItems(const nectar::MapDoc& d, const std::vector<ItemRef>& items);
/// Pega desplazado y devuelve lo pegado.
std::vector<ItemRef> pasteItems(nectar::MapDoc& d, const nectar::MapDoc& clip, double dx, double dz);

class EditorDoc {
public:
	nectar::MapDoc doc;
	std::string path; ///< map.json en disco; vacío si no se ha guardado
	bool dirty = false;
	std::vector<ItemRef> selection;
	int generation = 0; ///< sube con cada cambio confirmado (para recompilar)
	int liveGeneration = 0; ///< sube con cada cambio, también a medias

	void reset(const nectar::MapDoc& d, const std::string& p);
	/// Algo ha cambiado `doc` (se confirma cuando acaba la interacción).
	void touch();
	/// Confirma lo pendiente si ya no se está arrastrando ni escribiendo.
	void commit(bool interacting);
	bool canUndo() const { return !undo_.empty(); }
	bool canRedo() const { return !redo_.empty(); }
	void undo();
	void redo();
	/// Historial: qué fue cada paso que se puede deshacer (0 = el último).
	int undoCount() const { return int(undoLabels_.size()); }
	const std::string& undoLabel(int i) const { return undoLabels_[undoLabels_.size() - 1 - std::size_t(i)]; }
	/// Nombre del cambio que se está haciendo (se guarda con él al confirmarlo).
	std::string label;

	bool selected(const ItemRef& r) const;
	void select(const ItemRef& r, bool add);
	void clearSelection() { selection.clear(); }
	/// Quita de la selección lo que ya no existe.
	void pruneSelection();

private:
	nectar::MapDoc stable_;
	bool pending_ = false;
	std::vector<nectar::MapDoc> undo_, redo_;
	std::vector<std::string> undoLabels_, redoLabels_;
};

} // namespace nm
