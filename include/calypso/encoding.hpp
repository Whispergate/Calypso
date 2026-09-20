#pragma once
#include "config.hpp"
#include <vector>
#include <cstdint>
#include <string>

namespace calypso {

std::string encode_base64(const std::vector<uint8_t>& data);
std::string encode_hex(const std::vector<uint8_t>& data);
std::string encode_mac(const std::vector<uint8_t>& data);
std::string encode_uuid(const std::vector<uint8_t>& data);

std::string encode_payload(const std::vector<uint8_t>& data, EncodingMethod method);

std::vector<uint8_t> generate_entropy_mask(size_t length, uint32_t seed);

std::string format_as_cpp_array(const std::vector<uint8_t>& data, const std::string& name);
std::string format_as_cpp_string_array(const std::vector<std::string>& items,
                                        const std::string& name);

} // namespace calypso
