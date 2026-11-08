#include "nectar_assets/standin_world.h"

#include <string_view>

// Independently authored loader fixtures. Geometry is a simple grid of spheres
// and boxes, not a reconstruction of retail assets. Names below are runtime
// file/part contracts; dimensions, topology and joint budget are test choices.
namespace nectar {
namespace {
std::vector<int> starJoints(int count)
{
    std::vector<int> parents(count, 0);
    parents[0] = -1;
    return parents;
}
WorldPart sphere(const char* id, int depth = 0, int joint = 0, Vec3 centre = {})
{
    return { depth, joint, id, "none", 1, 8.0f, centre, nullptr };
}
WorldModelProfile body(const char* path)
{
    // A generous fixed test joint budget accommodates indexed source users;
    // it deliberately does not track any retail model's skeleton.
    WorldModelProfile m { path, starJoints(64), {} };
    m.parts = { sphere("bnds"), sphere("boun"), sphere("cent") };
    m.parts.push_back(sphere("slot"));
    for (int i = 0; i < 4; ++i) m.parts.push_back(sphere("none", 1, i + 1, {float(i * 8), 8.0f, 0.0f}));
    return m;
}
}

const std::vector<WorldModelProfile>& worldModelProfiles()
{
    static const std::vector<WorldModelProfile> list = [] {
        std::vector<WorldModelProfile> out;
        for (const char* path : {
            "dataDir/objects/plants/chidome.mod", "dataDir/objects/plants/clover.mod",
            "dataDir/objects/plants/hae.mod", "dataDir/objects/plants/hutaba.mod",
            "dataDir/objects/plants/ine.mod", "dataDir/objects/plants/tanpopo.mod" }) {
            out.push_back({path, starJoints(8), {sphere("bnds"), sphere("cent")}});
        }
        // Bridge's joint-segment path consumes pairs of root children, each
        // containing a child joint (workObject.cpp, Bridge constructor).
        for (const char* path : {"dataDir/objects/bridge/brd_long.mod", "dataDir/objects/bridge/brd_test.mod"}) {
            WorldModelProfile m {path, {-1}, {}};
            for (int i = 0; i < 8; ++i) {
                int parent = int(m.parents.size());
                m.parents.push_back(0);
                m.parents.push_back(parent);
            }
            out.push_back(std::move(m));
        }
        // HinderRock::getVertex requires these four named corners.
        out.push_back({"dataDir/objects/hinderrock/cube10.mod", {-1}, {
            sphere("vtx0", 0, 0, {-32.0f, 0.0f, 32.0f}),
            sphere("vtx1", 0, 0, {32.0f, 0.0f, 32.0f}),
            sphere("vtx2", 0, 0, {32.0f, 0.0f, -32.0f}),
            sphere("vtx3", 0, 0, {-32.0f, 0.0f, -32.0f}) }});
        WorldModelProfile gate {"dataDir/objects/gate/gt_soft.mod", starJoints(8), {
            {0, 0, "gate", "none", 2, 0.0f, {}, "objects/gate/centcoll.mod"},
            {0, 0, "side", "none", 2, 0.0f, {}, "objects/gate/sidecoll.mod"},
            sphere("flag"), sphere("none", 1, 0, {-24.0f, 8.0f, 0.0f}),
            sphere("none", 1, 0, {0.0f, 8.0f, 0.0f}), sphere("none", 1, 0, {24.0f, 8.0f, 0.0f}) }};
        out.push_back(std::move(gate));
        out.push_back(body("dataDir/bosses/mizu/mizu.mod"));
        auto snake = body("dataDir/bosses/snake/snake.mod");
        // Snake::init requires bnds and a seven-segment tube child chain.
        snake.parts.push_back(sphere("tube"));
        for (int i = 1; i <= 7; ++i) snake.parts.push_back(sphere("none", i, i, {0.0f, float(i * 8), 0.0f}));
        out.push_back(std::move(snake));
        for (const char* path : {"dataDir/tekis/beatle/beatle.mod", "dataDir/tekis/iwagon/iwagon.mod",
             "dataDir/tekis/kabekuiA/kabekuiA.mod", "dataDir/tekis/kabekuiB/kabekuiB.mod",
             "dataDir/tekis/otama/otama.mod", "dataDir/tekis/palm/palm.mod", "dataDir/tekis/swallow/swallow.mod"}) {
            auto m = body(path);
            const std::string_view model(path);
            if (model == "dataDir/tekis/beatle/beatle.mod") {
                // BTeki allocates 22 collision parts. Keep the required action
                // targets and platforms within that capacity.
                m.parts.resize(4);
                for (const char* id : {"kuti", "me_l", "me_r", "stm0", "stm1", "stm2", "stm3", "stm4", "stm5"})
                    m.parts.push_back(sphere(id, 0, 0, {0.0f, 8.0f, 0.0f}));
                // TAIbeatleStrategy's cloud-of-dust action requires all four spheres.
                m.parts.push_back(sphere("lhnd", 0, 1, {-8.0f, 0.0f, 8.0f}));
                m.parts.push_back(sphere("rhnd", 0, 2, {8.0f, 0.0f, 8.0f}));
                m.parts.push_back(sphere("lfot", 0, 3, {-8.0f, 0.0f, -8.0f}));
                m.parts.push_back(sphere("rfot", 0, 4, {8.0f, 0.0f, -8.0f}));
                // Platform codes: CollPart matches only the first letter ('s' stickable,
                // 'b' bouncy, collInfo.cpp isStickable/isBouncy); the rest is this file's
                // own. The platform file names stay on the boot manifest's list.
                m.parts.push_back({0, 0, "none", "sbod", 2, 0.0f, {}, "tekis/beatle/plat_body.mod"});
                m.parts.push_back({0, 1, "none", "bwng", 2, 0.0f, {}, "tekis/beatle/plat_wing_left.mod"});
                m.parts.push_back({0, 2, "none", "bwng", 2, 0.0f, {}, "tekis/beatle/plat_wing_right.mod"});
                m.parts.push_back({0, 3, "none", "none", 2, 0.0f, {}, "tekis/beatle/plat_nozzle.mod"});
            } else if (model == "dataDir/tekis/swallow/swallow.mod") {
                m.parts.push_back({0, 0, "pla1", "sbod", 2, 0.0f, {}, "tekis/swallow/plat_body.mod"});
                m.parts.push_back({0, 1, "pla2", "shed", 2, 0.0f, {}, "tekis/swallow/plat_head.mod"});
            } else {
                for (const char* id : {"head", "tube", "look", "flwr", "hana", "bomb", "lleg", "rleg"})
                    m.parts.push_back(sphere(id, 0, 0, {0.0f, 8.0f, 0.0f}));
            }
            out.push_back(std::move(m));
        }
        return out;
    }();
    return list;
}

const std::vector<std::string>& worldPlatformPaths()
{
    static const std::vector<std::string> list = [] {
        std::vector<std::string> paths;
        for (const WorldModelProfile& model : worldModelProfiles()) {
            for (const WorldPart& part : model.parts) {
                if (part.type == 2 && std::string_view(part.platform).substr(0, 6) == "tekis/")
                    paths.push_back(std::string("dataDir/") + part.platform);
            }
        }
        return paths;
    }();
    return list;
}

const std::vector<TekiParaProfile>& tekiParaProfiles()
{
    // Missing files retain the per-type constructors (CoreNode::load).
    // An empty positional file would be unsafe: TekiParameters::read expects
    // a version, eight IDs, then each type's integer and float parameter counts.
    static const std::vector<TekiParaProfile> list;
    return list;
}

const std::vector<BossParmProfile>& bossParmProfiles()
{
    // CreatureProp, BossProp and subclass tagged lists, with no overrides.
    static const std::vector<BossParmProfile> list = {
        {"dataDir/bosses/mizu/parms.bin", {{}, {}, {}}},
        {"dataDir/bosses/snake/parms.bin", {{}, {}, {}}},
    };
    return list;
}
} // namespace nectar
