#include "pc_p2_coop.h"

namespace {
void* sMovieProtagonist = nullptr;
} // namespace

extern "C" void pc_p2_coop_set_movie_protagonist(void* navi) { sMovieProtagonist = navi; }

extern "C" void* pc_p2_coop_take_movie_protagonist(void)
{
	void* navi = sMovieProtagonist;
	sMovieProtagonist = nullptr;
	return navi;
}

extern "C" void pc_p2_coop_clear_movie_protagonist(void) { sMovieProtagonist = nullptr; }

void* pc_p2_coop_movie_queue_slot_protagonist(void* taggedProtagonist, void* resolvedFallback)
{
	if (taggedProtagonist) {
		return taggedProtagonist;
	}
	return resolvedFallback;
}
