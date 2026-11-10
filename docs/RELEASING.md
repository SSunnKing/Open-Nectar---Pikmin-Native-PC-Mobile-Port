# Releasing

Releases are built by the `Release` workflow (`.github/workflows/release.yml`).
It runs the same scripts that were run by hand before, so the files are made the
same way, and it puts them on a **draft** GitHub release. Nothing is published
until you press Publish.

## Making a release

1. Edit `versions.json` (and write `RELEASE_NOTES_<version>.md`, which becomes the
   release text). Merge it to `main`.
2. Push a tag that is exactly the `release` value in `versions.json`:

   ```
   git tag 1.3
   git push origin 1.3
   ```

   If the tag and `versions.json` disagree, the workflow stops at the first step.
3. Wait for the four build jobs. The last job creates a draft release called
   `Open Nectar 1.3` with every file attached.
4. Open the draft, check the files, set the title and text the way you want, then
   publish it. The launcher reads `manifest.json` from the latest published
   release, so publishing is the moment users start to see the update.

Re-pushing the tag (or re-running the workflow) replaces the files of an existing
draft. It never touches a release that is already published.

## What it builds

| Job | Runs | Script | Files |
| --- | --- | --- | --- |
| Linux | Ubuntu runner, podman container (Ubuntu 20.04) | `scripts/release-linux.sh` | `nectar-linux.tar.gz`, per-part archives, `Open_Nectar-x86_64.AppImage` |
| Windows | Ubuntu runner, MinGW cross build | `scripts/release-windows.sh` | `nectar-windows.zip`, per-part archives |
| macOS | `macos-latest` (Apple Silicon only) | `scripts/release-macos.sh` | `nectar-macos.tar.gz`, per-part archives |
| Android | Ubuntu runner, Gradle | `games/pikmin1/packaging/android/package-apk.sh` | `open_nectar_<version>.apk` and its `.sha256` |

`manifest.json` is written by every system and has to be identical. The workflow
fails if it is not.

## Trying it without releasing

Actions > Release > Run workflow builds everything and keeps the files as
workflow artifacts. It never creates a release. Without the signing key below, the
APK is built with the debug key and is named `test-android-debug-key` so it cannot
be mistaken for a release APK.

## One-time setup: the Android signing key (repository admin)

Android only installs an update if it is signed with the same key as the installed
app, so the key must never change and must never be in the repository. The
workflow reads it from four repository secrets (Settings > Secrets and variables >
Actions > New repository secret):

| Secret | Value |
| --- | --- |
| `ANDROID_KEYSTORE_BASE64` | the release keystore, base64 encoded: `base64 -w0 ~/.config/opennectar/release.keystore` (on macOS: `base64 -i <file>`) |
| `ANDROID_KEYSTORE_PASSWORD` | the keystore password |
| `ANDROID_KEY_ALIAS` | the key alias (optional, defaults to `opennectar`) |
| `ANDROID_KEY_PASSWORD` | the key password (optional, defaults to the keystore password) |

These are the same values as in `~/.config/opennectar/keystore.properties`
(see `games/pikmin1/packaging/android/README-firma.txt`). Keep a copy of the
keystore outside the secrets as well, because secrets cannot be read back.

Until the secrets exist, a tag run builds everything else, skips the APK, and
prints a warning. The draft release then has no APK and it can be added by hand.

## Things to know

- **macOS** is Apple Silicon only and ad-hoc signed, like the script has always
  done. Users still need the `xattr -dr com.apple.quarantine .` step from the
  macOS README. Intel Macs would need another job.
- **Windows antivirus.** The cross-built `.exe` files come from the same script as
  before, but the build machine is different. After the first automated release,
  upload the Windows files to a scanner before publishing.
- **Online play** is not built into releases (`NECTAR_RELEASE_ONLINE` stays off).
  Pikmin 1 netplay is dead code: do not enable it.
- The tag filter accepts `1.3`, `1.3.1` and `1.3.1.1`.

## Status

Update this list when something changes.

- [x] Workflow merged (#297, 2026-11-10).
- [x] Linux: the build image now fails when apt fails and retries downloads
  (a mirror error had left the image without gcc-13).
- [x] Secrets `ANDROID_KEYSTORE_BASE64` and `ANDROID_KEYSTORE_PASSWORD` uploaded.
- [x] First manual run (Run workflow) with all four jobs green and the APK as
  `release-android`.
- [ ] First release from a tag.
