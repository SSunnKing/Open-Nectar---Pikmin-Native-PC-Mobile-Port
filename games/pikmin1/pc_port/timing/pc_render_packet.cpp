#include "pc_render_packet.h"

#include <algorithm>
#include <cstring>
#include <memory>

struct PcRenderPacketStore::Impl {
	uint64_t currentSerial = 0;
	double currentAlpha = 0.0;

	std::unique_ptr<PcRenderFrame> currentFrame;
	std::unique_ptr<PcRenderFrame> previousFrame;

	bool presenting = false;
	size_t presentationIndex = 0;

	PcRenderFrame presentationFrame;

	void swapFrames() {
		previousFrame = std::move(currentFrame);
		currentFrame = std::make_unique<PcRenderFrame>();
	}
};

PcRenderPacketStore::PcRenderPacketStore()
	: mImpl(new Impl())
{
}

PcRenderPacketStore::~PcRenderPacketStore()
{
	delete mImpl;
}

void PcRenderPacketStore::beginAuthoritativeTick(uint64_t serial)
{
	if (mImpl->currentFrame && mImpl->currentFrame->serial != serial) {
		mImpl->swapFrames();
	}
	if (!mImpl->currentFrame) {
		mImpl->currentFrame = std::make_unique<PcRenderFrame>();
	}
	mImpl->currentFrame->serial = serial;
	mImpl->currentSerial = serial;
	mImpl->currentAlpha = 0.0;
}

void PcRenderPacketStore::captureDisplayList(const void* list, uint32_t nbytes)
{
	if (!mImpl->currentFrame || !list || nbytes == 0) {
		return;
	}
	auto packet = std::make_unique<PcRenderPacket>();
	packet->serial = mImpl->currentSerial;
	packet->displayListPayload.resize(nbytes);
	memcpy(packet->displayListPayload.data(), list, nbytes);
	packet->valid = true;
	mImpl->currentFrame->packets.push_back(std::move(packet));
}

void PcRenderPacketStore::endAuthoritativeTick()
{
	// Frame complete, ready for presentation
}

bool PcRenderPacketStore::preparePresentation(uint64_t targetSerial, double alpha)
{
	mImpl->presentationFrame = PcRenderFrame();
	mImpl->presentationFrame.serial = targetSerial;
	mImpl->presentationFrame.alpha = alpha;

	PcRenderFrame* sourceFrame = nullptr;
	if (mImpl->currentFrame && mImpl->currentFrame->serial == targetSerial) {
		sourceFrame = mImpl->currentFrame.get();
	} else if (mImpl->previousFrame && mImpl->previousFrame->serial == targetSerial) {
		sourceFrame = mImpl->previousFrame.get();
	}

	if (!sourceFrame) {
		return false;
	}

	for (const auto& packet : sourceFrame->packets) {
		mImpl->presentationFrame.packets.push_back(std::make_unique<PcRenderPacket>(*packet));
	}

	mImpl->presenting = true;
	mImpl->presentationIndex = 0;
	return true;
}

const PcRenderFrame* PcRenderPacketStore::getPresentationFrame() const
{
	return mImpl->presenting ? &mImpl->presentationFrame : nullptr;
}

bool PcRenderPacketStore::hasPacket() const
{
	return mImpl->presenting && mImpl->presentationIndex < mImpl->presentationFrame.packets.size();
}

void PcRenderPacketStore::replayNextPacket()
{
	if (mImpl->presentationIndex < mImpl->presentationFrame.packets.size()) {
		mImpl->presentationIndex++;
	}
}

void PcRenderPacketStore::clearPresentation()
{
	mImpl->presenting = false;
	mImpl->presentationIndex = 0;
	mImpl->presentationFrame.packets.clear();
}

void PcRenderPacketStore::synchronize()
{
	if (mImpl->currentFrame) {
		mImpl->swapFrames();
	}
}

void PcRenderPacketStore::clear()
{
	mImpl->currentFrame.reset();
	mImpl->previousFrame.reset();
	mImpl->presentationFrame.packets.clear();
	mImpl->presenting = false;
}

size_t PcRenderPacketStore::getPacketCount() const
{
	if (!mImpl->currentFrame) {
		return 0;
	}
	return mImpl->currentFrame->packets.size();
}
