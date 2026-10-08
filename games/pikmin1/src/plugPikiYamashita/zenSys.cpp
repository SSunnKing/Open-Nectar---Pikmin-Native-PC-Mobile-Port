#include "DebugLog.h"
#include "zen/ogSub.h"

/**
 * @todo: Documentation
 * @note UNUSED Size: 00009C
 */
DEFINE_ERROR(__LINE__) // Never used in the DLL

/**
 * @todo: Documentation
 * @note UNUSED Size: 0000F0
 */
DEFINE_PRINT(nullptr);

/**
 * @todo: Documentation
 */
void zen::makePathName(const char* directoryPath, const char* fileName, char* outputPath)
{
	if (!outputPath) {
		return;
	}
	const char* directory = directoryPath ? directoryPath : "";
	const char* name      = fileName ? fileName : "";
	const char* slash     = strrchr(name, '/');
	const char* basename  = slash ? slash + 1 : name;
	snprintf(outputPath, PATH_MAX, "%s%s", directory, basename);
}

/**
 * @todo: Documentation
 */
Texture* zen::loadTexExp(const char* textureName, bool useDirectory, bool makePath)
{
	if (useDirectory) {
		char fullPath[PATH_MAX];
		const char* dirPath = gsys->mTexDir;
		if (makePath) {
			zen::makePathName(dirPath, textureName, fullPath);
		} else {
			snprintf(fullPath, sizeof(fullPath), "%s%s", dirPath, textureName);
		}

		return gsys->loadTexture(fullPath, true);
	}

	return gsys->loadTexture((immut char*)textureName, true);
}
