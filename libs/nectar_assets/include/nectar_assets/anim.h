#pragma once

// Writers for the animation formats the game loads (issue #66, under #62 and #59):
//  * the DCA and DCK entry payloads that live inside a bundle (bundle.h entry
//    types 2 and 3). The loaders (AnimDca::read, AnimDck::read in shapeBase.cpp)
//    are reached only through StdSystem::loadBundle; the loose .dca/.dck path is
//    a text parser for a format that does not exist, so loose files are not written;
//  * the AnimMgr table (pikis/animMgr.bin), which carries the per-animation
//    key lists. Key events (KEY_Action0...) live here, not in the payloads.
//
// Big-endian throughout. Neither payload has a header or magic: the stream starts
// at the joint count, and the loader ignores the bundle entry's size field, so a
// payload must be self-delimiting (the writers below emit exactly what the
// reader consumes and nothing else).
//
// The validation in each writer is the loader's silent failure modes (reads past
// a data block, a parent that comes after its child, a key list the animator
// would clamp or stop on), so a bad table fails here with a message.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nectar {

// ---------------------------------------------------------------- DCA / DCK payloads

/// One scale, rotation or translation component of a joint (AnimParam).
///  * DCK: `entryNum` 0 uses the default (scale 1, rotation 0, translation 1!),
///    1 is one static value at `dataOffset`, more are keys of 3 floats
///    (time, value, tangent; `flags` 0) or 4 floats (`flags` nonzero) from
///    `dataOffset`.
///  * DCA: `entryNum` is the baked sample count, at least 1, and the samples are
///    one float each from `dataOffset`. A DCA channel has no flags.
/// `dataOffset` counts floats inside the matching data block.
struct AnimChannel {
	std::int32_t entryNum   = 0;
	std::int32_t dataOffset = 0;
	std::int32_t flags      = 0;
};

struct AnimJointTrack {
	std::int32_t group  = 0;  ///< the game stores it and never reads it back; the stand-ins write the joint index
	std::int32_t parent = -1; ///< -1 is the root; otherwise an earlier joint
	AnimChannel scale[3], rotate[3], translate[3]; ///< x, y, z
};

struct AnimPayload {
	std::int32_t frameCount = 0; ///< totalFrameCount; independent of the data blocks
	std::vector<float> scaleData, rotateData, translateData;
	std::vector<AnimJointTrack> joints;
};

/// DCK entry payload (bundle entry type 3). false, with `error`, if a channel
/// reads outside its data block, a parent is not an earlier joint, or the frame
/// count or joint list is empty.
bool writeDckPayload(const AnimPayload& in, std::vector<std::uint8_t>& out, std::string& error);

/// DCA entry payload (bundle entry type 2). Same checks; every channel needs
/// `entryNum >= 1` and `flags == 0`.
bool writeDcaPayload(const AnimPayload& in, std::vector<std::uint8_t>& out, std::string& error);

/// The simplest valid animation for a skeleton: every component static, scale 1,
/// rotation 0, translation 0 (one entry each, because entryNum 0 would default
/// the translation to 1.0). `parents` is the model's per-joint parent list.
AnimPayload makeStaticAnim(const std::vector<int>& parents, int frameCount);

// ---------------------------------------------------------------- AnimMgr table

/// A record in an info's info-key or event-key list (AnimInfo::doread).
///  * info key: `frame` indexes the info's anim-key list (not a frame number),
///    `keyType` 0 loop start, 1 loop end, 2-5 KEY_Action0-3.
///  * event key: `frame` is a frame number, `eventType` 0 notify (sound), 1
///    action (effect), 2 none.
struct AnimKeyRecord {
	std::int32_t frame     = 0;
	std::int16_t eventType = 0;
	std::uint8_t cmdId     = 0;
	std::uint8_t keyType   = 0;
};

struct AnimInfoSpec {
	std::string name;               ///< bundle entry name is `<baseDir>/<name>`, exact and case-sensitive
	std::optional<int> flags;       ///< 'p00'; absent keeps the code default 2 (AnimInfo::Parms)
	std::optional<float> speed;     ///< 'spd'; absent keeps the code default 30.0
	std::vector<std::int32_t> animKeys;  ///< frame numbers, ascending; first is the start, last the end of a one-shot
	std::vector<AnimKeyRecord> infoKeys;  ///< ascending by the frame they point at; at most 16
	std::vector<AnimKeyRecord> eventKeys;
};

struct AnimMgrTable {
	std::string baseDir;        ///< 'a01', no trailing slash; prefixes every bundle entry name
	std::optional<int> flags;   ///< 'a00', unused by the game
	std::vector<AnimInfoSpec> infos;
};

/// `animMgr.bin` (version 1). Infos are written in index order. false, with
/// `error`, for an empty base directory or name, fewer than two anim keys, anim
/// keys that go down, more than 16 info keys, an info key that points past the
/// anim-key list or backwards, an info or event key type the game does not
/// handle, or an event type that the loader would silently clamp.
bool writeAnimMgrTable(const AnimMgrTable& in, std::vector<std::uint8_t>& out, std::string& error);

} // namespace nectar
