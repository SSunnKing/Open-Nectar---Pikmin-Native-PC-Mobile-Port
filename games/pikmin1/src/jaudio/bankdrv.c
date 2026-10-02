#include "jaudio/bankdrv.h"

#include "jaudio/bx.h"
#include "jaudio/ja_calc.h"
#include "jaudio/random.h"
#include "jaudio/rate.h"
#include <stddef.h>

/*
 * This table is consumed as signed 16-bit envelope op/value pairs. Keeping
 * it in its actual element type makes the sequence identical on big- and
 * little-endian targets.
 */
static s16 FORCE_RELEASE_TABLE[6] = { 0, 5, 0, 15, 0, 0 };

#ifdef PIKI_PC_PORT
static s16 Bank_ReadOscTableValue(const s16* table, u32 index, u8 bigEndian)
{
	if (!bigEndian) {
		return table[index];
	}

	const u8* bytes = reinterpret_cast<const u8*>(table) + index * sizeof(s16);
	return static_cast<s16>((static_cast<u16>(bytes[0]) << 8) | bytes[1]);
}
#endif

/**
 * @TODO: Documentation
 */
Inst_* Bank_InstChange(Bank_* bank, volatile u32 VOLATILE_index)
{
	STACK_PAD_VAR(1);
	u32 index;

	index = VOLATILE_index;
	if (!bank) {
		return NULL;
	}
	return bank->mInstruments[index];
}

/**
 * @TODO: Documentation
 * @note UNUSED Size: 000030
 */
Voice_* Bank_VoiceChange(Bank_* bank, volatile u32 VOLATILE_index)
{
	STACK_PAD_VAR(1);
	u32 index;

	index = VOLATILE_index;
	if (!bank) {
		return NULL;
	}
	return bank->mVoices[index];
}

/**
 * @TODO: Documentation
 */
Perc_* Bank_PercChange(Bank_* bank, volatile u32 VOLATILE_index)
{
	STACK_PAD_VAR(1);
	u32 index;

	index = VOLATILE_index;
	if (!bank) {
		return NULL;
	}
	return bank->mPercs[index];
}

/**
 * @TODO: Documentation
 */
int Bank_GetInstKeymap(Inst_* inst, u8 key)
{
	if (!inst) {
		return 0;
	}

	for (u32 i = 0; i < inst->mKeyRegionCount; i++) {
		if (key <= inst->mKeyRegions[i]->mBaseKey) {
			return i; // Return the index of the keymap
		}
	}

	return -1;
}

/**
 * @TODO: Documentation
 */
Vmap_* Bank_GetInstVmap(Inst_* inst, u8 key, u8 velocity)
{
	STACK_PAD_VAR(1);

	if (!inst) {
		return NULL;
	}

	int instIndex = Bank_GetInstKeymap(inst, key);
	if (instIndex != -1) {
		u8* REF_p3       = &velocity;
		InstKeymap_* keymap = inst->mKeyRegions[instIndex];
		for (u32 i = 0; i < keymap->mVelocityCount; i++) {
			Vmap_* vmap = keymap->mVelocities[i];
			if (velocity <= vmap->mBaseVelocity) {
				return vmap;
			}
		}

		return NULL;
	}

	return NULL;
}

/**
 * @TODO: Documentation
 */
Vmap_* Bank_GetPercVmap(Perc_* perc, u8 keyIdx, u8 vel)
{
	if (!perc) {
		return 0;
	}

	PercKeymap_* keymap = perc->mKeyRegions[keyIdx];
	if (!keymap) {
		return 0;
	}
	for (u32 i = 0; i < keymap->mVelocityCount; i++) {
		Vmap_* vmap = keymap->mVelocities[i];
		if (vel <= vmap->mBaseVelocity) {
			return vmap;
		}
	}
	return 0;
}

/**
 * @TODO: Documentation
 * @note UNUSED Size: 000010
 */
int Bank_GetVoiceMap(Voice_* voice, u16 id)
{
	TRAP_UNIMPLEMENTED;
}

/**
 * @TODO: Documentation
 */
f32 Bank_SenseToOfs(Sense_* sensor, u8 inputValue)
{
	if (!sensor) {
		return 1.0f;
	}

	if (sensor->threshold == 127 || sensor->threshold == 0) {
		return sensor->min + (f32)inputValue * (sensor->max - sensor->min) / 127.0f;
	}

	if (inputValue < sensor->threshold) {
		return sensor->min + (1.0f - sensor->min) * ((f32)inputValue / (f32)sensor->threshold);
	}

	int a = inputValue - sensor->threshold;
	int b = 127 - sensor->threshold;

	return 1.0f + (sensor->max - 1.0f) * ((f32)a / (f32)b);
}

/**
 * @TODO: Documentation
 */
f32 Bank_RandToOfs(Rand_* rand)
{
	f32 value;

	if (!rand) {
		return 1.0f;
	}
	value = GetRandom_sf32();
	value *= rand->range;
	value += rand->value;
	return value;
}

/**
 * @TODO: Documentation
 */
f32 Bank_OscToOfs(Osc_* osc, Oscbuf_* buf)
{
	f32 sub;
	int offset;
	s16 val0, val1, val2;
	f32 calc;
	s16* table;
#ifdef PIKI_PC_PORT
	u8 tableBigEndian;
#endif

	if (osc == NULL) {
		buf->value = 1.0f;
		return 1.0f;
	}

	if (buf->state == 4) {
		if (osc->attackVecOffset != osc->releaseVecOffset) {
			buf->tableIndex  = 0;
			buf->timeCounter = 0.0f;
			buf->targetValue = buf->value;
		}
		if (osc->releaseVecOffset == 0 && buf->releaseParam == 0) {
			buf->releaseParam = 0x10;
		}

		if (buf->releaseParam) {
			buf->state     = 8;
			buf->curveType = buf->releaseParam >> 14 & 3;
			f32 x          = buf->releaseParam & 0x3fff;
			x *= (JAC_DAC_RATE / 80.0f) / 600.0f;
			buf->timeCounter = x;
			if (buf->timeCounter < 1.0f) {
				buf->timeCounter = 1.0f;
			}
			buf->targetValue = 0.0f;
			buf->deltaRate   = (buf->targetValue - buf->value) / buf->timeCounter;
		} else {
			buf->state = 5;
		}
	}
	if (buf->state == 6) {
		buf->tableIndex  = 0;
		buf->timeCounter = 0.0f;
		buf->targetValue = buf->value;
		buf->state       = 7;
	}

	if (buf->state == 5) {
		table = osc->releaseVecOffset;
#ifdef PIKI_PC_PORT
		tableBigEndian = osc->releaseVecBigEndian;
#endif
	} else if (buf->state == 7) {
		table = FORCE_RELEASE_TABLE;
#ifdef PIKI_PC_PORT
		tableBigEndian = FALSE;
#endif
	} else {
		table = osc->attackVecOffset;
#ifdef PIKI_PC_PORT
		tableBigEndian = osc->attackVecBigEndian;
#endif
	}

	if (table == NULL && buf->state != 8) {
		buf->value = 1.0f;
		return 1.0f;
	}
	if (buf->state == 0) {
		return 1.0f;
	}
	if (buf->state == 3) {
		return buf->value * osc->width + osc->vertex;
	}

	if (buf->state == 1) {
		buf->state        = 2;
		buf->tableIndex   = 0;
		buf->timeCounter  = 0.0f;
		buf->targetValue  = 0.0f;
		buf->releaseParam = 0;
		sub               = osc->rate;
	} else {
		sub = osc->rate;
	}
	if (buf->state == 7) {
		sub = 1.0f;
	}
	buf->timeCounter -= sub;

	while (buf->timeCounter <= 0.0f) {
		offset     = (buf->tableIndex * 3);
		buf->value = buf->targetValue;
		if (buf->state == 8) {
			buf->state = 0;
			break;
		}
#ifdef PIKI_PC_PORT
		val0 = Bank_ReadOscTableValue(table, offset + 0, tableBigEndian);
		val1 = Bank_ReadOscTableValue(table, offset + 1, tableBigEndian);
		val2 = Bank_ReadOscTableValue(table, offset + 2, tableBigEndian);
#else
		val0 = table[offset + 0];
		val1 = table[offset + 1];
		val2 = table[offset + 2];
#endif

		if (val0 == 0xd) {
			buf->tableIndex = val2;
			continue;
		} else if (val0 == 0xf) {
			buf->state = 0;
			break;
		} else if (val0 == 0xe) {
			buf->state = 3;
			return buf->value * osc->width + osc->vertex;
		}
		buf->curveType = val0;

		if (val1 == 0) {
			buf->targetValue = val2 / 32768.0f;
			buf->tableIndex++;
		} else {
			f32 x = (u16)val1;
			x *= (JAC_DAC_RATE / 80.0f) / 600.0f;
			buf->timeCounter = x;
			buf->targetValue = val2 / 32768.0f;
			buf->deltaRate   = (buf->targetValue - buf->value) / buf->timeCounter;
			buf->tableIndex++;
		}
	}

	calc       = -(buf->deltaRate * buf->timeCounter - buf->targetValue);
	buf->value = calc;

	switch (buf->curveType) {
	case 0:
	{
		break;
	}
	case 1:
	{
		if (calc > 0.0f) {
			calc = calc * calc;
		} else {
			calc = -calc * calc;
		}
		break;
	}
	case 2:
	{
		if (calc > 0.0f) {
			calc = sqrtf2(calc);
		} else {
			calc = -sqrtf2(-calc);
		}
		break;
	}
	}
	return calc * osc->width + osc->vertex;
}
