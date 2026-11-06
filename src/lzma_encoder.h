#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sc2csv {

struct LzmaPacked {
    bool ok = false;
    // The 5 property bytes (properties byte + dictionary size), as the container header stores them.
    std::uint8_t properties[5] = {};
    std::vector<std::uint8_t> payload;
    std::string error;
};

// Packs data as a raw LZMA1 stream without an end marker, which is what Supercell's CSV containers hold.
// The caller writes properties, the size, then the payload.
LzmaPacked LzmaPack(std::span<const std::uint8_t> data);

} // namespace sc2csv
