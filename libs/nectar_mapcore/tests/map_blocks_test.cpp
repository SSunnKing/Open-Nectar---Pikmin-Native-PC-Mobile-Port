// Bloques de terreno (issue #376): ida y vuelta por el map.json y despliegue
// (tipo, giro en radianes, gemelo del mapa espejo). No necesita el disco.

#include "nectar_mapcore/map.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

bool near(double a, double b) { return std::fabs(a - b) < 1e-3; }

} // namespace

int main()
{
	using namespace nectar;
	MapDoc doc    = defaultMapDoc();
	doc.symmetric = false;
	DocBlock water;
	water.kind = "water";
	water.x    = 120;
	water.y    = -40;
	water.z    = -80;
	water.sx   = 500;
	water.sy   = 35;
	water.sz   = 260;
	water.ry   = 30;
	DocBlock wall;
	wall.kind              = "wall";
	wall.material.source   = "courses/practice/practice.mod";
	wall.material.index    = 0;
	wall.material.attribute = 1;
	doc.blocks             = { water, wall };

	DocProp collision;
	collision.asset       = "map:meshes/level_col.mod";
	collision.ground      = true;
	collision.visible     = false;
	collision.importScale = 2.5;
	doc.props.push_back(collision);
	DocProp painted;
	painted.asset        = "map:meshes/level.mod";
	painted.paintPalette = { DocMaterial { "courses/practice/practice.mod", 0, 1, 0 } };
	painted.paint        = { 0, 1, 1, 0, 1 };
	doc.props.push_back(painted);

	MapDoc back;
	std::string error;
	check(parseMapDoc(writeMapDoc(doc), back, error), "the map with blocks parses back");
	check(back.blocks.size() == 2, "two blocks after the round trip");
	if (back.blocks.size() == 2) {
		const DocBlock& b = back.blocks[0];
		check(b.kind == "water" && near(b.x, 120) && near(b.y, -40) && near(b.z, -80), "water block position and kind");
		check(near(b.sx, 500) && near(b.sy, 35) && near(b.sz, 260) && near(b.ry, 30), "water block size and rotation");
		check(back.blocks[1].kind == "wall" && back.blocks[1].material.index == 0 && back.blocks[1].material.attribute == 1,
		      "wall block keeps its material");
	}

	check(back.props.size() >= 2 && !back.props[back.props.size() - 2].visible && near(back.props[back.props.size() - 2].importScale, 2.5),
	      "a collision-only mesh keeps visible = false and its import scale");
	check(!back.props.empty() && back.props.back().paint == std::vector<std::uint8_t> { 0, 1, 1, 0, 1 } && back.props.back().paintPalette.size() == 1
	          && back.props.back().paintPalette[0].attribute == 1,
	      "mesh paint survives the round trip");

	MapData map;
	check(expandMap(back, map, error, false), "the map with blocks expands");
	check(map.props.size() >= 2 && !map.props[map.props.size() - 2].visible, "visible = false reaches the expanded prop");
	check(!map.props.empty() && map.props.back().paint.size() == 5 && (map.props.back().paintPalette[0].code >> 29) == 1u,
	      "mesh paint reaches the expanded prop with its terrain code");
	check(map.blocks.size() == 2, "one expanded block per block without symmetry");
	if (map.blocks.size() == 2) {
		check(map.blocks[0].kind == MAPBLOCK_Water && map.blocks[1].kind == MAPBLOCK_Wall, "block kinds expand");
		check(near(map.blocks[0].ry, 30 * 3.14159265 / 180), "rotation expands to radians");
		check((map.blocks[1].material.code >> 29) == 1u && !(map.blocks[1].material.code & 0x02000000u), "a wall block is rock, not floor");
	}

	back.symmetric = true;
	check(expandMap(back, map, error, false), "the mirrored map expands");
	check(map.blocks.size() == 4, "the mirrored map twins every block");
	if (map.blocks.size() == 4) check(near(map.blocks[1].x, -120) && near(map.blocks[1].z, 80), "the twin sits turned round the centre");

	// Un JSON con un tipo que no existe se rechaza.
	std::string bad = writeMapDoc(doc);
	const std::size_t at = bad.find("\"water\"");
	if (at != std::string::npos) bad.replace(at, 7, "\"lava\"");
	check(!parseMapDoc(bad, back, error), "an unknown block kind is rejected");

	if (failures) return 1;
	std::printf("map_blocks_test: OK\n");
	return 0;
}
