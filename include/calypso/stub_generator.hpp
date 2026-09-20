#pragma once
#include "config.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace calypso {

struct GeneratedStub {
    std::string source;
    std::string output_path;
};

GeneratedStub generate_loader(const PackerConfig& cfg,
                               const std::vector<uint8_t>& encrypted_payload,
                               const std::vector<uint8_t>& key,
                               size_t original_size,
                               const std::string& stubs_dir);

} // namespace calypso
