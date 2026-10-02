#include "pc_render_packet.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

static int failures = 0;

static void check(bool condition, const char* message) {
	if (!condition) {
		std::printf("FAIL: %s\n", message);
		failures++;
	}
}

int main() {
	PcRenderPacketStore store;

	// Test 1: Empty store
	check(store.getPacketCount() == 0, "empty store has zero packets");
	check(!store.hasPacket(), "empty store has no presentation packet");

	// Test 2: Capture display lists
	uint8_t testData1[] = {0x61, 0x00, 0x00, 0x01, 0x80, 0x40};
	uint8_t testData2[] = {0x10, 0x00, 0x05, 0x00, 0x00, 0x00, 0x10, 0x00};

	store.beginAuthoritativeTick(100);
	store.captureDisplayList(testData1, sizeof(testData1));
	store.captureDisplayList(testData2, sizeof(testData2));
	store.endAuthoritativeTick();

	check(store.getPacketCount() == 2, "captured two display lists");

	// Test 3: Prepare presentation
	check(store.preparePresentation(100, 0.5), "prepare presentation for serial 100");
	check(store.hasPacket(), "presentation has packet");

	const PcRenderFrame* frame = store.getPresentationFrame();
	check(frame != nullptr, "presentation frame exists");
	check(frame->serial == 100, "presentation frame serial matches");
	check(frame->alpha == 0.5, "presentation frame alpha matches");
	check(frame->packets.size() == 2, "presentation frame has two packets");

	// Test 4: Replay packets
	store.replayNextPacket();
	check(store.hasPacket(), "still has packet after first replay");
	store.replayNextPacket();
	check(!store.hasPacket(), "no more packets after replaying all");

	// Test 5: Multiple ticks
	store.beginAuthoritativeTick(101);
	store.captureDisplayList(testData1, sizeof(testData1));
	store.endAuthoritativeTick();

	check(store.preparePresentation(101, 0.0), "prepare presentation for serial 101");
	check(store.hasPacket(), "presentation has packet for serial 101");

	// Test 6: Clear
	store.clear();
	check(store.getPacketCount() == 0, "store cleared");
	check(!store.hasPacket(), "no presentation after clear");

	// Test 7: Synchronize
	store.beginAuthoritativeTick(200);
	store.captureDisplayList(testData1, sizeof(testData1));
	store.endAuthoritativeTick();
	store.synchronize();
	check(store.getPacketCount() == 0, "synchronized store has no current packets");

	// Test 8: Verify packet content preserved
	store.beginAuthoritativeTick(300);
	store.captureDisplayList(testData1, sizeof(testData1));
	store.endAuthoritativeTick();

	check(store.preparePresentation(300, 0.0), "prepare presentation for serial 300");
	const PcRenderFrame* frame300 = store.getPresentationFrame();
	check(frame300 != nullptr && frame300->packets.size() == 1, "frame 300 has one packet");
	if (frame300 && frame300->packets.size() == 1) {
		const auto& packet = frame300->packets[0];
		check(packet->valid, "packet is valid");
		check(packet->displayListPayload.size() == sizeof(testData1), "packet size matches");
		check(memcmp(packet->displayListPayload.data(), testData1, sizeof(testData1)) == 0, "packet content matches");
	}

	std::printf("PcRenderPacketStore: %s\n", failures ? "FAILED" : "all tests passed");
	return failures ? 1 : 0;
}
