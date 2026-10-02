NECTAR — NATIVE PIKMIN PORT FOR ANDROID (arm64)

One APK. It contains both the USA Rev. 1 (GPIE01) and European (GPIP01)
builds of the game and picks the right one from your disc image. The game
runs natively — no emulator — with the original JAudio sound engine and
on-screen touch controls, or a Bluetooth/USB controller.

System requirements:
  - Android 10 or newer, 64-bit (arm64-v8a). Practically any phone or
    tablet from 2019 on.
  - OpenGL ES 3.0.
  - About 1 GB free in internal storage: the extracted assets take roughly
    650 MB.
  - Your legal, uncompressed copy of Pikmin (USA Rev. 1 or Europe), in
    ISO or GCM format. RVZ/WIA/GCZ are not supported on Android: convert
    them to ISO with Dolphin on a computer first.

The ROM and any Nintendo proprietary resources are not included. The disc
image is read once and neither copied nor modified.


-------------------------------------------------------------------
1. INSTALLATION
-------------------------------------------------------------------

The APK is not on Google Play. Copy open_nectar_<version>.apk to the
device (USB, cloud, browser download) and open it from the Files app.
Android asks once to allow installs from that app; allow it.

If you want to check the download, the .sha256 file next to the APK holds
its checksum.


-------------------------------------------------------------------
2. FIRST RUN
-------------------------------------------------------------------

1. Open Nectar. The first screen asks for your disc image.
2. Tap the button and pick the ISO/GCM with the system file picker. It can
   be anywhere the picker can reach: internal storage, SD card, USB drive.
3. The disc is verified and its assets extracted into the app's private
   storage. It takes well under a minute on a recent device.
4. The game starts by itself. From then on the app opens straight into the
   game.

The disc image can be deleted from the device afterwards.


-------------------------------------------------------------------
3. CONTROLS
-------------------------------------------------------------------

Touch: the left side of the screen is the stick; buttons for throwing,
whistling, disbanding, map and camera sit around it. Any button of the
GameCube pad that the game shows on screen appears in the touch layout
too. The "layout" button (top row) lets you move and resize every button;
"settings" opens the same options menu as F1 on desktop (graphics,
controls, mods).

Controller: any gamepad Android recognises works. The touch layout hides
itself while a controller is in use and comes back when you touch the
screen.


-------------------------------------------------------------------
4. UPDATING
-------------------------------------------------------------------

Install the new APK over the old one. Extracted assets, settings and save
data stay in place. Never uninstall to update: uninstalling deletes the
app's private storage, saves included.


-------------------------------------------------------------------
5. TROUBLESHOOTING
-------------------------------------------------------------------

"App not installed" / signature error
  An earlier build signed with a different key (a test build) is
  installed. Back up nothing — it cannot be — and uninstall it first.

Disc not recognised
  Only USA Rev. 1 and Europe are supported, and only ISO/GCM. Compressed
  formats must be converted on a computer.

Black screen or crash on start
  The device needs OpenGL ES 3.0 and 64-bit Android; 32-bit devices are
  not supported.

Low frame rate
  Open settings and lower the resolution scale or disable the optional
  post-processing effects.
