#include "types.h"
#include "JSystem/JGadget/binary.h"
#include "JSystem/JStudio/fvb-data-parse.h"

namespace JStudio {
namespace fvb {
namespace data {
/**
 * @note Address: 0x8000CA3C
 * @note Size: 0x68
 */
void TParse_TParagraph::getData(TParse_TParagraph::TData* data) const
{
#ifdef PIKI_PC_PORT
	// The FVB block was promoted to host order (pc_promote_fvb), so the
	// shared big-endian parseVariableUInt_16_32_following must not be used.
	const u8* raw = (const u8*)mRaw;
	const u8* parse;
	u16 first;
	__builtin_memcpy(&first, raw, 2);
	if (!(first & 0x8000)) {
		u16 t;
		__builtin_memcpy(&t, raw + 2, 2);
		data->mSize = first;
		data->mType = t;
		parse       = raw + 4;
	} else {
		u16 lo;
		u32 t;
		__builtin_memcpy(&lo, raw + 2, 2);
		__builtin_memcpy(&t, raw + 4, 4);
		data->mSize = ((u32)first << 16 & 0x7FFF0000u) | lo;
		data->mType = t;
		parse       = raw + 8;
	}
#else
	const u8* parse = (const u8*)JGadget::binary::parseVariableUInt_16_32_following(mRaw, (u32*)data, (u32*)&data->mType, 0);
#endif
	u32 t           = data->mSize;
	if (!t) {
		data->mContent = nullptr;
		data->mNext    = parse;
	} else {
		data->mContent = parse;
		data->mNext    = parse + align_roundUp(t, 4);
	}
}

} // namespace data
} // namespace fvb
} // namespace JStudio
