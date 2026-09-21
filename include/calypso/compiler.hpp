#pragma once
#include "config.hpp"
#include <string>

namespace calypso {

enum class CompilerType { MSVC, MinGW, MinGWCross, LLVM, None };

struct DetectedCompiler {
    CompilerType type;
    std::string path;
    bool supports_ollvm = false;
    std::string ollvm_plugin;
};

DetectedCompiler detect_compiler(bool prefer_llvm = false,
                                 const std::string& ollvm_plugin = "");
DetectedCompiler detect_cross_compiler();
bool compile_loader(const PackerConfig& cfg, const std::string& source_path,
                    const std::string& output_path, const DetectedCompiler& compiler);

} // namespace calypso
