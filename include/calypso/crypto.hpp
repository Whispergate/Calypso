#pragma once
#include "config.hpp"
#include <vector>
#include <cstdint>
#include <string>

namespace calypso {

std::vector<uint8_t> expand_key(const std::string& key, size_t target_len = 32);

std::vector<uint8_t> encrypt_aes_ecb(const std::vector<uint8_t>& data,
                                      const std::vector<uint8_t>& key);
std::vector<uint8_t> encrypt_aes_cbc(const std::vector<uint8_t>& data,
                                      const std::vector<uint8_t>& key);
std::vector<uint8_t> encrypt_xor(const std::vector<uint8_t>& data,
                                  const std::vector<uint8_t>& key);

std::vector<uint8_t> encrypt_payload(const std::vector<uint8_t>& data,
                                      const std::string& key,
                                      CipherMode cipher);

} // namespace calypso
