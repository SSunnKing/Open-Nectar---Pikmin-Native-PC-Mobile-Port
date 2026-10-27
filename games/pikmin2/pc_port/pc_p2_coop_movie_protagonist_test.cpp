/**
 * @file pc_p2_coop_movie_protagonist_test.cpp
 * @brief Protagonist tag queue, per-slot binding, and stale-tag clearing.
 */

#include "pc_p2_coop.h"
#include "pc_p2_coop_movie.h"

#include <cstdio>

namespace {

int sFailures = 0;

void expect_ptr(void* actual, void* expected, const char* what)
{
	if (actual != expected) {
		std::printf("FAIL: %s -> got %p, expected %p\n", what, actual, expected);
		++sFailures;
	}
}

} // namespace

int main()
{
	char olimar;
	char louie;
	char unrelated;

	expect_ptr(pc_p2_coop_take_movie_protagonist(), nullptr, "empty queue");

	pc_p2_coop_set_movie_protagonist(&louie);
	expect_ptr(pc_p2_coop_take_movie_protagonist(), &louie, "dequeue louie");

	expect_ptr(pc_p2_coop_take_movie_protagonist(), nullptr, "consumed once");

	pc_p2_coop_set_movie_protagonist(&olimar);
	pc_p2_coop_set_movie_protagonist(&louie);
	expect_ptr(pc_p2_coop_take_movie_protagonist(), &louie, "last writer wins");

	// Queued movies: each play() consumes its tag into a context slot (simulated).
	pc_p2_coop_set_movie_protagonist(&olimar);
	void* movieA = pc_p2_coop_take_movie_protagonist();
	pc_p2_coop_set_movie_protagonist(&louie);
	void* movieB = pc_p2_coop_take_movie_protagonist();
	void* slotA  = pc_p2_coop_movie_queue_slot_protagonist(movieA, &unrelated);
	void* slotB  = pc_p2_coop_movie_queue_slot_protagonist(movieB, &unrelated);
	expect_ptr(slotA, &olimar, "queue slot A keeps Olimar tag");
	expect_ptr(slotB, &louie, "queue slot B keeps Louie tag");
	expect_ptr(pc_p2_coop_take_movie_protagonist(), nullptr, "tags consumed at enqueue");

	// Issue 188: the meet target is a Pikmin, so split-screen mTargetNavi stays
	// Olimar. The whistle tag has to win over that fallback.
	expect_ptr(pc_p2_coop_movie_queue_slot_protagonist(&louie, &olimar), &louie,
	           "whistle tag beats split-screen Olimar");

	// A demo queued with no tag must stay unresolved. Filling it with the
	// captain of the movie already playing would aim the later cutscene at
	// the wrong captain once that movie's targets are gone.
	expect_ptr(pc_p2_coop_movie_queue_slot_protagonist(nullptr, nullptr), nullptr,
	           "queued demo with no tag stays unresolved");
	expect_ptr(pc_p2_coop_movie_queue_slot_protagonist(nullptr, &louie), &louie,
	           "playback resolves this demo's own target");

	// Stale global tag must not leak after clear (e.g. section reset).
	pc_p2_coop_set_movie_protagonist(&louie);
	pc_p2_coop_clear_movie_protagonist();
	expect_ptr(pc_p2_coop_take_movie_protagonist(), nullptr, "clear drops stale tag");

	// Fallback when no tag was set for this movie.
	expect_ptr(pc_p2_coop_movie_queue_slot_protagonist(nullptr, &olimar), &olimar, "slot uses fallback navi");

	if (sFailures == 0) {
		std::printf("pc_p2_coop_movie_protagonist_test: all checks passed\n");
		return 0;
	}
	std::printf("pc_p2_coop_movie_protagonist_test: %d failure(s)\n", sFailures);
	return 1;
}
