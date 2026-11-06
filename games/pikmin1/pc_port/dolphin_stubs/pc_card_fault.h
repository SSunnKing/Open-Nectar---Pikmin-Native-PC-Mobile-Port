#pragma once
// Scenario-only fault injection for the filesystem memory card (card_stubs.cpp).
// Compiled only with PIKMIN_BUILD_SCENARIOS (PIKI_SCENARIOS=1); a release build
// has no hook and the card behaves as before.
//
// A scenario sets a fault with the CARD_RESULT_* code the call should report and
// clears it when done. Each check counts a hit, so a scenario can prove its fault
// was reached. The card worker thread reads the table, so it is atomic.
#if PIKI_SCENARIOS

enum PcCardFault {
	// CARDProbe reports no card (code ignored, any nonzero sets it).
	PC_CARD_FAULT_PROBE,
	// CARDMountAsync and CARDMount fail at once with the code (as the SDK does for
	// WRONGDEVICE or NOCARD).
	PC_CARD_FAULT_MOUNT,
	// CARDMountAsync starts (READY) and the code arrives as the result (as the SDK
	// reports ENCODING or BROKEN); CARDMount returns the code.
	PC_CARD_FAULT_MOUNT_RESULT,
	// CARDCheckAsync starts (READY) and the code arrives as the result; CARDCheck
	// returns the code.
	PC_CARD_FAULT_CHECK_RESULT,
	// CARDGetSectorSize reports the code as the sector size, in bytes.
	PC_CARD_FAULT_SECTOR_SIZE,
	// CARDFreeBlocks reports no free directory entry (code ignored).
	PC_CARD_FAULT_NO_FREE_FILES,
	// CARDFreeBlocks reports no free bytes (code ignored).
	PC_CARD_FAULT_NO_FREE_BYTES,
	// CARDCreate fails with the code.
	PC_CARD_FAULT_CREATE,
	// CARDWrite fails with the code, before any byte reaches the file.
	PC_CARD_FAULT_WRITE,
	PC_CARD_FAULT_COUNT
};

void pc_card_fault_set(int fault, int code);
void pc_card_fault_clear_all(void);
// How often the fault was applied since it was last set.
int pc_card_fault_hits(int fault);

#endif
