#pragma once
#include "config.hpp"
#include <vector>
#include <cstdint>
#include <string>

namespace calypso {

struct Payload {
    std::vector<uint8_t> data;
    PayloadType type;
    std::string original_path;
};

Payload read_payload(const std::string& path, PayloadType type_override);
bool convert_pe_to_shellcode(const std::string& pe_path, std::vector<uint8_t>& out);

} // namespace calypso
