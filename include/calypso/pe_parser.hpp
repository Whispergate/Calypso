#pragma once
#include <cstdint>
#include <vector>

namespace calypso {

struct PEInfo {
    bool valid          = false;
    bool is_64bit       = false;
    bool is_dotnet      = false;
    uint32_t entry_rva  = 0;
    uint64_t image_base = 0;
};

PEInfo parse_pe(const std::vector<uint8_t>& data);

} // namespace calypso
