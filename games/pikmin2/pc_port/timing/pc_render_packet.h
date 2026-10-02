#pragma once

#include <cstdint>
#include <vector>
#include <memory>

struct PcRenderPacket {
	uint64_t serial;
	std::vector<uint8_t> displayListPayload;
	bool valid = false;
};

struct PcRenderFrame {
	uint64_t serial;
	double alpha;
	std::vector<std::unique_ptr<PcRenderPacket>> packets;
};

class PcRenderPacketStore {
public:
	PcRenderPacketStore();
	~PcRenderPacketStore();

	void beginAuthoritativeTick(uint64_t serial);
	void captureDisplayList(const void* list, uint32_t nbytes);
	void endAuthoritativeTick();

	bool preparePresentation(uint64_t targetSerial, double alpha);
	const PcRenderFrame* getPresentationFrame() const;
	bool hasPacket() const;
	void replayNextPacket();
	void clearPresentation();

	void synchronize();
	void clear();
	size_t getPacketCount() const;

private:
	struct Impl;
	Impl* mImpl;
};
