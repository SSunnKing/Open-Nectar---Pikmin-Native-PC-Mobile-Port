// Host test for the deterministic RNG streams.
//
// Covers the MSL sequence for seed 1, the state
// get/set round-trip, independence of the cosmetic stream, and passthrough
// to rand() when deterministic mode is off.

#include "netplay/pc_sim_rng.h"
#include "netplay/pc_netplay_det.h"

#include <cstdio>
#include <cstdlib>

static int failures = 0;

static void check(bool condition, const char* message)
{
	if (!condition) {
		std::fprintf(stderr, "FAIL: %s\n", message);
		++failures;
	}
}

static void setEnv(const char* name, const char* value)
{
#if defined(_WIN32)
	// _putenv updates both the CRT environ (read by getenv) and the process
	// environment; "NAME=" removes the variable.
	char entry[128];
	if (value != nullptr) {
		std::snprintf(entry, sizeof(entry), "%s=%s", name, value);
	} else {
		std::snprintf(entry, sizeof(entry), "%s=", name);
	}
	_putenv(entry);
#else
	if (value != nullptr) {
		setenv(name, value, 1);
	} else {
		unsetenv(name);
	}
#endif
}

static char* emptyArgv[] = { nullptr };

int main()
{
	// Deterministic phase: MSL-compatible sequence for seed 1.
	setEnv("PIKMIN_NETPLAY_DETERMINISTIC", "1");
	setEnv("PIKMIN_NETPLAY_UNTHROTTLED", nullptr);
	pc_netplay_det_init(0, emptyArgv);
	check(pc_netplay_deterministic(), "deterministic switch must be on with env=1");
	check(!pc_netplay_unthrottled(), "unthrottled must be off without env");

	pc_sim_srand(1);
	const int expected[5] = { 16838, 5758, 10113, 17515, 31051 };
	for (int i = 0; i < 5; ++i) {
		const int got = pc_sim_rand();
		if (got != expected[i]) {
			std::fprintf(stderr, "FAIL: seed-1 output %d: got %d want %d\n", i, got, expected[i]);
			++failures;
		}
		if (got < 0 || got > 0x7fff) {
			std::fprintf(stderr, "FAIL: seed-1 output %d out of range: %d\n", i, got);
			++failures;
		}
	}

	// State get/set round-trip.
	pc_sim_srand(0x12345678u);
	check(pc_sim_rng_state() == 0x12345678u, "state getter must return the seed");
	const int before = pc_sim_rand();
	const unsigned mid  = pc_sim_rng_state();
	const int after     = pc_sim_rand();
	check(mid != pc_sim_rng_state(), "state must advance after a draw");
	pc_sim_rng_set_state(mid);
	check(pc_sim_rand() == after, "restored state must reproduce the next draw");
	(void)before;

	// Independence of the cosmetic stream.
	pc_sim_srand(0x11111111u);
	pc_cosmetic_srand(0x22222222u);
	const unsigned simState0 = pc_sim_rng_state();
	const unsigned cosState0 = pc_cosmetic_rng_state();
	for (int i = 0; i < 16; ++i) pc_sim_rand();
	check(pc_cosmetic_rng_state() == cosState0, "sim draws must not advance the cosmetic stream");
	for (int i = 0; i < 16; ++i) pc_cosmetic_rand();
	check(pc_sim_rng_state() != simState0, "sim stream must have advanced");
	const unsigned simState1 = pc_sim_rng_state();
	for (int i = 0; i < 16; ++i) pc_cosmetic_rand();
	check(pc_sim_rng_state() == simState1, "cosmetic draws must not advance the sim stream");
	pc_sim_srand(0xAAAAAAAAu);
	pc_cosmetic_srand(0xAAAAAAAAu);
	check(pc_sim_rand() == pc_cosmetic_rand(), "same seed must give the same first draw on both streams");

	// Float shapers span [0, max] in deterministic mode. Draws are 15-bit, so
	// dividing by a host RAND_MAX of 0x7fffffff would pin every roll near 0.
	check(pc_sim_rand_max() == 0x7fff, "deterministic draw range must be 0x7fff");
	pc_sim_srand(1);
	pc_cosmetic_srand(1);
	float simMax = 0.0f, cosMax = 0.0f, simSum = 0.0f, cosSum = 0.0f;
	const int kRolls = 4096;
	for (int i = 0; i < kRolls; ++i) {
		const float s = pc_sim_randf(1.0f);
		const float c = pc_cosmetic_randf(1.0f);
		check(s >= 0.0f && s <= 1.0f, "sim roll must stay in [0, 1]");
		check(c >= 0.0f && c <= 1.0f, "cosmetic roll must stay in [0, 1]");
		simMax = s > simMax ? s : simMax;
		cosMax = c > cosMax ? c : cosMax;
		simSum += s;
		cosSum += c;
	}
	check(simMax > 0.99f, "sim rolls must reach the top of the range");
	check(cosMax > 0.99f, "cosmetic rolls must reach the top of the range");
	check(simSum / kRolls > 0.45f && simSum / kRolls < 0.55f, "sim roll mean must be near 0.5");
	check(cosSum / kRolls > 0.45f && cosSum / kRolls < 0.55f, "cosmetic roll mean must be near 0.5");

	// Passthrough phase: with the switch off both streams return rand().
	setEnv("PIKMIN_NETPLAY_DETERMINISTIC", nullptr);
	pc_netplay_det_init(0, emptyArgv);
	check(!pc_netplay_deterministic(), "deterministic switch must be off without env/flag");

	std::srand(424242);
	const int simExpected = std::rand();
	const int cosExpected = std::rand();
	std::srand(424242);
	check(pc_sim_rand() == simExpected, "sim stream must passthrough to rand() when off");
	check(pc_cosmetic_rand() == cosExpected, "cosmetic stream must passthrough to rand() when off");

	// Off mode keeps the original rand() / RAND_MAX float bit for bit.
	check(pc_sim_rand_max() == RAND_MAX, "off-mode draw range must be the host RAND_MAX");
	std::srand(777);
	const float simRollExpected = 3.0f * (std::rand() / float(RAND_MAX));
	const float cosRollExpected = 3.0f * (std::rand() / float(RAND_MAX));
	std::srand(777);
	check(pc_sim_randf(3.0f) == simRollExpected, "off-mode sim roll must match rand() / RAND_MAX");
	check(pc_cosmetic_randf(3.0f) == cosRollExpected, "off-mode cosmetic roll must match rand() / RAND_MAX");

	// Moving a draw between streams must not change off-mode values: whatever
	// mix of sim and cosmetic calls is made, the n-th draw is the n-th rand().
	{
		const unsigned kSeed = 20261028u;
		const int kDraws     = 64;
		std::srand(kSeed);
		float expected[kDraws];
		for (int i = 0; i < kDraws; ++i) expected[i] = 5.0f * (std::rand() / float(RAND_MAX));

		// All on the sim stream.
		std::srand(kSeed);
		bool allSim = true;
		for (int i = 0; i < kDraws; ++i) allSim = allSim && pc_sim_randf(5.0f) == expected[i];
		check(allSim, "off-mode all-sim sequence must equal the rand() sequence");

		// Same draws, each site moved to the cosmetic stream.
		std::srand(kSeed);
		bool allCos = true;
		for (int i = 0; i < kDraws; ++i) allCos = allCos && pc_cosmetic_randf(5.0f) == expected[i];
		check(allCos, "off-mode all-cosmetic sequence must equal the rand() sequence");

		// An arbitrary interleave of the two streams, same call order.
		std::srand(kSeed);
		bool mixed = true;
		for (int i = 0; i < kDraws; ++i) {
			const bool cosmetic = ((i * 7 + 3) % 5) < 2;
			const float v       = cosmetic ? pc_cosmetic_randf(5.0f) : pc_sim_randf(5.0f);
			mixed               = mixed && v == expected[i];
		}
		check(mixed, "off-mode interleaved sim/cosmetic sequence must equal the rand() sequence");
	}

	std::printf("PcSimRng: %s\n", failures ? "FAILED" : "all tests passed");
	return failures ? 1 : 0;
}
