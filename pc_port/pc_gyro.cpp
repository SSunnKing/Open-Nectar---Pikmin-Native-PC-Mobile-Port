#include "pc_gyro.h"

#include "pc_window.h"
#include "settings/pc_settings.h"

#include <cmath>

namespace {

constexpr float kCalibrationSeconds = 2.0f;
constexpr float kDeadZone           = 0.015f; // rad/s tras restar el bias
constexpr float kCursorPerRadian    = 400.0f; // unidades de delta del ratón por radián
constexpr float kCameraDragPerRad   = 1.0f / 3.2f; // PcamCamera gira 3,2 rad por unidad

SDL_GameController* sPad = nullptr;
bool sPadHasGyro         = false;
SDL_Sensor* sDeviceGyro  = nullptr;
Uint64 sLastCounter      = 0;

float sCalibLeft   = 0.0f;
double sCalibSum[3] = { 0.0, 0.0, 0.0 };
int sCalibSamples  = 0;

void bindPad(SDL_GameController* pad)
{
	if (pad == sPad) {
		return;
	}
	sPad        = pad;
	sPadHasGyro = false;
	if (pad && SDL_GameControllerHasSensor(pad, SDL_SENSOR_GYRO)) {
		sPadHasGyro = SDL_GameControllerSetSensorEnabled(pad, SDL_SENSOR_GYRO, SDL_TRUE) == 0;
		if (sPadHasGyro) {
			printf("[PC Port] Gyro: using controller sensor (%s)\n", SDL_GameControllerName(pad));
		}
	}
}

/// Velocidad angular en ejes de pantalla: [0] alrededor de "derecha" (cabeceo,
/// + = apuntar arriba), [1] alrededor de "arriba" (guiñada, + = girar a la
/// izquierda), [2] alrededor de "hacia el jugador". rad/s, sin bias.
bool readRaw(float out[3])
{
	if (sPadHasGyro) {
		// SDL ya da los mandos en ese marco: x cabeceo, y guiñada, z alabeo.
		return SDL_GameControllerGetSensorData(sPad, SDL_SENSOR_GYRO, out, 3) == 0;
	}
	if (sDeviceGyro) {
		float d[3];
		if (SDL_SensorGetData(sDeviceGyro, d, 3) != 0) {
			return false;
		}
		// El sensor del móvil va en ejes del dispositivo en vertical
		// (x derecha, y arriba, z fuera de la pantalla). Se rotan según cómo
		// esté girada la pantalla.
		switch (SDL_GetDisplayOrientation(0)) {
		case SDL_ORIENTATION_LANDSCAPE: // borde derecho arriba
			out[0] = -d[1];
			out[1] = d[0];
			break;
		case SDL_ORIENTATION_LANDSCAPE_FLIPPED:
			out[0] = d[1];
			out[1] = -d[0];
			break;
		case SDL_ORIENTATION_PORTRAIT_FLIPPED:
			out[0] = -d[0];
			out[1] = -d[1];
			break;
		default:
			out[0] = d[0];
			out[1] = d[1];
			break;
		}
		out[2] = d[2];
		return true;
	}
	return false;
}

bool sRecenterCursor = false;
bool sRecenterView   = false;

} // namespace

extern "C" void pc_gyro_request_recenter(void)
{
	sRecenterCursor = true;
	sRecenterView   = true;
}

extern "C" int pc_gyro_take_recenter_cursor(void)
{
	const bool r    = sRecenterCursor;
	sRecenterCursor = false;
	return r;
}

extern "C" int pc_gyro_take_recenter_view(void)
{
	const bool r  = sRecenterView;
	sRecenterView = false;
	return r;
}

void pc_gyro_init(void)
{
	if (SDL_InitSubSystem(SDL_INIT_SENSOR) != 0) {
		printf("[PC Port] Gyro: sensor subsystem unavailable: %s\n", SDL_GetError());
		return;
	}
#if defined(__ANDROID__)
	for (int i = 0; i < SDL_NumSensors(); i++) {
		if (SDL_SensorGetDeviceType(i) == SDL_SENSOR_GYRO) {
			sDeviceGyro = SDL_SensorOpen(i);
			if (sDeviceGyro) {
				printf("[PC Port] Gyro: using device sensor (%s)\n", SDL_SensorGetDeviceName(i));
				break;
			}
		}
	}
#endif
}

bool pc_gyro_available(void)
{
	return sPadHasGyro || sDeviceGyro != nullptr;
}

void pc_gyro_calibrate_start(void)
{
	if (!pc_gyro_available()) {
		return;
	}
	sCalibLeft    = kCalibrationSeconds;
	sCalibSum[0]  = sCalibSum[1] = sCalibSum[2] = 0.0;
	sCalibSamples = 0;
}

float pc_gyro_calibration_seconds_left(void)
{
	return sCalibLeft;
}

void pc_gyro_update(SDL_GameController* pad, bool menuOpen, bool firstPerson)
{
	bindPad(pad);

	const Uint64 now = SDL_GetPerformanceCounter();
	float dt         = sLastCounter ? float(double(now - sLastCounter) / double(SDL_GetPerformanceFrequency())) : 0.0f;
	sLastCounter     = now;
	if (dt > 0.1f) {
		dt = 0.1f; // tras una carga o una pausa, no soltar un tirón
	}

	float w[3];
	if (!readRaw(w)) {
		return;
	}

	// La calibración funciona aunque el giroscopio esté desactivado: se lanza
	// desde el menú, antes de activarlo.
	if (sCalibLeft > 0.0f) {
		for (int i = 0; i < 3; i++) {
			sCalibSum[i] += w[i];
		}
		sCalibSamples++;
		sCalibLeft -= dt;
		if (sCalibLeft <= 0.0f) {
			sCalibLeft = 0.0f;
			if (sCalibSamples > 0) {
				const float bias[3] = { float(sCalibSum[0] / sCalibSamples), float(sCalibSum[1] / sCalibSamples),
					                    float(sCalibSum[2] / sCalibSamples) };
				pc_settings_set_gyro_bias(bias);
				printf("[PC Port] Gyro calibrated: bias %.4f %.4f %.4f rad/s\n", bias[0], bias[1], bias[2]);
			}
		}
		return;
	}

	if (!pc_settings_get_gyro_enabled() || menuOpen || dt <= 0.0f) {
		return;
	}

	float bias[3];
	pc_settings_get_gyro_bias(bias);
	float pitch = w[0] - bias[0];
	float yaw   = w[1] - bias[1];
	if (std::fabs(pitch) < kDeadZone) pitch = 0.0f;
	if (std::fabs(yaw) < kDeadZone) yaw = 0.0f;
	if (pitch == 0.0f && yaw == 0.0f) {
		return;
	}

	const int invert = pc_settings_get_gyro_invert();
	if (invert & 1) yaw = -yaw;
	if (invert & 2) pitch = -pitch;

	const float sens  = pc_settings_get_gyro_sensitivity();
	const float yawRad   = yaw * dt * sens;
	const float pitchRad = pitch * dt * sens;

	if (firstPerson) {
		// Mismo convenio que el ratón: arrastre negativo gira a la derecha.
		pc_window_add_camera_drag(yawRad * kCameraDragPerRad);
		pc_window_add_camera_pitch(pitchRad * kCameraDragPerRad);
	} else {
		// Delta de cursor en píxeles con Y hacia abajo, como el ratón.
		pc_window_add_cursor_delta(-yawRad * kCursorPerRadian, -pitchRad * kCursorPerRadian);
	}
}
