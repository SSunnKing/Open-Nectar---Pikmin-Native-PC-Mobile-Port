#include "launch_request.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace pikmin::launcher;

static void check(bool value, const char* message)
{
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct Parsed {
    bool consumed = false;
    LaunchTarget target = LaunchTarget::None;
    std::string value;
    int next = 0; // index after the arguments used
};

static Parsed parse(std::vector<std::string> args)
{
    args.insert(args.begin(), "nectar-launcher");
    std::vector<char*> argv;
    for (std::string& arg : args) argv.push_back(arg.data());
    Parsed parsed;
    int i = 1;
    parsed.consumed = consumeLaunchArgument(i, int(argv.size()), argv.data(), parsed.target, parsed.value);
    parsed.next = i + 1;
    return parsed;
}

int main()
{
    // Both spellings, both games.
    Parsed p = parse({ "--launch=pikmin1" });
    check(p.consumed && p.target == LaunchTarget::Pikmin1 && p.next == 2, "--launch=pikmin1");
    p = parse({ "--launch=pikmin2" });
    check(p.consumed && p.target == LaunchTarget::Pikmin2 && p.next == 2, "--launch=pikmin2");
    p = parse({ "--launch", "pikmin2" });
    check(p.consumed && p.target == LaunchTarget::Pikmin2 && p.next == 3, "--launch pikmin2 uses two arguments");
    p = parse({ "--launch", "pikmin1", "--skip-verify" });
    check(p.consumed && p.target == LaunchTarget::Pikmin1 && p.next == 3, "--launch pikmin1 leaves the next option");

    // Unknown or missing names are errors, not silently ignored.
    p = parse({ "--launch=pikmin3" });
    check(p.consumed && p.target == LaunchTarget::Invalid && p.value == "pikmin3", "unknown game is invalid");
    p = parse({ "--launch=Pikmin2" });
    check(p.consumed && p.target == LaunchTarget::Invalid, "names are lowercase");
    p = parse({ "--launch=" });
    check(p.consumed && p.target == LaunchTarget::Invalid && p.value.empty(), "empty name is invalid");
    p = parse({ "--launch" });
    check(p.consumed && p.target == LaunchTarget::Invalid && p.next == 2, "missing name is invalid");
    p = parse({ "--launch", "--skip-verify" });
    check(p.consumed && p.target == LaunchTarget::Invalid && p.next == 2, "an option is not a game name");

    // Other options are left alone.
    p = parse({ "--skip-verify" });
    check(!p.consumed && p.target == LaunchTarget::None, "other options are not --launch");
    p = parse({ "--launcher=pikmin1" });
    check(!p.consumed, "--launcher= is not --launch=");

    // What the downloaded launcher passes on to the installed one.
    check(launchArgument(LaunchTarget::Pikmin1) == "--launch=pikmin1", "forward pikmin1");
    check(launchArgument(LaunchTarget::Pikmin2) == "--launch=pikmin2", "forward pikmin2");
    check(launchArgument(LaunchTarget::None).empty(), "nothing to forward");
    check(launchArgument(LaunchTarget::Invalid).empty(), "invalid is never forwarded");
    check(parse({ launchArgument(LaunchTarget::Pikmin2) }).target == LaunchTarget::Pikmin2, "forwarded argument parses back");

    // Launch only when the game is installed, whole and has its binary.
    LaunchFacts ready;
    ready.assetsInstalled = true;
    ready.executablePresent = true;
    check(decideLaunch(ready) == LaunchDecision::Launch, "installed game launches");
    LaunchFacts missing;
    check(decideLaunch(missing) == LaunchDecision::NotInstalled, "no assets: home screen");
    missing.executablePresent = true;
    check(decideLaunch(missing) == LaunchDecision::NotInstalled, "binary without assets: home screen");
    LaunchFacts repair = ready;
    repair.needsRepair = true;
    check(decideLaunch(repair) == LaunchDecision::NeedsRepair, "half-updated install: home screen");
    LaunchFacts noBinary = ready;
    noBinary.executablePresent = false;
    check(decideLaunch(noBinary) == LaunchDecision::MissingExecutable, "missing binary: home screen");

    // Every fallback explains itself briefly, names the game and has no ellipsis.
    for (LaunchTarget target : { LaunchTarget::Pikmin1, LaunchTarget::Pikmin2 }) {
        for (LaunchDecision decision :
             { LaunchDecision::NotInstalled, LaunchDecision::NeedsRepair, LaunchDecision::MissingExecutable }) {
            const std::string notice = launchFallbackNotice(target, decision);
            check(!notice.empty(), "fallback has a notice");
            check(notice.size() <= 80, "notice is short");
            check(notice.find("...") == std::string::npos && notice.find("\xE2\x80\xA6") == std::string::npos,
                  "notice has no ellipsis");
            check(notice.find(launchTargetTitle(target)) != std::string::npos, "notice names the game");
        }
        check(launchFallbackNotice(target, LaunchDecision::Launch).empty(), "launching needs no notice");
    }
    check(launchFallbackNotice(LaunchTarget::Pikmin2, LaunchDecision::NotInstalled).find("Pikmin 2") == 0,
          "Pikmin 2 notice says Pikmin 2");

    // Android shortcut Intent: extra, or a data URI, into the same launch target.
    check(std::string(kAndroidLaunchExtra) == "org.opennectar.LAUNCH", "extra name");
    check(parseAndroidLaunchChoice("pikmin1", "") == LaunchTarget::Pikmin1, "extra pikmin1");
    check(parseAndroidLaunchChoice("pikmin2", "") == LaunchTarget::Pikmin2, "extra pikmin2");
    check(parseAndroidLaunchChoice("", androidLaunchUri(LaunchTarget::Pikmin1)) == LaunchTarget::Pikmin1, "uri pikmin1");
    check(parseAndroidLaunchChoice("", androidLaunchUri(LaunchTarget::Pikmin2)) == LaunchTarget::Pikmin2, "uri pikmin2");
    check(parseAndroidLaunchChoice("pikmin2", androidLaunchUri(LaunchTarget::Pikmin1)) == LaunchTarget::Pikmin2,
          "extra wins when both are set");
    check(parseAndroidLaunchChoice("", androidLaunchUri(LaunchTarget::Pikmin2) + "?from=shortcut") == LaunchTarget::Pikmin2,
          "uri query is not part of the name");
    check(parseAndroidLaunchChoice("", androidLaunchUri(LaunchTarget::Pikmin1) + "#cover") == LaunchTarget::Pikmin1,
          "uri fragment is not part of the name");
    check(parseAndroidLaunchChoice("", androidLaunchUri(LaunchTarget::Pikmin1) + "/") == LaunchTarget::Pikmin1,
          "one trailing slash is fine");
    check(parseAndroidLaunchChoice("", "") == LaunchTarget::None, "no extra and no uri is a normal open");
    check(parseAndroidLaunchChoice("", "https://example.com/pikmin1") == LaunchTarget::None, "other schemes are ignored");
    check(parseAndroidLaunchChoice("", "opennectar://launch") == LaunchTarget::None, "uri without a game path is ignored");
    check(parseAndroidLaunchChoice("pikmin3", androidLaunchUri(LaunchTarget::Pikmin1)) == LaunchTarget::Invalid,
          "a bad extra is not rescued by the uri");
    check(parseAndroidLaunchChoice("Pikmin1", "") == LaunchTarget::Invalid, "extra names are lowercase");
    check(parseAndroidLaunchChoice("", std::string(kAndroidLaunchUriPrefix) + "pikmin3") == LaunchTarget::Invalid,
          "unknown uri name is invalid");
    check(parseAndroidLaunchChoice("", std::string(kAndroidLaunchUriPrefix)) == LaunchTarget::Invalid,
          "our uri with no name is invalid");
    check(parseAndroidLaunchChoice("", std::string(kAndroidLaunchUriPrefix) + "pikmin1/extra") == LaunchTarget::Invalid,
          "extra uri path is invalid");
    check(androidLaunchUri(LaunchTarget::None).empty() && androidLaunchUri(LaunchTarget::Invalid).empty(),
          "no uri for a missing game");
    const std::string invalidNotice = androidLaunchInvalidNotice();
    check(!invalidNotice.empty() && invalidNotice.size() <= 80, "invalid shortcut notice is short");
    check(invalidNotice.find("...") == std::string::npos && invalidNotice.find("\xE2\x80\xA6") == std::string::npos,
          "invalid shortcut notice has no ellipsis");
    check(invalidNotice.find("Pikmin") != std::string::npos && invalidNotice.find("Pikmin 2") != std::string::npos,
          "invalid shortcut notice names both games");

    // The three Android facts take the same decisions as a desktop --launch.
    check(decideLaunch(androidLaunchFacts(true, false, true)) == LaunchDecision::Launch, "android: ready launches");
    check(decideLaunch(androidLaunchFacts(false, false, true)) == LaunchDecision::NotInstalled, "android: not installed");
    check(decideLaunch(androidLaunchFacts(false, true, false)) == LaunchDecision::NotInstalled,
          "android: incomplete but not installed is not a repair");
    check(decideLaunch(androidLaunchFacts(true, true, true)) == LaunchDecision::NeedsRepair, "android: incomplete install repairs");
    check(decideLaunch(androidLaunchFacts(true, false, false)) == LaunchDecision::MissingExecutable,
          "android: missing .so is a missing binary");
    return 0;
}
