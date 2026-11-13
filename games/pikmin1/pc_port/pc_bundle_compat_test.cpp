#include "pc_bundle_compat.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static void check(bool value, const char* message)
{
	if (!value) {
		std::fprintf(stderr, "FAIL: %s\n", message);
		std::exit(1);
	}
}

int main()
{
	char logoPath[] = "cinemas/titles/logoMotioe/logo.dck";
	check(pcCanonicalizeBundleAnimationPath(logoPath), "logo typo path is rewritten");
	check(std::strcmp(logoPath, "cinemas/titles/logoMotion/logo.dck") == 0, "logo path becomes logoMotion");

	char ordinaryPath[] = "cinemas/demo01/motion/d1_ufo.dck";
	check(!pcCanonicalizeBundleAnimationPath(ordinaryPath), "ordinary path is left alone");
	check(std::strcmp(ordinaryPath, "cinemas/demo01/motion/d1_ufo.dck") == 0, "ordinary path text unchanged");
	check(!pcCanonicalizeBundleAnimationPath(nullptr), "null path is rejected");

	std::puts("bundle animation paths canonicalized");
	return 0;
}
