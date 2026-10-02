#include "id32.h"

// On little-endian hosts the union's integer view is the byte-reversed multichar
// literal the GameCube code compares against. Store the four characters in big-endian
// logical order in memory (as the GameCube union did) so mStrView / mStringID stay
// sensible, and swap when the API exchanges integer literals.
static inline u32 id32ByteSwap(u32 x)
{
	return (x >> 24) | ((x >> 8) & 0x0000FF00u) | ((x << 8) & 0x00FF0000u) | (x << 24);
}

ID32 ID32::eof('_eof');

/**
 * @note Address: 0x80413254
 * @note Size: 0x1C
 */
bool ID32::isEof()
{
	return getID() == '_eof';
}

/**
 * @note Address: 0x80413270
 * @note Size: 0x38
 */
ID32::ID32()
{
	setID('none');
}

/**
 * @note Address: 0x804132A8
 * @note Size: 0x30
 */
ID32::ID32(u32 id)
{
	setID(id);
}

/**
 * @note Address: 0x804132D8
 * @note Size: 0x24
 */
void ID32::setID(u32 set_id)
{
#if PIKMIN2_ID32_LITTLE_ENDIAN
	mId.mIntView = id32ByteSwap(set_id);
#else
	mId.mIntView = set_id;
#endif
	updateString();
}

/**
 * @brief Checks if the given ID matches the ID stored, with an exception for a specific character.
 *
 * @param _id The ID to be checked.
 * @param exception The character that is exempted from the comparison.
 * @return true if the ID matches the stored ID, false otherwise.
 *
 * @note Address: 0x804132FC
 * @note Size: 0xB8
 */
bool ID32::match(u32 id, char exception)
{
	u32 self = getID();

	// Compare in big-endian logical order (byte 0 == most significant), matching the
	// GameCube behaviour where the union's first byte is the most significant one.
	for (int i = 0; i < 4; i++) {
		int shift = 24 - 8 * i;
		char idByte   = (id >> shift) & 0xFF;
		char selfByte = (self >> shift) & 0xFF;

		if (idByte != exception && idByte != selfByte) {
			return false;
		}
	}

	return true;
}

/**
 * @brief Updates the number ID using the string ID.
 *
 * @note Address: 0x804133B4
 * @note Size: 0x24
 */
void ID32::updateID()
{
	for (int i = 0; i < 4; i++) {
		mId.mStrView[i] = mStringID[i];
	}
}

/**
 * @note Address: 0x804133D8
 * @note Size: 0x2C
 */
void ID32::updateString()
{
	char* c_id = reinterpret_cast<char*>(&mId.mIntView);

	for (int i = 0; i < 4; i++) {
		mStringID[i] = c_id[i];
	}
	mStringID[4] = '\0';
}

/**
 * @note Address: 0x80413404
 * @note Size: 0x30
 */
void ID32::operator=(u32 id)
{
	setID(id);
}

/**
 * @note Address: 0x80413434
 * @note Size: 0x14
 */
bool ID32::operator==(u32 id)
{
	return getID() == id;
}

/**
 * @note Address: 0x80413448
 * @note Size: 0x18
 */
bool ID32::operator!=(u32 id)
{
	return getID() != id;
}

/**
 * @brief Writes the ID32 object to the given stream.
 *
 * @note If in text mode, it converts to a string and writes in the format "{str}". Otherwise, 4 bytes are written.
 *
 * @param stream The stream to write the ID32 object to.
 *
 * @note Address: 0x80413460
 * @note Size: 0x90
 */
void ID32::write(Stream& stream)
{
	if (stream.mMode == STREAM_MODE_TEXT) {
		char str[0x10];
		sprint(str);
		stream.printf("{%s} ", str);
		return;
	}

	stream.writeByte(mId.mStrView[3]);
	stream.writeByte(mId.mStrView[2]);
	stream.writeByte(mId.mStrView[1]);
	stream.writeByte(mId.mStrView[0]);
}

/**
 * @brief Reads the ID32 data from the given stream.
 *
 * @note If in text mode, the function reads the next token to update the ID32 string. Otherwise, it reads four bytes.
 *
 * @param stream The stream to read from.
 *
 * @note Address: 0x804134F0
 * @note Size: 0xEC
 */
void ID32::read(Stream& stream)
{
	if (stream.mMode == STREAM_MODE_TEXT) {
		char* token     = stream.getNextToken();
		mId.mStrView[3] = token[3];
		mId.mStrView[2] = token[2];
		mId.mStrView[1] = token[1];
		mId.mStrView[0] = token[0];
		updateString();
		return;
	}

	mId.mStrView[3] = stream.readByte();
	mId.mStrView[2] = stream.readByte();
	mId.mStrView[1] = stream.readByte();
	mId.mStrView[0] = stream.readByte();
	updateString();
}

/**
 * @note Address: 0x804135DC
 * @note Size: 0x4
 */
void ID32::print()
{
}

/**
 * @brief Converts the ID32 value to a string representation.
 *
 * @note The resulting string will be null-terminated.
 *
 * @param output The character array to store the string representation.
 *
 * @note Address: 0x804135E0
 * @note Size: 0x38
 */
void ID32::sprint(char* output)
{
	// The union stores the characters in big-endian logical order on every host, so
	// copy them directly instead of shifting the raw integer (which is byte-swapped on
	// little-endian machines).
	output[0] = mId.mStrView[0];
	output[1] = mId.mStrView[1];
	output[2] = mId.mStrView[2];
	output[3] = mId.mStrView[3];
	output[4] = '\0';
}
