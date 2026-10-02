// SkyEngine PNG writer (libpng)
#pragma once
#include <cstdint>
#include <string>

namespace sky {

// rgb: w*h*3 bytes (top-left first). Returns false on failure.
bool write_png(const std::string& path, int w, int h, const uint8_t* rgb);

} // namespace sky
