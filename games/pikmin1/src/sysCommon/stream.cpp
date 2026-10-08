#include "Stream.h"

#include "Common/String.h"
#include <string.h>

#if defined(_WIN32) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define PIKI_STREAM_LITTLE_ENDIAN 1
#else
#define PIKI_STREAM_LITTLE_ENDIAN 0
#endif

static inline u16 streamSwap16(u16 value)
{
	return static_cast<u16>((value >> 8) | (value << 8));
}

static inline u32 streamSwap32(u32 value)
{
	return (value >> 24) | ((value >> 8) & 0x0000FF00u)
	     | ((value << 8) & 0x00FF0000u) | (value << 24);
}

// operator new[] is used without this header being included.
#if defined(BUGFIX)
#include "sysNew.h"
#endif

/**
 * @todo: Documentation
 */
int Stream::readInt()
{
	int i;
	read(&i, sizeof(int));
#if PIKI_STREAM_LITTLE_ENDIAN
	i = static_cast<int>(streamSwap32(static_cast<u32>(i)));
#endif
	return i;
}

/**
 * @todo: Documentation
 */
u8 Stream::readByte()
{
	u8 c;
	read(&c, sizeof(u8));
	return c;
}

/**
 * @todo: Documentation
 */
short Stream::readShort()
{
	short s;
	read(&s, sizeof(short));
#if PIKI_STREAM_LITTLE_ENDIAN
	s = static_cast<short>(streamSwap16(static_cast<u16>(s)));
#endif
	return s;
}

/**
 * @todo: Documentation
 */
f32 Stream::readFloat()
{
	f32 f;
	read(&f, sizeof(f32));
#if PIKI_STREAM_LITTLE_ENDIAN
	u32 bits;
	memcpy(&bits, &f, sizeof(bits));
	bits = streamSwap32(bits);
	memcpy(&f, &bits, sizeof(f));
#endif
	return f;
}

/**
 * @todo: Documentation
 */
char* Stream::readString()
{
	int size = readInt();

	char* str = new char[size + 1];
	read(str, size);
	str[size] = '\0';
	return str;
}

/**
 * @todo: Documentation
 */
void Stream::readString(char* dest, int size)
{
	String str(dest, size);
	readString(str);
}

/**
 * @todo: Documentation
 */
void Stream::readString(String& str)
{
	int size = readInt();
	if (size < 0) {
		size = 0;
	}
	// String(nullptr, 0) is a common destination in the original code.  An
	// empty serialized string still needs one byte for its terminator.
	if (!str.mString || str.mLength < size) {
		str.init(size > 0 ? size : 1);
	}

	read(str.mString, size);
	str.mString[size] = '\0';
}

/**
 * @todo: Documentation
 */
void Stream::writeInt(int i)
{
	int result = i;
#if PIKI_STREAM_LITTLE_ENDIAN
	result = static_cast<int>(streamSwap32(static_cast<u32>(result)));
#endif
	write(&result, sizeof(result));
}

/**
 * @todo: Documentation
 */
void Stream::writeByte(u8 c)
{
	write(&c, sizeof(u8));
}

/**
 * @todo: Documentation
 */
void Stream::writeShort(short _s)
{
	short s = _s;
#if PIKI_STREAM_LITTLE_ENDIAN
	s = static_cast<short>(streamSwap16(static_cast<u16>(s)));
#endif
	write(&s, sizeof(short));
}

/**
 * @todo: Documentation
 */
void Stream::writeFloat(f32 f)
{
	f32 result = f;
#if PIKI_STREAM_LITTLE_ENDIAN
	u32 bits;
	memcpy(&bits, &result, sizeof(bits));
	bits = streamSwap32(bits);
	memcpy(&result, &bits, sizeof(result));
#endif
	write(&result, sizeof(f32));
}

/**
 * @todo: Documentation
 */
void Stream::writeString(immut char* str)
{
	// `String` can't decide if it wants to be owning or non-owning.
	String s(const_cast<char*>(str), 0);
	writeString(s);
}

/**
 * @todo: Documentation
 */
void Stream::writeString(immut String& s)
{
	s32 length = ALIGN_NEXT(s.getLength(), 4);
	writeInt(length);
	write(s.mString, s.getLength());

	char c = 0;
	for (s32 i = 0; i < length - s.getLength(); i++) {
		write(&c, 1);
	}
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 0000C4 (Matching by size)
 */
void Stream::print(immut char* fmt, ...)
{
	char dest[1024];
	va_list args;
	va_start(args, fmt);
	vsprintf(dest, fmt, args);
	va_end(args);
	if (strlen(dest)) {
		write(dest, strlen(dest));
	}
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 000064 (Matching by size)
 */
void Stream::vPrintf(immut char* param_1, va_list args)
{
	char dest[1024];
	vsprintf(dest, param_1, args);
	if (strlen(dest) != 0) {
		write(dest, strlen(dest));
	}
}

/**
 * @todo: Documentation
 */
void Stream::read(void*, int)
{
}

/**
 * @todo: Documentation
 */
void Stream::write(immut void*, int)
{
}

/**
 * @todo: Documentation
 */
int Stream::getPending()
{
	return 0;
}

/**
 * @todo: Documentation
 */
int Stream::getAvailable()
{
	return 0;
}

/**
 * @todo: Documentation
 */
void Stream::close()
{
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 00006C
 */
void RandomAccessStream::writeTo(int position, immut void* buffer, int length)
{
	setPosition(position);
	write(buffer, length);
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 00006C (Matching by size)
 */
void RandomAccessStream::readFrom(int position, void* buffer, int length)
{
	setPosition(position);
	read(buffer, length);
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 00005C (Matching by size)
 */
void RandomAccessStream::writeIntTo(int position, int value)
{
	setPosition(position);
	writeInt(value);
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 00004C (Matching by size)
 */
int RandomAccessStream::readIntFrom(int position)
{
	setPosition(position);
	int value = readInt();
	return value;
}
