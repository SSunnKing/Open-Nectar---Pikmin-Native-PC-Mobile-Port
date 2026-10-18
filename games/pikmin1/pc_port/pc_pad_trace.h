#ifndef PC_PAD_TRACE_H
#define PC_PAD_TRACE_H

/**
 * @file pc_pad_trace.h
 * @brief Diagnostic log of the pad axis path.
 *
 * Answers "the stick jumped to a corner" reports by recording the raw SDL axes
 * next to the pad values derived from them: if the raw axis is already at a
 * rail the driver did it, and if the axis is centred then something else wrote
 * the pad value. Silent unless a sample looks implausible, so an ordinary
 * session writes nothing. Set PIKMIN_PAD_TRACE=0 to disable it entirely, or =2
 * to log every change.
 *
 * Its own unit rather than part of the axis conversion: it owns a log file,
 * reads the environment and keeps state between polls, none of which belongs in
 * pc_pad_axis.cpp -- that file is a pure conversion so a test can link it.
 */

/**
 * @brief Directory pad_trace.log is written to, with a trailing separator.
 *        Defaults to the current working directory.
 */
void pc_pad_trace_set_dir(const char* dir);

/**
 * @brief Record one poll of the pad.
 *
 * @param instanceId SDL instance id of the controller, -1 when unknown.
 * @param device     Name SDL reports for it, may be null.
 * @param raw        SDL axes: left X, left Y, right X, right Y.
 * @param pad        Pad axes derived from them: main X/Y, C X/Y.
 * @param deadZone   Raw magnitude below which an axis is treated as centred.
 */
void pc_pad_trace_poll(int instanceId, const char* device, const int raw[4], const int pad[4], int deadZone);

/**
 * @brief One-shot note (attach/detach/hotplug) in the same log.
 */
void pc_pad_trace_note(const char* what, const char* detail);

#endif // PC_PAD_TRACE_H
