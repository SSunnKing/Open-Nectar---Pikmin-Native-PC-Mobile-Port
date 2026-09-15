#include "pc_photo_mode.h"

#include <SDL.h>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846f;

// World units. Pikmin's stages are hundreds of units across and the gameplay
// camera sits a few hundred out, so this crosses a clearing in a couple of
// seconds without being twitchy.
constexpr float kMoveSpeed  = 320.0f;
constexpr float kFastFactor = 4.0f;
constexpr float kSlowFactor = 0.25f;
constexpr float kLookSpeed  = 1.6f;  // radians per second
constexpr float kRollSpeed  = 1.2f;

// Stop just short of straight up and straight down. At exactly a quarter turn
// the azimuth stops meaning anything and the view snaps as it crosses.
constexpr float kPitchLimit = kPi * 0.5f - 0.02f;

bool sActive     = false;
bool sTogglePrev = false;

float sPosX = 0.0f, sPosY = 0.0f, sPosZ = 0.0f;
float sPitch = 0.0f, sYaw = 0.0f, sRoll = 0.0f;

bool keyDown(const Uint8* keys, int numKeys, SDL_Scancode sc)
{
	return keys != nullptr && (int)sc < numKeys && keys[sc] != 0;
}

} // namespace

extern "C" {

int pc_photo_mode_active(void) { return sActive ? 1 : 0; }

static bool sToggleRequested = false;
static float sTouchMoveX = 0.0f, sTouchMoveY = 0.0f;
static float sTouchLookDX = 0.0f, sTouchLookDY = 0.0f;
// Un arrastre de toda la altura de la pantalla gira ~150 grados.
static const float kTouchLookGain = 2.6f;

void pc_photo_mode_set_touch_move(float x, float y) { sTouchMoveX = x; sTouchMoveY = y; }
void pc_photo_mode_add_touch_look(float dx, float dy) { sTouchLookDX += dx; sTouchLookDY += dy; }

void pc_photo_mode_request_toggle(void) { sToggleRequested = true; }

int pc_photo_mode_poll_toggle(void)
{
	int numKeys        = 0;
	const Uint8* keys  = SDL_GetKeyboardState(&numKeys);
	const bool nowDown = keyDown(keys, numKeys, SDL_SCANCODE_F3);
	const bool edge    = nowDown && !sTogglePrev;
	sTogglePrev        = nowDown;
	const bool requested = sToggleRequested;
	sToggleRequested     = false;
	return (edge || requested) ? 1 : 0;
}

void pc_photo_mode_enter(float posX, float posY, float posZ,
                         float pitch, float yaw)
{
	sPosX  = posX;
	sPosY  = posY;
	sPosZ  = posZ;
	sPitch = pitch;
	sYaw   = yaw;
	sRoll  = 0.0f;
	sActive = true;
}

void pc_photo_mode_exit(void) { sActive = false; }

void pc_photo_mode_forward_vector(float pitch, float yaw,
                                  float* outX, float* outY, float* outZ)
{
	// From NCamera::makeCamera, rotation.x is (inclination - pi/2) and
	// rotation.y is the azimuth of the vector running from what the camera
	// watches to where the camera sits. NPolar3f::output expands that as
	//     (r sin(incl) sin(azi), r cos(incl), r sin(incl) cos(azi))
	// so with incl = pitch + pi/2 the substitutions sin(incl) = cos(pitch) and
	// cos(incl) = -sin(pitch) give the target-to-camera vector. The camera
	// looks the other way, hence the negation.
	const float cp = std::cos(pitch);
	const float sp = std::sin(pitch);
	const float sy = std::sin(yaw);
	const float cy = std::cos(yaw);
	if (outX) *outX = -cp * sy;
	if (outY) *outY = sp;
	if (outZ) *outZ = -cp * cy;
}

void pc_photo_mode_right_vector(float yaw, float* outX, float* outY, float* outZ)
{
	// forward (flattened) crossed with world up.
	if (outX) *outX = std::cos(yaw);
	if (outY) *outY = 0.0f;
	if (outZ) *outZ = -std::sin(yaw);
}

void pc_photo_mode_angles_from_forward(float fx, float fy, float fz,
                                       float* outPitch, float* outYaw)
{
	// forward = (-cos(pitch) sin(yaw), sin(pitch), -cos(pitch) cos(yaw)), so
	// the vertical component alone gives the pitch and the two horizontal ones
	// give the yaw once their signs are undone.
	const float len = std::sqrt(fx * fx + fy * fy + fz * fz);
	if (len < 1e-6f) {
		if (outPitch) *outPitch = 0.0f;
		if (outYaw) *outYaw = 0.0f;
		return;
	}
	float ny = fy / len;
	if (ny > 1.0f) ny = 1.0f;
	if (ny < -1.0f) ny = -1.0f;
	if (outPitch) *outPitch = std::asin(ny);
	if (outYaw) *outYaw = std::atan2(-fx, -fz);
}

void pc_photo_mode_update(float dt,
                          float* posX, float* posY, float* posZ,
                          float* pitch, float* yaw, float* roll)
{
	if (!sActive) return;
	if (dt < 0.0f) dt = 0.0f;
	// A long stall must not teleport the camera across the stage.
	if (dt > 0.1f) dt = 0.1f;

	int numKeys       = 0;
	const Uint8* keys = SDL_GetKeyboardState(&numKeys);

	// Look first: movement is relative to where the camera ends up facing.
	float lookX = 0.0f, lookY = 0.0f;
	if (keyDown(keys, numKeys, SDL_SCANCODE_LEFT)) lookX -= 1.0f;
	if (keyDown(keys, numKeys, SDL_SCANCODE_RIGHT)) lookX += 1.0f;
	if (keyDown(keys, numKeys, SDL_SCANCODE_UP)) lookY += 1.0f;
	if (keyDown(keys, numKeys, SDL_SCANCODE_DOWN)) lookY -= 1.0f;

	sYaw += lookX * kLookSpeed * dt;
	sPitch += lookY * kLookSpeed * dt;
	// Arrastre táctil: derecha gira a la derecha, arriba mira hacia arriba.
	sYaw += sTouchLookDX * kTouchLookGain;
	sPitch -= sTouchLookDY * kTouchLookGain;
	sTouchLookDX = sTouchLookDY = 0.0f;
	if (sPitch > kPitchLimit) sPitch = kPitchLimit;
	if (sPitch < -kPitchLimit) sPitch = -kPitchLimit;
	while (sYaw > kPi) sYaw -= 2.0f * kPi;
	while (sYaw < -kPi) sYaw += 2.0f * kPi;

	// Roll, for a tilted shot. R puts it back level.
	if (keyDown(keys, numKeys, SDL_SCANCODE_Q)) sRoll -= kRollSpeed * dt;
	if (keyDown(keys, numKeys, SDL_SCANCODE_E)) sRoll += kRollSpeed * dt;
	if (keyDown(keys, numKeys, SDL_SCANCODE_R)) sRoll = 0.0f;

	float fx, fy, fz, rx, ry, rz;
	pc_photo_mode_forward_vector(sPitch, sYaw, &fx, &fy, &fz);
	pc_photo_mode_right_vector(sYaw, &rx, &ry, &rz);

	float moveF = 0.0f, moveR = 0.0f, moveU = 0.0f;
	if (keyDown(keys, numKeys, SDL_SCANCODE_W)) moveF += 1.0f;
	if (keyDown(keys, numKeys, SDL_SCANCODE_S)) moveF -= 1.0f;
	if (keyDown(keys, numKeys, SDL_SCANCODE_D)) moveR += 1.0f;
	if (keyDown(keys, numKeys, SDL_SCANCODE_A)) moveR -= 1.0f;
	if (keyDown(keys, numKeys, SDL_SCANCODE_SPACE)) moveU += 1.0f;
	if (keyDown(keys, numKeys, SDL_SCANCODE_LCTRL)) moveU -= 1.0f;
	// Stick táctil: adelante/atrás y lateral, proporcional.
	moveF += sTouchMoveY;
	moveR += sTouchMoveX;

	float speed = kMoveSpeed;
	if (keyDown(keys, numKeys, SDL_SCANCODE_LSHIFT)) speed *= kFastFactor;
	if (keyDown(keys, numKeys, SDL_SCANCODE_LALT)) speed *= kSlowFactor;
	speed *= dt;

	sPosX += (fx * moveF + rx * moveR) * speed;
	sPosY += (fy * moveF + moveU) * speed;
	sPosZ += (fz * moveF + rz * moveR) * speed;

	if (posX) *posX = sPosX;
	if (posY) *posY = sPosY;
	if (posZ) *posZ = sPosZ;
	if (pitch) *pitch = sPitch;
	if (yaw) *yaw = sYaw;
	if (roll) *roll = sRoll;
}

} // extern "C"
