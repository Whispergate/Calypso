#include "calypso/compiler.hpp"
#include <iostream>
#include <cstdlib>
#include <array>
#include <memory>
#include <filesystem>

namespace calypso {

static bool command_exists(const std::string& cmd) {
#ifdef _WIN32
    std::string check = "where " + cmd + " >nul 2>nul";
#else
    std::string check = "which " + cmd + " >/dev/null 2>&1";
#endif
    return std::system(check.c_str()) == 0;
}

static bool is_ollvm(const std::string& clang_path) {
#ifdef _WIN32
    std::string check = clang_path + " -mllvm -fla --version >nul 2>nul";
#else
    std::string check = clang_path + " -mllvm -fla --version >/dev/null 2>&1";
#endif
    return std::system(check.c_str()) == 0;
}

DetectedCompiler detect_compiler(bool prefer_llvm) {
    if (prefer_llvm) {
        for (const auto& name : {"ollvm-clang++", "clang++-ollvm", "clang++"}) {
            if (command_exists(name)) {
                if (is_ollvm(name)) {
                    return {CompilerType::LLVM, name};
                }
                std::cerr << "[!] Found " << name << " but it does not support "
                          << "Obfuscator-LLVM passes (-fla/-sub/-bcf).\n"
                          << "[!] Compiling with standard clang++ (no IR obfuscation).\n";
                return {CompilerType::LLVM, name};
            }
        }
        std::cerr << "[!] --llvm-obfuscate requires clang++ in PATH\n";
    }

#ifdef _WIN32
    if (command_exists("cl.exe"))
        return {CompilerType::MSVC, "cl.exe"};
    if (command_exists("g++.exe"))
        return {CompilerType::MinGW, "g++.exe"};
    if (command_exists("clang++.exe"))
        return {CompilerType::LLVM, "clang++.exe"};
#else
    // On Linux, look for cross-compiler first (we're building Windows loaders)
    if (command_exists("x86_64-w64-mingw32-g++"))
        return {CompilerType::MinGWCross, "x86_64-w64-mingw32-g++"};
    if (command_exists("i686-w64-mingw32-g++"))
        return {CompilerType::MinGWCross, "i686-w64-mingw32-g++"};
    // Native compilers (only useful with --source-only on Linux)
    if (command_exists("g++"))
        return {CompilerType::MinGW, "g++"};
    if (command_exists("clang++"))
        return {CompilerType::LLVM, "clang++"};
#endif

    return {CompilerType::None, ""};
}

DetectedCompiler detect_cross_compiler() {
    if (command_exists("x86_64-w64-mingw32-g++"))
        return {CompilerType::MinGWCross, "x86_64-w64-mingw32-g++"};
    if (command_exists("i686-w64-mingw32-g++"))
        return {CompilerType::MinGWCross, "i686-w64-mingw32-g++"};
    return {CompilerType::None, ""};
}

bool compile_loader(const PackerConfig& cfg, const std::string& source_path,
                    const std::string& output_path, const DetectedCompiler& compiler) {
    if (compiler.type == CompilerType::None) {
        std::cerr << "[!] No C++ compiler found. "
#ifdef _WIN32
                  << "Install MSVC or MinGW.\n";
#else
                  << "Install mingw-w64 (apt install mingw-w64) for cross-compilation.\n";
#endif
        return false;
    }

    std::string cmd;

    switch (compiler.type) {
        case CompilerType::MSVC: {
            cmd = compiler.path +
                  " /std:c++latest /EHsc /O2 /GS- /MT /W0 /DNOMINMAX"
                  " \"" + source_path + "\"";

            if (cfg.output_format == OutputFormat::DLL)
                cmd += " /LD";

            cmd += " /Fe:\"" + output_path + "\"";
            cmd += " /link /SUBSYSTEM:";
            cmd += cfg.hide_window ? "WINDOWS" : "CONSOLE";

            if (cfg.crypto_backend == CryptoBackend::CNG)
                cmd += " bcrypt.lib";

            cmd += " advapi32.lib ole32.lib oleaut32.lib";
            break;
        }

        case CompilerType::MinGW:
        case CompilerType::MinGWCross: {
            cmd = compiler.path + " -std=c++23 -O2 -s -w -static -DNOMINMAX"
                  " \"" + source_path + "\"";

            if (cfg.output_format == OutputFormat::DLL)
                cmd += " -shared";

            if (cfg.hide_window)
                cmd += " -mwindows";

            cmd += " -o \"" + output_path + "\"";

            if (cfg.crypto_backend == CryptoBackend::CNG)
                cmd += " -lbcrypt";

            cmd += " -ladvapi32 -lole32 -loleaut32";
            break;
        }

        case CompilerType::LLVM: {
            cmd = compiler.path + " -std=c++23 -O2 -w -DNOMINMAX";

            if (cfg.llvm_obfuscate && is_ollvm(compiler.path)) {
                cmd += " -mllvm -fla -mllvm -sub -mllvm -bcf -mllvm -bcf_prob=40";
                cmd += " -mllvm -split";
            }

            cmd += " \"" + source_path + "\"";

            if (cfg.output_format == OutputFormat::DLL)
                cmd += " -shared";

            cmd += " -o \"" + output_path + "\"";

            if (cfg.crypto_backend == CryptoBackend::CNG)
                cmd += " -lbcrypt";

            cmd += " -ladvapi32 -lole32 -loleaut32";
            break;
        }

        default:
            return false;
    }

    std::cout << "[*] Compiling: " << cmd << std::endl;
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        std::cerr << "[!] Compilation failed (exit " << ret << ")\n";
        return false;
    }

    return true;
}

} // namespace calypso
