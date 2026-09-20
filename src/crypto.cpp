#include "calypso/crypto.hpp"
#include <cstring>
#include <random>
#include <iomanip>
#include <sstream>
#include <iostream>

extern "C" {
#include "aes.h"
}

namespace calypso {

static std::string to_hex(const std::string& input) {
    std::ostringstream oss;
    for (unsigned char c : input)
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    return oss.str();
}

std::vector<uint8_t> expand_key(const std::string& key, size_t target_len) {
    // Match NimSyscallPacker: hex-encode the key, then pad/truncate to target_len
    std::string hex_key = to_hex(key);
    std::vector<uint8_t> expanded(target_len, 0);
    for (size_t i = 0; i < target_len && i < hex_key.size(); i++)
        expanded[i] = static_cast<uint8_t>(hex_key[i]);
    return expanded;
}

static std::vector<uint8_t> pkcs7_pad(const std::vector<uint8_t>& data, size_t block_size) {
    size_t pad_len = block_size - (data.size() % block_size);
    std::vector<uint8_t> padded = data;
    padded.insert(padded.end(), pad_len, static_cast<uint8_t>(pad_len));
    return padded;
}

std::vector<uint8_t> encrypt_aes_ecb(const std::vector<uint8_t>& data,
                                      const std::vector<uint8_t>& key) {
    if (key.size() != 32) {
        std::cerr << "[!] AES-256 requires 32-byte key\n";
        std::exit(1);
    }

    auto padded = pkcs7_pad(data, AES_BLOCKLEN);
    std::vector<uint8_t> output = padded;

    struct AES_ctx ctx;
    AES_init_ctx(&ctx, key.data());

    for (size_t i = 0; i < output.size(); i += AES_BLOCKLEN)
        AES_ECB_encrypt(&ctx, output.data() + i);

    return output;
}

std::vector<uint8_t> encrypt_aes_cbc(const std::vector<uint8_t>& data,
                                      const std::vector<uint8_t>& key) {
    if (key.size() != 32) {
        std::cerr << "[!] AES-256 requires 32-byte key\n";
        std::exit(1);
    }

    // Generate random IV
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    uint8_t iv[AES_BLOCKLEN];
    for (auto& b : iv) b = static_cast<uint8_t>(dist(gen));

    auto padded = pkcs7_pad(data, AES_BLOCKLEN);
    std::vector<uint8_t> output = padded;

    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key.data(), iv);
    AES_CBC_encrypt_buffer(&ctx, output.data(), output.size());

    // Prepend IV
    std::vector<uint8_t> result(iv, iv + AES_BLOCKLEN);
    result.insert(result.end(), output.begin(), output.end());
    return result;
}

std::vector<uint8_t> encrypt_xor(const std::vector<uint8_t>& data,
                                  const std::vector<uint8_t>& key) {
    std::vector<uint8_t> output = data;
    for (size_t i = 0; i < output.size(); i++)
        output[i] ^= key[i % key.size()];
    return output;
}

std::vector<uint8_t> encrypt_rc4(const std::vector<uint8_t>& data,
                                  const std::vector<uint8_t>& key) {
    uint8_t S[256];
    for (int i = 0; i < 256; i++) S[i] = static_cast<uint8_t>(i);
    uint8_t j = 0;
    for (int i = 0; i < 256; i++) {
        j = j + S[i] + key[i % key.size()];
        std::swap(S[i], S[j]);
    }
    std::vector<uint8_t> output(data.size());
    uint8_t ii = 0, jj = 0;
    for (size_t k = 0; k < data.size(); k++) {
        ii++;
        jj += S[ii];
        std::swap(S[ii], S[jj]);
        output[k] = data[k] ^ S[static_cast<uint8_t>(S[ii] + S[jj])];
    }
    return output;
}

std::vector<uint8_t> encrypt_payload(const std::vector<uint8_t>& data,
                                      const std::string& key,
                                      CipherMode cipher) {
    auto expanded = expand_key(key, 32);

    switch (cipher) {
        case CipherMode::AES_ECB: return encrypt_aes_ecb(data, expanded);
        case CipherMode::AES_CBC: return encrypt_aes_cbc(data, expanded);
        case CipherMode::XOR:     return encrypt_xor(data, expanded);
        case CipherMode::RC4:     return encrypt_rc4(data, expanded);
    }
    return {};
}

} // namespace calypso
