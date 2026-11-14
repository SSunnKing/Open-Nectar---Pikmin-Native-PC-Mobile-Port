#pragma once
// --launch=pikmin1|pikmin2 (#336): start one game straight away, without the
// home screen. Parsing and the launch-or-home decision live here, without
// the filesystem, so launch_request_test can cover them.

#include <string>

namespace pikmin::launcher {

enum class LaunchTarget { None, Pikmin1, Pikmin2, Invalid };

inline LaunchTarget parseLaunchTarget(const std::string& value)
{
    if (value == "pikmin1") return LaunchTarget::Pikmin1;
    if (value == "pikmin2") return LaunchTarget::Pikmin2;
    return LaunchTarget::Invalid;
}

// Reads "--launch=NAME" or "--launch NAME" at argv[i]. Returns false when
// argv[i] is some other option. On true, `i` points at the last argument
// used, `target` holds the game (Invalid for an unknown or missing name) and
// `value` holds the name as typed, for the error message.
inline bool consumeLaunchArgument(int& i, int argc, char** argv, LaunchTarget& target, std::string& value)
{
    const std::string arg = argv[i];
    const std::string prefix = "--launch=";
    if (arg.compare(0, prefix.size(), prefix) == 0) {
        value = arg.substr(prefix.size());
    } else if (arg == "--launch") {
        if (i + 1 >= argc || std::string(argv[i + 1]).compare(0, 2, "--") == 0) {
            value.clear();
            target = LaunchTarget::Invalid;
            return true;
        }
        value = argv[++i];
    } else {
        return false;
    }
    target = parseLaunchTarget(value);
    return true;
}

// The argument to pass on when another launcher takes over (the downloaded
// one hands off to the installed one). Empty for None and Invalid.
inline std::string launchArgument(LaunchTarget target)
{
    switch (target) {
    case LaunchTarget::Pikmin1: return "--launch=pikmin1";
    case LaunchTarget::Pikmin2: return "--launch=pikmin2";
    default: return std::string();
    }
}

inline const char* launchTargetTitle(LaunchTarget target)
{
    return target == LaunchTarget::Pikmin2 ? "Pikmin 2" : "Pikmin";
}

// What the launcher knows about the requested game.
struct LaunchFacts {
    bool assetsInstalled = false;   // assets/.pikmin-assets or .pikmin2-assets
    bool executablePresent = false; // the game binary Play would start
    bool needsRepair = false;       // packageMissing(): the home screen offers the repair
};

enum class LaunchDecision { Launch, NotInstalled, NeedsRepair, MissingExecutable };

inline LaunchDecision decideLaunch(const LaunchFacts& facts)
{
    if (!facts.assetsInstalled) return LaunchDecision::NotInstalled;
    if (facts.needsRepair) return LaunchDecision::NeedsRepair;
    if (!facts.executablePresent) return LaunchDecision::MissingExecutable;
    return LaunchDecision::Launch;
}

// Short notice for the home screen when the game could not start directly.
inline std::string launchFallbackNotice(LaunchTarget target, LaunchDecision decision)
{
    const std::string game = launchTargetTitle(target);
    switch (decision) {
    case LaunchDecision::NotInstalled: return game + " is not installed yet. Install it here to play.";
    case LaunchDecision::NeedsRepair: return "Finish repairing the install, then " + game + " can start directly.";
    case LaunchDecision::MissingExecutable: return game + " could not start: its game file is missing. Update to restore it.";
    case LaunchDecision::Launch: break;
    }
    return std::string();
}

// Android shortcut Intent (#336). Keep these in sync with GameLaunch.java.
//   extra  Intent extra "org.opennectar.LAUNCH" = "pikmin1" or "pikmin2"
//   data   Intent data URI "opennectar://launch/pikmin1" (or pikmin2)
// A normal open sets neither (None). An explicit name that isn't a game is
// Invalid, same as --launch=pikmin3. A URI with some other scheme is ignored.
inline constexpr const char* kAndroidLaunchExtra = "org.opennectar.LAUNCH";
inline constexpr const char* kAndroidLaunchUriPrefix = "opennectar://launch/";

inline std::string androidLaunchUri(LaunchTarget target)
{
    const char* id = target == LaunchTarget::Pikmin2 ? "pikmin2" : target == LaunchTarget::Pikmin1 ? "pikmin1" : "";
    return *id ? std::string(kAndroidLaunchUriPrefix) + id : std::string();
}

// The game named by an Android data URI, or None when it isn't one of ours.
// Invalid when it is our URI but the name isn't a game (or is missing).
inline LaunchTarget launchTargetFromAndroidUri(const std::string& data)
{
    const std::string prefix = kAndroidLaunchUriPrefix;
    if (data.size() < prefix.size() || data.compare(0, prefix.size(), prefix) != 0) return LaunchTarget::None;
    std::string name = data.substr(prefix.size());
    const std::string::size_type cut = name.find_first_of("?#");
    if (cut != std::string::npos) name.resize(cut);
    if (!name.empty() && name.back() == '/') name.pop_back();
    if (name.empty() || name.find('/') != std::string::npos) return LaunchTarget::Invalid;
    return parseLaunchTarget(name);
}

// `extra` wins when it is set, including when it names an unknown game.
// Both empty is a normal open.
inline LaunchTarget parseAndroidLaunchChoice(const std::string& extra, const std::string& data)
{
    if (!extra.empty()) return parseLaunchTarget(extra);
    if (data.empty()) return LaunchTarget::None;
    return launchTargetFromAndroidUri(data);
}

// Home-screen notice when a shortcut named something that isn't a game.
inline std::string androidLaunchInvalidNotice()
{
    return "That shortcut does not name Pikmin or Pikmin 2.";
}

// Android's view of the same three facts decideLaunch already uses.
// `contentReady` is what the home screen calls installed. `incomplete` is
// that install missing files the game opens (the repair case). `libraryPresent`
// is the game's .so in the APK (the binary).
inline LaunchFacts androidLaunchFacts(bool contentReady, bool incomplete, bool libraryPresent)
{
    LaunchFacts facts;
    facts.assetsInstalled = contentReady;
    facts.needsRepair = contentReady && incomplete;
    facts.executablePresent = libraryPresent;
    return facts;
}

} // namespace pikmin::launcher
