#pragma once
#include "BSP.h"
#include <string>

namespace neoquake {
	bool ParseBSPEntities(const uint8_t* lumpData, size_t size, std::vector<BSPEntity>& out);
}
