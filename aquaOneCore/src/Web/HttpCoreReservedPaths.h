#pragma once

#include <string.h>

namespace AquaCore {
namespace Web {
namespace Internal {

inline bool isCoreReadPath(const char* path) {
    return path != nullptr &&
        (strcmp(path, "/") == 0 ||
         strcmp(path, "/assets/aqua.css") == 0 ||
         strcmp(path, "/api/system") == 0 ||
         strcmp(path, "/api/diagnostics") == 0);
}

} // namespace Internal
} // namespace Web
} // namespace AquaCore
