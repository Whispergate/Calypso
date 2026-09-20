#include "calypso/obfuscation.hpp"
#include <sstream>

namespace calypso {

static uint32_t xorshift(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

std::string generate_random_varname(uint32_t& seed) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyz";
    std::string name;
    name += charset[xorshift(seed) % 26];
    for (int i = 0; i < 7; i++)
        name += charset[xorshift(seed) % 26];
    return name;
}

std::string generate_junk_function(uint32_t& seed) {
    std::string fname = generate_random_varname(seed);
    std::string v1 = generate_random_varname(seed);
    std::string v2 = generate_random_varname(seed);
    std::string v3 = generate_random_varname(seed);

    uint32_t init1 = xorshift(seed) % 1000;
    uint32_t init2 = xorshift(seed) % 1000;

    std::ostringstream oss;
    oss << "__declspec(noinline) static volatile int " << fname << "() {\n"
        << "    volatile int " << v1 << " = " << init1 << ";\n"
        << "    volatile int " << v2 << " = " << init2 << ";\n"
        << "    volatile int " << v3 << " = 0;\n"
        << "    for (volatile int i = 0; i < " << (xorshift(seed) % 10 + 3) << "; i++) {\n"
        << "        " << v3 << " += (" << v1 << " * " << v2 << ") ^ (i + " << (xorshift(seed) % 100) << ");\n"
        << "        " << v1 << " = (" << v1 << " >> 1) | (" << v2 << " << 31);\n"
        << "        " << v2 << " ^= " << v3 << ";\n"
        << "    }\n"
        << "    return " << v3 << ";\n"
        << "}\n";
    return oss.str();
}

std::string generate_opaque_predicate(uint32_t& seed, const std::string& body) {
    std::string v = generate_random_varname(seed);
    uint32_t val = (xorshift(seed) % 50) * 2 + 1; // always odd

    std::ostringstream oss;
    oss << "{ volatile int " << v << " = " << val << ";\n"
        << "  if ((" << v << " * " << v << " - 1) % 8 == 0) {\n"
        << "    " << body << "\n"
        << "  } }\n";
    return oss.str();
}

std::string flatten_control_flow(const std::vector<std::string>& blocks, uint32_t& seed) {
    if (blocks.empty()) return "";

    // Shuffle block order
    std::vector<size_t> order(blocks.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = i;
    for (size_t i = order.size() - 1; i > 0; i--) {
        size_t j = xorshift(seed) % (i + 1);
        std::swap(order[i], order[j]);
    }

    // Build mapping: original index -> case label
    std::vector<int> case_labels(blocks.size());
    for (size_t i = 0; i < order.size(); i++)
        case_labels[order[i]] = static_cast<int>(i);

    std::string state_var = generate_random_varname(seed);

    std::ostringstream oss;
    oss << "{ volatile int " << state_var << " = " << case_labels[0] << ";\n"
        << "  while (" << state_var << " != -1) {\n"
        << "    switch (" << state_var << ") {\n";

    for (size_t i = 0; i < blocks.size(); i++) {
        int label = case_labels[i];
        int next = (i + 1 < blocks.size()) ? case_labels[i + 1] : -1;
        oss << "      case " << label << ":\n"
            << "        " << blocks[i] << "\n"
            << "        " << state_var << " = " << next << ";\n"
            << "        break;\n";
    }

    oss << "    }\n  }\n}\n";
    return oss.str();
}

} // namespace calypso
