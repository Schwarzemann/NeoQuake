#include "BSP_Entities.h"
#include <cctype>

namespace neoquake {

    // Very small Quake-entity parser: { "key" "value" ... }
    bool ParseBSPEntities(const uint8_t* p, size_t n, std::vector<BSPEntity>& out) {
        if (!p || n == 0) return false;
        size_t i = 0;
        auto skipWS = [&]() { while (i < n && std::isspace((unsigned char)p[i])) ++i; };

        skipWS();
        while (i < n) {
            skipWS();
            if (i >= n || p[i] != '{') break;
            ++i; // consume '{'
            BSPEntity e;
            for (;;) {
                skipWS();
                if (i < n && p[i] == '}') { ++i; break; }
                if (i >= n || p[i] != '\"') break;
                // key
                ++i; size_t k0 = i; while (i < n && p[i] != '\"') ++i; if (i >= n) break;
                std::string key((const char*)p + k0, (const char*)p + i);
                ++i; skipWS();
                if (i >= n || p[i] != '\"') break;
                ++i; size_t v0 = i; while (i < n && p[i] != '\"') ++i; if (i >= n) break;
                std::string val((const char*)p + v0, (const char*)p + i);
                ++i;
                e.kv.push_back({ std::move(key), std::move(val) });
            }
            if (!e.kv.empty()) out.push_back(std::move(e));
            skipWS();
        }
        return !out.empty();
    }

} // namespace neoquake
