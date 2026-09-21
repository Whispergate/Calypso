#include "calypso/compiler.hpp"
#include <iostream>
#include <fstream>
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

static const std::vector<std::string> OLLVM_PLUGIN_PATHS = {
    "/opt/llvm/libLLVMObfuscator.so",
    "/usr/lib/libLLVMObfuscator.so",
    "/usr/local/lib/libLLVMObfuscator.so",
};

static std::string find_ollvm_plugin(const std::string& user_path) {
    namespace fs = std::filesystem;
    if (!user_path.empty()) {
        if (fs::exists(user_path)) return user_path;
        std::cerr << "[!] OLLVM plugin not found at: " << user_path << "\n";
        return "";
    }
    for (const auto& p : OLLVM_PLUGIN_PATHS) {
        if (fs::exists(p)) return p;
    }
    return "";
}

static bool test_ollvm_plugin(const std::string& clang_path, const std::string& plugin_path) {
    namespace fs = std::filesystem;
    auto tmp_src = fs::temp_directory_path() / "calypso_ollvm_test.cpp";
    auto tmp_obj = fs::temp_directory_path() / "calypso_ollvm_test.o";
    {
        std::ofstream f(tmp_src);
        f << "int _test(){return 0;}\n";
    }
#ifdef _WIN32
    std::string check = clang_path + " -fpass-plugin=\"" + plugin_path +
                        "\" -c \"" + tmp_src.string() +
                        "\" -o \"" + tmp_obj.string() + "\" >nul 2>nul";
#else
    std::string check = clang_path + " -fpass-plugin=\"" + plugin_path +
                        "\" -c \"" + tmp_src.string() +
                        "\" -o \"" + tmp_obj.string() + "\" >/dev/null 2>&1";
#endif
    int ret = std::system(check.c_str());
    fs::remove(tmp_src);
    fs::remove(tmp_obj);
    return ret == 0;
}

DetectedCompiler detect_compiler(bool prefer_llvm, const std::string& user_plugin_path) {
    if (prefer_llvm) {
        auto plugin = find_ollvm_plugin(user_plugin_path);
        if (!plugin.empty()) {
            for (const auto& name : {"clang++-21", "clang++-20", "clang++-19",
                                      "clang++-18", "clang++"}) {
                if (command_exists(name) && test_ollvm_plugin(name, plugin)) {
                    return {CompilerType::LLVM, name, true, plugin};
                }
            }
            std::cerr << "[!] Found OLLVM plugin at " << plugin
                      << " but no compatible clang++ found.\n";
        } else {
            std::cerr << "[!] --llvm-obfuscate set but libLLVMObfuscator.so not found.\n"
                      << "[!] Install eshard/obfuscator-llvm or use --ollvm-plugin <path>.\n";
        }
        std::cerr << "[!] Falling back to standard compiler (no IR obfuscation).\n";
    }

#ifdef _WIN32
    if (command_exists("cl.exe"))
        return {CompilerType::MSVC, "cl.exe"};
    if (command_exists("g++.exe"))
        return {CompilerType::MinGW, "g++.exe"};
    if (command_exists("clang++.exe"))
        return {CompilerType::LLVM, "clang++.exe"};
#else
    if (command_exists("x86_64-w64-mingw32-g++"))
        return {CompilerType::MinGWCross, "x86_64-w64-mingw32-g++"};
    if (command_exists("i686-w64-mingw32-g++"))
        return {CompilerType::MinGWCross, "i686-w64-mingw32-g++"};
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
    std::string env_prefix;

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

#ifndef _WIN32
            cmd += " --target=x86_64-w64-mingw32 -static";
#endif

            if (cfg.llvm_obfuscate && compiler.supports_ollvm) {
                cmd += " -fpass-plugin=\"" + compiler.ollvm_plugin + "\"";
                env_prefix =
                    "LLVM_OBF_SCALAROPTIMIZERLATE_PASSES="
                    "flattening,bogus,substitution,split-basic-blocks "
                    "LLVM_OBF_OPTIMIZERLASTEP_PASSES=string-encryption ";
            }

            cmd += " \"" + source_path + "\"";

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

        default:
            return false;
    }

    std::string full_cmd = env_prefix + cmd;
    std::cout << "[*] Compiling: " << full_cmd << std::endl;
    int ret = std::system(full_cmd.c_str());
    if (ret != 0) {
        std::cerr << "[!] Compilation failed (exit " << ret << ")\n";
        return false;
    }

    return true;
}

} // namespace calypso
