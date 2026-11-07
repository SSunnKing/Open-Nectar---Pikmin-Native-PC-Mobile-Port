// Netplay desync forensics. See pc_state_dump.h.

#include "netplay/pc_state_dump.h"

#include "Boss.h"
#include "GoalItem.h"
#include "ItemMgr.h"
#include "NaviMgr.h"
#include "ObjType.h"
#include "Pellet.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PlayerState.h"
#include "gameflow.h"
#include "teki.h"
#include "sysNew.h"

#include <cstdio>
#include <cstring>

namespace fx = pc_netplay_forensics;

namespace {
constexpr uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime  = 1099511628211ULL;

uint64_t fnv(uint64_t h, const uint8_t* p, size_t n)
{
	for (size_t i = 0; i < n; ++i) {
		h ^= (uint64_t)p[i];
		h *= kFnvPrime;
	}
	return h;
}

// Byte stream builder: the same little-endian bytes pc_state_hash.cpp's mixU32
// feeds FNV-1a, so mixing the concatenation equals mixing field by field.
struct Buf {
	uint8_t b[640];
	size_t n = 0;
	void u32(uint32_t v)
	{
		if (n + 4 > sizeof(b)) return; // never reached: the widest object is ~120 bytes
		for (int i = 0; i < 4; ++i) b[n++] = (uint8_t)((v >> (8 * i)) & 0xFF);
	}
	void s32(int32_t v) { u32((uint32_t)v); }
	void f32(float v)
	{
		uint32_t bits = 0;
		std::memcpy(&bits, &v, sizeof(bits));
		u32(bits);
	}
	void vec(const Vector3f& v)
	{
		f32(v.x);
		f32(v.y);
		f32(v.z);
	}
};

struct Slot {
	bool valid   = false;
	uint64_t tick = 0;
	uint64_t xtra = 0;
	std::vector<fx::ObjRec> recs;
};

bool sEnabled        = false;
Slot sRing[kStateDumpRingTicks];
uint64_t sLastXtra   = 0;
unsigned sMismatches = 0;
bool sMismatchLogged = false;

void fill_motion(fx::ObjRec& r, Creature* c)
{
	r.f[0]  = c->mSRT.t.x;
	r.f[1]  = c->mSRT.t.y;
	r.f[2]  = c->mSRT.t.z;
	r.f[3]  = c->mSRT.r.x;
	r.f[4]  = c->mSRT.r.y;
	r.f[5]  = c->mSRT.r.z;
	r.f[6]  = c->mVelocity.x;
	r.f[7]  = c->mVelocity.y;
	r.f[8]  = c->mVelocity.z;
	r.f[9]  = c->mTargetVelocity.x;
	r.f[10] = c->mTargetVelocity.y;
	r.f[11] = c->mTargetVelocity.z;
	r.f[12] = c->mFaceDirection;
}

void mix_motion(Buf& b, Creature* c)
{
	b.vec(c->mSRT.t);
	b.vec(c->mSRT.r);
	b.vec(c->mVelocity);
	b.vec(c->mTargetVelocity);
	b.f32(c->mFaceDirection);
}

// Fields every Creature carries that the seven columns do not mix.
void creature_extra(Buf& x, Creature* c)
{
	// These fields affect future high-rate motion even when current position
	// and velocity agree. Keep the original 30 Hz diagnostic schema intact.
	if (PcBallisticClock::enabled(gsys->getFrameTime())) {
		x.f32(c->mPcBallisticClock.elapsed());
		x.vec(c->mPcVolatileTransport);
		x.vec(c->mPcVolatileInput);
		x.u32(c->mPcVolatileMode);
	}
	x.u32(c->mCreatureFlags);
	x.f32(c->mHealth);
	x.vec(c->mVolatileVelocity);
	x.vec(c->_B0);
	x.vec(c->mFixedPosition);
	x.f32(c->mAirResistance);
	x.f32(c->mGroundOffset);
	x.s32(c->mRebirthDay);
	x.u32(c->mWaterFxTimer);
	x.vec(c->mLastPosition);
	x.vec(c->mAttachPosition);
	x.vec(c->mPrevAngularVelocity);
	x.u32(c->mHasCollChangedVelocity);
	x.u32(c->mCollisionOccurred);
	x.s32(c->mPelletStickSlot);
	x.u32(c->mIsBeingDamaged ? 1u : 0u);
	x.u32(c->mIsFrozen);
	x.f32(c->mCollisionRadius);
	// Link presence only (never a pointer value).
	uint32_t links = 0;
	if (c->mStickTarget != nullptr) links |= 1u;
	if (!c->mHoldingCreature.isNull()) links |= 2u;
	if (!c->mGrabbedCreature.isNull()) links |= 4u;
	if (c->mRope != nullptr) links |= 8u;
	if (c->mCollPlatform != nullptr) links |= 16u;
	if (c->mStickListHead != nullptr) links |= 32u;
	x.u32(links);
}

uint64_t xhash_piki(Piki* p)
{
	Buf x;
	creature_extra(x, p);
	x.u32(p->mActionState);
	x.u32(p->mIsPanicked ? 1u : 0u);
	x.u32(p->mInWaterTimer);
	x.f32(p->mMotionSpeed);
	x.f32(p->mMoveSpeed);
	x.f32(p->mDeathTimer);
	x.f32(p->mFlickIntensity);
	x.f32(p->mRotationAngle);
	x.u32(p->mIsWhistlePending ? 1u : 0u);
	x.s32(p->mPlayerId);
	x.u32(p->mMode);
	x.s32(p->mFormationPriority);
	x.vec(p->mPushTargetPos);
	x.vec(p->mPluckVelocity);
	x.s32(p->mFloweringTimer);
	x.s32(p->mCurrRoutePoint);
	x.s32(p->mNumRoutePoints);
	x.u32(p->mWantToStick ? 1u : 0u);
	x.u32(p->mEmotion);
	x.s32(p->mNavi != nullptr ? (int32_t)p->mNavi->mNaviID : -1);
	uint32_t l = 0;
	if (p->mLeaderCreature != nullptr) l |= 1u;
	if (p->mCarryingShipPart != nullptr) l |= 2u;
	if (p->mCurrNectar != nullptr) l |= 4u;
	if (p->mPushTargetPiki != nullptr) l |= 8u;
	x.u32(l);
	return fnv(kFnvOffset, x.b, x.n);
}

uint64_t xhash_teki(Teki* t)
{
	Buf x;
	if (PcBallisticClock::enabled(gsys->getFrameTime())) x.f32(t->mPcActionBallisticClock.elapsed());
	creature_extra(x, t);
	x.s32(t->mDeadState);
	x.s32(t->mReturnStateID);
	x.s32(t->mCurrentQueueId);
	x.s32(t->mActionStateId);
	x.f32(t->mStoredDamage);
	x.f32(t->mDamageCount);
	x.s32(t->mRouteWayPointCount);
	x.s32(t->mCurrRouteWayPointID);
	x.vec(t->mTargetPosition);
	x.f32(t->mTargetAngle);
	x.vec(t->mActionVelocity);
	x.s32(t->mCurrentAnimEvent);
	x.f32(t->mAnimationSpeed);
	x.s32(t->mMotionLoopCount);
	x.f32(t->mMotionSpeed);
	for (int i = 0; i < 5; ++i) x.f32(t->mTimers[i]);
	x.s32(t->mTekiOptions);
	x.s32(t->mAnimKeyOptions);
	uint32_t tg = 0;
	for (int i = 0; i < 4; ++i)
		if (!t->mTargetCreatures[i].isNull()) tg |= 1u << i;
	x.u32(tg);
	return fnv(kFnvOffset, x.b, x.n);
}

uint64_t xhash_boss(Boss* b)
{
	Buf x;
	creature_extra(x, b);
	x.u32((b->isAlive() ? 1u : 0u) | (b->isAtari() ? 2u : 0u) | (b->isVisible() ? 4u : 0u)
	      | (b->isOrganic() ? 8u : 0u) | (b->getInvincible() ? 16u : 0u) | (b->getMotionFinish() ? 32u : 0u)
	      | (b->getOnWall() ? 64u : 0u));
	x.f32(b->getAnimTimer());
	x.f32(b->getDamagePoint());
	x.f32(b->getMaxLife());
	x.f32(b->getWalkTimer());
	x.f32(b->getAttackTimer());
	x.s32(b->getLoopCounter());
	x.s32(b->getFlickDamageCount());
	x.s32(b->getItemColour());
	x.vec(*b->getInitPosition());
	x.vec(*b->getTargetPosition());
	x.u32(b->getTargetCreature() != nullptr ? 1u : 0u);
	return fnv(kFnvOffset, x.b, x.n);
}

uint64_t xhash_plain(Creature* c)
{
	Buf x;
	creature_extra(x, c);
	return fnv(kFnvOffset, x.b, x.n);
}

void push(std::vector<fx::ObjRec>& out, fx::ObjRec& r, const Buf& b, uint64_t* chain, uint64_t xh,
          uint64_t* xtraChain)
{
	r.hash  = fnv(kFnvOffset, b.b, b.n);
	r.xhash = xh;
	if (chain != nullptr) *chain = fnv(*chain, b.b, b.n);
	if (xtraChain != nullptr) {
		uint8_t k[9];
		k[0] = r.kind;
		for (int i = 0; i < 8; ++i) k[1 + i] = (uint8_t)((xh >> (8 * i)) & 0xFF);
		*xtraChain = fnv(*xtraChain, k, sizeof(k));
	}
	out.push_back(r);
}
} // namespace

void pc_state_dump_set_enabled(bool on) { sEnabled = on; }
bool pc_state_dump_enabled(void) { return sEnabled; }
uint64_t pc_state_dump_last_xtra(void) { return sLastXtra; }
unsigned pc_state_dump_coverage_mismatches(void) { return sMismatches; }

void pc_state_dump_capture(uint64_t tick, const uint64_t subs[7])
{
	if (!sEnabled) return;
	Slot& slot = sRing[tick % kStateDumpRingTicks];
	slot.valid = false;
	slot.tick  = tick;
	slot.recs.clear();
	std::vector<fx::ObjRec>& out = slot.recs;
	uint64_t xtra = kFnvOffset;

	uint64_t cNavi = 0, cPiki = 0, cTeki = 0, cItem = 0, cWorld = 0;

	if (naviMgr != nullptr) {
		cNavi = kFnvOffset;
		int ord = 0;
		Iterator it(naviMgr);
		CI_LOOP(it)
		{
			Navi* n = static_cast<Navi*>(*it);
			if (n == nullptr) continue;
			Buf b;
			b.s32((int32_t)n->mNaviID);
			mix_motion(b, n); // pos rot vel drive face, in the hash's order
			// The hash mixes health after face; keep that order (mix_motion ends with face).
			b.f32(n->mHealth);
			AState<Navi>* st = n->getCurrState();
			const int32_t sid = st != nullptr ? (int32_t)st->getID() : (int32_t)-1;
			b.s32(sid);
			fx::ObjRec r;
			r.kind   = fx::kNavi;
			r.ord    = ord++;
			r.type   = (int32_t)n->mNaviID;
			r.state  = sid;
			r.health = n->mHealth;
			fill_motion(r, n);
			push(out, r, b, &cNavi, xhash_plain(n), &xtra);
		}
	}
	if (naviMgr != nullptr && pikiMgr != nullptr) {
		cPiki = kFnvOffset;
		int ord = 0;
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			Piki* p = static_cast<Piki*>(*it);
			if (p == nullptr) continue;
			Buf b;
			b.u32((uint32_t)p->mColor);
			b.s32((int32_t)p->mHappa);
			mix_motion(b, p);
			b.f32(p->mHealth);
			AState<Piki>* st = p->getCurrState();
			const int32_t sid = st != nullptr ? (int32_t)st->getID() : (int32_t)-1;
			b.s32(sid);
			fx::ObjRec r;
			r.kind   = fx::kPiki;
			r.ord    = ord++;
			r.type   = (int32_t)p->mColor;
			r.state  = sid;
			r.aux[0] = (int32_t)p->mHappa;
			r.health = p->mHealth;
			fill_motion(r, p);
			push(out, r, b, &cPiki, xhash_piki(p), &xtra);
		}
	}
	if (naviMgr != nullptr && tekiMgr != nullptr) {
		cTeki = kFnvOffset;
		int ord = 0;
		Iterator it(tekiMgr);
		CI_LOOP(it)
		{
			Teki* t = static_cast<Teki*>(*it);
			if (t == nullptr) continue;
			Buf b;
			b.s32((int32_t)t->mTekiType);
			mix_motion(b, t);
			b.f32(t->mHealth);
			b.s32((int32_t)t->mStateID);
			fx::ObjRec r;
			r.kind   = fx::kTeki;
			r.ord    = ord++;
			r.type   = (int32_t)t->mTekiType;
			r.state  = (int32_t)t->mStateID;
			r.health = t->mHealth;
			fill_motion(r, t);
			push(out, r, b, &cTeki, xhash_teki(t), &xtra);
		}
	}
	if (naviMgr != nullptr && (itemMgr != nullptr || pelletMgr != nullptr || bossMgr != nullptr)) {
		cItem = kFnvOffset;
		if (itemMgr != nullptr) {
			int ord = 0;
			Iterator it(itemMgr);
			CI_LOOP(it)
			{
				Creature* c = *it;
				if (c == nullptr) continue;
				Buf b;
				b.s32((int32_t)c->mObjType);
				mix_motion(b, c);
				fx::ObjRec r;
				r.kind   = fx::kItem;
				r.ord    = ord++;
				r.type   = (int32_t)c->mObjType;
				r.health = c->mHealth;
				fill_motion(r, c);
				if (c->mObjType == OBJTYPE_Goal) {
					GoalItem* goal = static_cast<GoalItem*>(c);
					b.u32((uint32_t)goal->mOnionColour);
					b.u32(goal->mHeldPikis[0]);
					b.u32(goal->mHeldPikis[1]);
					b.u32(goal->mHeldPikis[2]);
					r.aux[0] = (int32_t)goal->mOnionColour;
					r.aux[1] = (int32_t)goal->mHeldPikis[0];
					r.aux[2] = (int32_t)goal->mHeldPikis[1];
					r.aux[3] = (int32_t)goal->mHeldPikis[2];
				}
				push(out, r, b, &cItem, xhash_plain(c), &xtra);
			}
			for (int color = PikiMinColor; color <= PikiMaxColor; ++color) {
				GoalItem* onion = itemMgr->getContainer(color);
				Buf b;
				fx::ObjRec r;
				r.kind = fx::kOnion;
				r.ord  = color;
				r.type = color;
				if (onion == nullptr) {
					b.u32(0);
					b.u32(0);
					b.u32(0);
				} else {
					b.u32(onion->mHeldPikis[0]);
					b.u32(onion->mHeldPikis[1]);
					b.u32(onion->mHeldPikis[2]);
					r.aux[0] = (int32_t)onion->mHeldPikis[0];
					r.aux[1] = (int32_t)onion->mHeldPikis[1];
					r.aux[2] = (int32_t)onion->mHeldPikis[2];
				}
				push(out, r, b, &cItem, 0, &xtra);
			}
		}
		if (pelletMgr != nullptr) {
			int ord = 0;
			Iterator it(pelletMgr);
			CI_LOOP(it)
			{
				Pellet* p = static_cast<Pellet*>(*it);
				if (p == nullptr) continue;
				Buf b;
				fx::ObjRec r;
				r.kind = fx::kPellet;
				r.ord  = ord++;
				if (p->mConfig != nullptr) {
					b.u32(p->mConfig->mPelletId.mId);
					b.s32((int32_t)p->mConfig->mPelletType());
					r.type   = (int32_t)p->mConfig->mPelletId.mId;
					r.aux[0] = (int32_t)p->mConfig->mPelletType();
				} else {
					b.u32(0);
					b.s32(0);
				}
				mix_motion(b, p);
				AState<Pellet>* st = p->getCurrState();
				const int32_t sid = st != nullptr ? (int32_t)st->getID() : (int32_t)-1;
				b.s32(sid);
				b.s32((int32_t)p->getState());
				b.u32((uint32_t)p->mCarrierCount);
				r.state  = sid;
				r.aux[1] = (int32_t)p->getState();
				r.aux[2] = (int32_t)p->mCarrierCount;
				r.health = p->mHealth;
				fill_motion(r, p);
				push(out, r, b, &cItem, xhash_plain(p), &xtra);
			}
		}
		if (bossMgr != nullptr) {
			int ord = 0;
			Iterator it(bossMgr);
			CI_LOOP(it)
			{
				Boss* bo = static_cast<Boss*>(*it);
				if (bo == nullptr) continue;
				Buf b;
				b.s32((int32_t)bo->getCurrentState());
				b.s32((int32_t)bo->getNextState());
				b.f32(bo->getCurrentLife());
				mix_motion(b, bo);
				fx::ObjRec r;
				r.kind   = fx::kBoss;
				r.ord    = ord++;
				r.type   = (int32_t)bo->mObjType;
				r.state  = (int32_t)bo->getCurrentState();
				r.aux[0] = (int32_t)bo->getNextState();
				r.health = bo->getCurrentLife();
				fill_motion(r, bo);
				push(out, r, b, &cItem, xhash_boss(bo), &xtra);
			}
		}
	}
	{
		cWorld = kFnvOffset;
		Buf b;
		b.f32(gameflow.mWorldClock.mTimeOfDay);
		b.s32((int32_t)gameflow.mWorldClock.mCurrentDay);
		fx::ObjRec r;
		r.kind = fx::kWorld;
		r.ord  = 0;
		r.type = (int32_t)gameflow.mWorldClock.mCurrentDay;
		r.f[0] = gameflow.mWorldClock.mTimeOfDay;
		if (playerState != nullptr) {
			b.s32((int32_t)playerState->mLivingPikiNum);
			b.s32((int32_t)playerState->mTotalBornPikiNum);
			b.s32((int32_t)playerState->mTotalDeadPikiNum);
			b.s32((int32_t)playerState->mTotalPluckedPikiCount);
			r.aux[0] = (int32_t)playerState->mLivingPikiNum;
			r.aux[1] = (int32_t)playerState->mTotalBornPikiNum;
			r.aux[2] = (int32_t)playerState->mTotalDeadPikiNum;
			r.aux[3] = (int32_t)playerState->mTotalPluckedPikiCount;
		} else {
			b.s32(0);
			b.s32(0);
			b.s32(0);
			b.s32(0);
		}
		push(out, r, b, &cWorld, 0, nullptr);
	}
	{
		// rng and mode are single values; their records carry the column itself.
		fx::ObjRec r;
		r.kind = fx::kRng;
		r.ord  = 0;
		r.hash = subs[5];
		r.aux[0] = (int32_t)(uint32_t)(subs[5] & 0xFFFFFFFFu);
		out.push_back(r);
		fx::ObjRec q;
		q.kind = fx::kMode;
		q.ord  = 0;
		q.hash = subs[6];
		q.aux[0] = (int32_t)(uint32_t)(subs[6] & 0xFFFFFFFFu);
		q.aux[1] = (int32_t)(uint32_t)(subs[6] >> 32);
		out.push_back(q);
	}
	slot.xtra  = xtra;
	slot.valid = true;
	sLastXtra  = xtra;

	// Coverage check against the real columns (navi piki teki item world).
	const uint64_t mine[5] = { cNavi, cPiki, cTeki, cItem, cWorld };
	for (int i = 0; i < 5; ++i) {
		if (mine[i] == subs[i]) continue;
		++sMismatches;
		if (!sMismatchLogged) {
			sMismatchLogged = true;
			std::printf("[netplay] state dump: coverage mismatch at tick=%llu in %s (dump %016llx, hash %016llx); "
			            "the per-object records do not reproduce that column\n",
			            (unsigned long long)tick, fx::kSubNames[i], (unsigned long long)mine[i],
			            (unsigned long long)subs[i]);
			std::fflush(stdout);
		}
		break;
	}
}

namespace {
Creature* find_creature(int kind, int ord)
{
	Creature* target = nullptr;
	int at           = 0;
	if (kind == fx::kNavi && naviMgr != nullptr) {
		Iterator it(naviMgr);
		CI_LOOP(it)
		{
			if (*it != nullptr && at++ == ord) target = *it;
		}
	} else if (kind == fx::kPiki && pikiMgr != nullptr) {
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			if (*it != nullptr && at++ == ord) target = *it;
		}
	} else if (kind == fx::kTeki && tekiMgr != nullptr) {
		Iterator it(tekiMgr);
		CI_LOOP(it)
		{
			if (*it != nullptr && at++ == ord) target = *it;
		}
	}
	return target;
}
} // namespace

bool pc_state_dump_test_nudge(int kind, int ord)
{
	Creature* target = find_creature(kind, ord);
	if (target == nullptr) return false;
	target->mSRT.t.x += 1.0f;
	return true;
}

bool pc_state_dump_apply_motion(const std::vector<fx::ObjRec>& recs)
{
	std::vector<Creature*> targets;
	for (const fx::ObjRec& r : recs) {
		Creature* c = find_creature(r.kind, r.ord);
		if (c == nullptr) return false;
		targets.push_back(c);
	}
	for (size_t i = 0; i < recs.size(); ++i) {
		const fx::ObjRec& r = recs[i];
		Creature* c         = targets[i];
		auto put = [&](Vector3f& v, int at) {
			v.x = r.f[at];
			v.y = r.f[at + 1];
			v.z = r.f[at + 2];
		};
		put(c->mSRT.t, 0);
		put(c->mSRT.r, 3);
		put(c->mVelocity, 6);
		put(c->mTargetVelocity, 9);
		c->mFaceDirection = r.f[12];
		c->mHealth        = r.health;
	}
	return true;
}

const std::vector<fx::ObjRec>* pc_state_dump_find(uint64_t tick)
{
	const Slot& s = sRing[tick % kStateDumpRingTicks];
	return (s.valid && s.tick == tick) ? &s.recs : nullptr;
}

bool pc_state_dump_xtra_of(uint64_t tick, uint64_t* xtra)
{
	const Slot& s = sRing[tick % kStateDumpRingTicks];
	if (!s.valid || s.tick != tick) return false;
	if (xtra != nullptr) *xtra = s.xtra;
	return true;
}

uint64_t pc_state_dump_oldest_tick(void)
{
	uint64_t best = 0;
	for (const Slot& s : sRing)
		if (s.valid && (best == 0 || s.tick < best)) best = s.tick;
	return best;
}

bool pc_state_dump_write_tick(FILE* f, uint64_t tick)
{
	const Slot& s = sRing[tick % kStateDumpRingTicks];
	if (f == nullptr || !s.valid || s.tick != tick) return false;
	std::fprintf(f, "# tick %llu: %zu records; xtra=%016llx\n", (unsigned long long)tick, s.recs.size(),
	             (unsigned long long)s.xtra);
	for (const fx::ObjRec& r : s.recs) std::fprintf(f, "%s\n", fx::format_obj(r).c_str());
	return true;
}
