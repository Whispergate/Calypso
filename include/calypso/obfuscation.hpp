#pragma once
#include <string>
#include <cstdint>
#include <vector>

namespace calypso {

std::string generate_random_varname(uint32_t& seed);
std::string generate_junk_function(uint32_t& seed);
std::string generate_opaque_predicate(uint32_t& seed, const std::string& body);
std::string flatten_control_flow(const std::vector<std::string>& blocks, uint32_t& seed);

} // namespace calypso
