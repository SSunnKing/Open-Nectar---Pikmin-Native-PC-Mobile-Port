#include "nectar_assets/anim.h"

#include "nectar_assets/parms.h"

#include "be_writer.h"

#include <cmath>

namespace nectar {

namespace {

// DCK keys are 3 floats (flags 0) or 4 floats, see extract() in shapeBase.cpp.
std::int64_t dckKeyStride(const AnimChannel& c) { return c.flags == 0 ? 3 : 4; }

bool checkChannel(const AnimChannel& c, const std::vector<float>& block, bool dck, const char* what, int joint, std::string& error)
{
	const std::int64_t size = std::int64_t(block.size());
	auto fail = [&](const std::string& why) {
		error = "joint " + std::to_string(joint) + " " + what + ": " + why;
		return false;
	};
	if (c.entryNum < 0 || c.dataOffset < 0) return fail("negative entry count or data offset");
	if (dck) {
		if (c.entryNum == 0) return true; // the loader uses the default and reads no data
		if (c.entryNum == 1) {
			if (c.dataOffset >= size) return fail("static value is past the end of its data block");
			return true;
		}
		if (std::int64_t(c.dataOffset) + std::int64_t(c.entryNum) * dckKeyStride(c) > size) return fail("keys run past the end of their data block");
		return true;
	}
	if (c.flags != 0) return fail("a DCA channel has no flags");
	if (c.entryNum < 1) return fail("a DCA channel needs at least one sample");
	if (std::int64_t(c.dataOffset) + c.entryNum > size) return fail("samples run past the end of their data block");
	return true;
}

bool writePayload(const AnimPayload& in, bool dck, std::vector<std::uint8_t>& out, std::string& error)
{
	if (in.frameCount < 1) {
		error = "animation needs at least one frame";
		return false;
	}
	if (in.joints.empty()) {
		error = "animation has no joints";
		return false;
	}
	if (in.scaleData.size() > 0x7FFFFFFF || in.rotateData.size() > 0x7FFFFFFF || in.translateData.size() > 0x7FFFFFFF || in.joints.size() > 0x7FFFFFFF) {
		error = "animation is too big for int32 counts";
		return false;
	}
	for (std::size_t j = 0; j < in.joints.size(); j++) {
		const AnimJointTrack& t = in.joints[j];
		// makeAnimSRT concatenates onto the parent's matrix in joint order.
		if (t.parent < -1 || t.parent >= int(j)) {
			error = "joint " + std::to_string(j) + " has parent " + std::to_string(t.parent) + " (-1 or an earlier joint)";
			return false;
		}
		for (int i = 0; i < 3; i++) {
			if (!checkChannel(t.scale[i], in.scaleData, dck, "scale", int(j), error) || !checkChannel(t.rotate[i], in.rotateData, dck, "rotation", int(j), error)
			    || !checkChannel(t.translate[i], in.translateData, dck, "translation", int(j), error)) {
				return false;
			}
		}
	}

	BeWriter w;
	w.s32(std::int32_t(in.joints.size()));
	w.s32(in.frameCount);
	for (const std::vector<float>* block : { &in.scaleData, &in.rotateData, &in.translateData }) { // DataChunk::read: count, then floats
		w.s32(std::int32_t(block->size()));
		for (float f : *block) w.f32(f);
	}
	for (const AnimJointTrack& t : in.joints) {
		w.s32(t.group);
		w.s32(t.parent);
		for (const AnimChannel* set : { t.scale, t.rotate, t.translate }) {
			for (int i = 0; i < 3; i++) {
				w.s32(set[i].entryNum);
				w.s32(set[i].dataOffset);
				if (dck) w.s32(set[i].flags);
			}
		}
	}
	out = std::move(w.b);
	return true;
}

bool checkKeyType(const AnimKeyRecord& k, const char* what, const std::string& name, std::string& error)
{
	if (k.keyType > 5) { // checkConstantKey acts on types 0-5 only
		error = "animation '" + name + "': " + what + " key type " + std::to_string(k.keyType) + " is not 0-5";
		return false;
	}
	return true;
}

void putKey(BeWriter& w, const AnimKeyRecord& k)
{
	// Reader order (AnimInfo::doread): frame, event type, command id, key type.
	w.s32(k.frame);
	w.u16(std::uint16_t(k.eventType));
	w.u8(k.cmdId);
	w.u8(k.keyType);
}

} // namespace

bool writeDckPayload(const AnimPayload& in, std::vector<std::uint8_t>& out, std::string& error) { return writePayload(in, true, out, error); }

bool writeDcaPayload(const AnimPayload& in, std::vector<std::uint8_t>& out, std::string& error) { return writePayload(in, false, out, error); }

AnimPayload makeStaticAnim(const std::vector<int>& parents, int frameCount)
{
	AnimPayload a;
	a.frameCount     = frameCount;
	a.scaleData      = { 1.0f };
	a.rotateData     = { 0.0f };
	a.translateData  = { 0.0f };
	for (std::size_t j = 0; j < parents.size(); j++) {
		AnimJointTrack t;
		t.group  = std::int32_t(j);
		t.parent = parents[j];
		for (int i = 0; i < 3; i++) {
			t.scale[i]     = { 1, 0, 0 };
			t.rotate[i]    = { 1, 0, 0 };
			t.translate[i] = { 1, 0, 0 };
		}
		a.joints.push_back(t);
	}
	return a;
}

bool writeAnimMgrTable(const AnimMgrTable& in, std::vector<std::uint8_t>& out, std::string& error)
{
	if (in.baseDir.empty()) {
		error = "AnimMgr base directory is empty";
		return false;
	}
	if (in.infos.size() > 0x7FFFFFFF) {
		error = "AnimMgr has too many infos";
		return false;
	}
	for (std::size_t i = 0; i < in.infos.size(); i++) {
		const AnimInfoSpec& a = in.infos[i];
		const std::string label = "info " + std::to_string(i) + " '" + a.name + "'";
		if (a.name.empty()) {
			error = label + ": empty name";
			return false;
		}
		if (a.animKeys.size() < 2) {
			error = label + ": needs a start and an end anim key";
			return false;
		}
		for (std::size_t k = 0; k < a.animKeys.size(); k++) {
			if (a.animKeys[k] < 0 || (k > 0 && a.animKeys[k] < a.animKeys[k - 1])) {
				error = label + ": anim keys must be non-negative and ascending";
				return false;
			}
		}
		if (a.infoKeys.size() > 16) { // checkConstantKeys errors above 16
			error = label + ": " + std::to_string(a.infoKeys.size()) + " info keys (the game allows 16)";
			return false;
		}
		std::int32_t previous = -1;
		for (const AnimKeyRecord& k : a.infoKeys) {
			if (k.frame < 0 || k.frame >= std::int32_t(a.animKeys.size())) {
				error = label + ": info key points at anim key " + std::to_string(k.frame) + " of " + std::to_string(a.animKeys.size());
				return false;
			}
			// checkConstantKeys stops at the first key that has not been reached yet.
			if (a.animKeys[std::size_t(k.frame)] < previous) {
				error = label + ": info keys must be ascending by the frame they point at";
				return false;
			}
			previous = a.animKeys[std::size_t(k.frame)];
			if (!checkKeyType(k, "info", label, error)) return false;
		}
		for (const AnimKeyRecord& k : a.eventKeys) {
			if (k.frame < 0) {
				error = label + ": negative event key frame";
				return false;
			}
			if (k.eventType < 0 || k.eventType > 2) { // the loader rewrites anything at or above ANIMEVENT_COUNT to Notify
				error = label + ": event type " + std::to_string(k.eventType) + " is not 0-2";
				return false;
			}
		}
		if (a.speed && !std::isfinite(*a.speed)) {
			error = label + ": speed is not finite";
			return false;
		}
	}

	auto params = [&](std::vector<ParmEntry> list, std::vector<std::uint8_t>& bytes) {
		ParmFile f;
		f.lists.push_back(std::move(list));
		return writeParmFile(f, bytes, error);
	};

	BeWriter w;
	w.s32(1); // version: 1 or later has info keys (AnimInfo::doread)
	w.s32(std::int32_t(in.infos.size()));
	std::vector<std::uint8_t> block;
	{
		std::vector<ParmEntry> list;
		if (in.flags) list.push_back(parmInt("a00", *in.flags));
		list.push_back(parmString("a01", in.baseDir));
		if (!params(std::move(list), block)) return false;
		w.bytes(block);
	}
	for (const AnimInfoSpec& a : in.infos) {
		w.str(a.name);
		std::vector<ParmEntry> list; // an empty block (just the terminator) is valid
		if (a.flags) list.push_back(parmInt("p00", *a.flags));
		if (a.speed) list.push_back(parmFloat("spd", *a.speed));
		if (!params(std::move(list), block)) return false;
		w.bytes(block);

		w.s32(std::int32_t(a.animKeys.size()));
		for (std::int32_t f : a.animKeys) w.s32(f);
		w.s32(std::int32_t(a.infoKeys.size()));
		for (const AnimKeyRecord& k : a.infoKeys) putKey(w, k);
		w.s32(std::int32_t(a.eventKeys.size()));
		for (const AnimKeyRecord& k : a.eventKeys) putKey(w, k);
	}
	out = std::move(w.b);
	return true;
}

} // namespace nectar
