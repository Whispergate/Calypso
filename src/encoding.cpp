#include "calypso/encoding.hpp"
#include <sstream>
#include <iomanip>
#include <cstdio>
#include <random>

namespace calypso {

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encode_base64(const std::vector<uint8_t>& data) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i < data.size()) {
        uint32_t octet_a = data[i++];
        uint32_t octet_b = (i < data.size()) ? data[i++] : 0;
        uint32_t octet_c = (i < data.size()) ? data[i++] : 0;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
        out += b64_table[(triple >> 18) & 0x3F];
        out += b64_table[(triple >> 12) & 0x3F];
        out += (i > data.size() + 1) ? '=' : b64_table[(triple >> 6) & 0x3F];
        out += (i > data.size()) ? '=' : b64_table[triple & 0x3F];
    }
    return out;
}

std::string encode_hex(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    for (auto b : data)
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    return oss.str();
}

std::string encode_mac(const std::vector<uint8_t>& data) {
    // Pad to multiple of 6 bytes
    std::vector<uint8_t> padded = data;
    while (padded.size() % 6 != 0)
        padded.push_back(0);

    std::string out;
    for (size_t i = 0; i < padded.size(); i += 6) {
        if (!out.empty()) out += "\n";
        char buf[18];
        std::snprintf(buf, sizeof(buf), "%02X-%02X-%02X-%02X-%02X-%02X",
                      padded[i], padded[i+1], padded[i+2],
                      padded[i+3], padded[i+4], padded[i+5]);
        out += buf;
    }
    return out;
}

std::string encode_uuid(const std::vector<uint8_t>& data) {
    // Pad to multiple of 16 bytes
    std::vector<uint8_t> padded = data;
    while (padded.size() % 16 != 0)
        padded.push_back(0);

    std::string out;
    for (size_t i = 0; i < padded.size(); i += 16) {
        if (!out.empty()) out += "\n";
        char buf[48];
        std::snprintf(buf, sizeof(buf),
            "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            padded[i], padded[i+1], padded[i+2], padded[i+3],
            padded[i+4], padded[i+5], padded[i+6], padded[i+7],
            padded[i+8], padded[i+9], padded[i+10], padded[i+11],
            padded[i+12], padded[i+13], padded[i+14], padded[i+15]);
        out += buf;
    }
    return out;
}

std::string encode_payload(const std::vector<uint8_t>& data, EncodingMethod method) {
    switch (method) {
        case EncodingMethod::Base64: return encode_base64(data);
        case EncodingMethod::Hex:    return encode_hex(data);
        case EncodingMethod::MAC:    return encode_mac(data);
        case EncodingMethod::UUID:   return encode_uuid(data);
        case EncodingMethod::None:   return {};
    }
    return {};
}

std::vector<uint8_t> generate_entropy_mask(size_t length, uint32_t seed) {
    static const char english_chars[] =
        "etaoinshrdlcumwfgypbvkjxqz"
        "ETAOINSHRDLCUMWFGYPBVKJXQZ"
        "          "
        "0123456789"
        ".,;:!?-'\"()";
    constexpr size_t charset_len = sizeof(english_chars) - 1;

    std::mt19937 rng(seed);
    std::uniform_int_distribution<size_t> dist(0, charset_len - 1);

    std::vector<uint8_t> mask(length);
    for (size_t i = 0; i < length; i++)
        mask[i] = static_cast<uint8_t>(english_chars[dist(rng)]);
    return mask;
}

std::string format_as_cpp_array(const std::vector<uint8_t>& data, const std::string& name) {
    std::ostringstream oss;
    oss << "unsigned char " << name << "[] = {\n    ";
    for (size_t i = 0; i < data.size(); i++) {
        oss << "0x" << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(data[i]);
        if (i + 1 < data.size()) oss << ",";
        if ((i + 1) % 16 == 0 && i + 1 < data.size()) oss << "\n    ";
    }
    oss << "\n};\n";
    oss << "const size_t " << name << "_len = sizeof(" << name << ");\n";
    return oss.str();
}

std::string format_as_cpp_string_array(const std::vector<std::string>& items,
                                        const std::string& name) {
    std::ostringstream oss;
    oss << "const char* " << name << "[] = {\n";
    for (size_t i = 0; i < items.size(); i++) {
        oss << "    \"" << items[i] << "\"";
        if (i + 1 < items.size()) oss << ",";
        oss << "\n";
    }
    oss << "};\n";
    oss << "const size_t " << name << "_count = " << items.size() << ";\n";
    return oss.str();
}

} // namespace calypso
