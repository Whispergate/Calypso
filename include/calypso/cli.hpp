#pragma once
// Calypso - CLI argument parsing

#include "config.hpp"

namespace calypso {

void print_banner();
void print_usage();
PackerConfig parse_args(int argc, char* argv[]);

} // namespace calypso
