#pragma once

class Camera;

void pc_camera_set_enabled(bool enabled);
bool pc_camera_is_enabled();
void pc_camera_begin_authoritative_tick();
void pc_camera_prepare_presentation(double alpha);
void pc_camera_on_set(Camera* camera);
void pc_camera_restore_frame();
void pc_camera_synchronize();
void pc_camera_clear();
