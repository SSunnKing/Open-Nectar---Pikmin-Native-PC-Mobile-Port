"""Exercise the macOS helper with real CMake and synthetic game/SDL projects."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest


class MacOSBuildTest(unittest.TestCase):
    def test_existing_build_caches_select_requested_sdl_and_regions(self):
        script = pathlib.Path(__file__).resolve().parents[1] / "build-macos.sh"
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            (root / "scripts").mkdir()
            shutil.copy(script, root / "scripts/build-macos.sh")
            sdl = root / "build/sdl2-2.32.10/install"
            stale = root / "homebrew"
            for prefix, label in ((sdl, "native"), (stale, "compat")):
                package = prefix / "lib/cmake/SDL2"
                package.mkdir(parents=True)
                (package / "SDL2Config.cmake").write_text(f'set(SDL_FLAVOR "{label}")\n')
            (sdl / "lib/libSDL2.a").touch()
            project = '''cmake_minimum_required(VERSION 3.16)
project(SyntheticGame NONE)
find_package(SDL2 REQUIRED CONFIG)
set(PIKMIN_GAME_VERSION "default" CACHE STRING "region")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
file(WRITE "${CMAKE_BINARY_DIR}/bin/nectar" "${SDL_FLAVOR}:${PIKMIN_GAME_VERSION}")
add_custom_target(pikmin_pc)
add_custom_target(pikmin_launcher)
add_custom_target(pikmin2_pc)
'''
            for game in ("pikmin1", "pikmin2"):
                source = root / "games" / game
                source.mkdir(parents=True)
                (source / "CMakeLists.txt").write_text(project)
            for build, game in (("pikmin1", "pikmin1"), ("pikmin1-pal", "pikmin1"), ("pikmin2", "pikmin2")):
                subprocess.run([
                    "cmake", "-S", str(root / "games" / game), "-B", str(root / "build" / build),
                    f"-DSDL2_DIR={stale / 'lib/cmake/SDL2'}", "-DPIKMIN_GAME_VERSION=stale-region",
                ], check=True, capture_output=True)
            result = subprocess.run(["bash", str(root / "scripts/build-macos.sh")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            expected = {
                "pikmin1/bin/nectar": "native:VERSION_GPIE01_01",
                "pikmin1/bin/nectar-pal": "native:VERSION_GPIP01_00",
                "pikmin2/bin/nectar": "native:VERSION_GPVP01",
            }
            for path, contents in expected.items():
                with self.subTest(path=path):
                    self.assertEqual((root / "build" / path).read_text(), contents)


if __name__ == "__main__":
    unittest.main()
