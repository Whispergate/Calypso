#include "calypso/cli.hpp"
#include <iostream>
#include <string>
#include <sstream>
#include <random>
#include <algorithm>

namespace calypso {

void print_banner() {
    std::cout << R"(
   ____      _
  / ___|__ _| |_   _ _ __  ___  ___
 | |   / _` | | | | | '_ \/ __|/ _ \
 | |__| (_| | | |_| | |_) \__ \ (_) |
  \____\__,_|_|\__, | .__/|___/\___/
               |___/|_|
  C++23 Syscall Packer - v1.0
  By @Whispergate, @Lavender-exe
)" << std::endl;
}

void print_usage() {
    std::cout << R"(Usage: Calypso.exe --file <payload> [options]

Payload:
  --file <path>           Input payload file (required)
  --type <type>           Force type: shellcode | pe | csharp (auto-detect)
  --peinject              Convert PE to shellcode via donut before packing
  --arguments <args>      Hardcode arguments into the loader

Encryption:
  --key <key>             Encryption key (random 32-char if omitted)
  --cipher <cipher>       aes-ecb (default) | aes-cbc | xor | rc4
  --crypto-backend <be>   cng (default) | tiny-aes

Encoding:
  --encode <method>       none (default) | base64 | hex | mac | uuid

Compression:
  --compress <method>     none (default) | zlib | lz4 | rle

Obfuscation:
  --obfuscate             Enable source-level obfuscation
  --llvm-obfuscate        Use Obfuscator-LLVM for IR-level obfuscation
  --obf-seed <seed>       Custom seed for obfuscation randomness

Injection:
  --inject <method>       local (default) | remote
  --execute <primitive>   direct (default) | thread | apc | callback | fiber
  --process <name>        Target for remote inject (default: RuntimeBroker.exe)
  --ppid <name>           Parent process name for PPID spoofing
  --block-dlls            Block non-Microsoft DLLs in spawned process
  --module-stomp          Use module stomping for shellcode allocation
  --drip                  Drip-load shellcode in small chunks

Syscalls:
  --syscall <method>      indirect (default) | hellsgate | halosgate

Evasion:
  --sleep <seconds>       Sleep before decryption (default: 0)
  --amsi <methods>        AMSI bypass methods (comma-separated, default: hwbp)
                          hwbp, provider-deregister, context-corrupt,
                          ntcreatesection-hook, clr-patch, patch, none
  --etw <methods>         ETW bypass methods (comma-separated, default: hwbp)
                          hwbp, provider-disable, complus, unhook-ntdll,
                          patch-etwwrite, patch-nttrace, callback-remove, none
  --no-antidebug          Skip anti-debug checks
  --sandbox <checks>      domain,diskspace,memory,emulated (comma-separated)
  --self-delete           Loader deletes itself after execution
  --unhook <dlls>         Unhook DLLs via fresh copy (comma-separated)

Output:
  --output <path>         Output file (random name if omitted)
  --dll                   Build as DLL
  --dll-export <names>    Comma-separated DLL export function names
  --hide                  Build as GUI app (no console window)
  --source-only           Emit generated source, don't compile
  --verbose               Loader prints debug output
  --entropy-reduce        Reduce payload entropy to evade static analysis
)" << std::endl;
}

static std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> result;
    std::istringstream iss(s);
    std::string token;
    while (std::getline(iss, token, ',')) {
        // trim whitespace
        auto start = token.find_first_not_of(" \t");
        auto end = token.find_last_not_of(" \t");
        if (start != std::string::npos)
            result.push_back(token.substr(start, end - start + 1));
    }
    return result;
}

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

static std::string generate_random_key(size_t length = 32) {
    static const char charset[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, sizeof(charset) - 2);
    std::string key(length, ' ');
    for (auto& c : key) c = charset[dist(gen)];
    return key;
}

static std::string generate_random_name() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, 15);
    std::string name;
    for (int i = 0; i < 8; i++) {
        int v = dist(gen);
        name += "0123456789abcdef"[v];
    }
    return name;
}

static AmsiBypass parse_amsi_method(const std::string& m) {
    if (m == "hwbp")                  return AmsiBypass::HWBP;
    if (m == "provider-deregister")   return AmsiBypass::ProviderDeregister;
    if (m == "context-corrupt")       return AmsiBypass::ContextCorrupt;
    if (m == "ntcreatesection-hook")  return AmsiBypass::NtCreateSectionHook;
    if (m == "clr-patch")             return AmsiBypass::CLRPatch;
    if (m == "patch")                 return AmsiBypass::Patch;
    if (m == "none")                  return AmsiBypass::None;
    std::cerr << "[!] Unknown AMSI bypass method: " << m << std::endl;
    return AmsiBypass::None;
}

static EtwBypass parse_etw_method(const std::string& m) {
    if (m == "hwbp")             return EtwBypass::HWBP;
    if (m == "provider-disable") return EtwBypass::ProviderDisable;
    if (m == "complus")          return EtwBypass::ComPlus;
    if (m == "unhook-ntdll")     return EtwBypass::UnhookNtdll;
    if (m == "patch-etwwrite")   return EtwBypass::PatchEtwWrite;
    if (m == "patch-nttrace")    return EtwBypass::PatchNtTrace;
    if (m == "callback-remove")  return EtwBypass::CallbackRemove;
    if (m == "none")             return EtwBypass::None;
    std::cerr << "[!] Unknown ETW bypass method: " << m << std::endl;
    return EtwBypass::None;
}

PackerConfig parse_args(int argc, char* argv[]) {
    PackerConfig cfg;

    auto get_next = [&](int& i) -> std::string {
        if (i + 1 >= argc) {
            std::cerr << "[!] Missing argument for " << argv[i] << std::endl;
            std::exit(1);
        }
        return argv[++i];
    };

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--file") {
            cfg.input_file = get_next(i);
        } else if (arg == "--type") {
            auto t = to_lower(get_next(i));
            if (t == "shellcode")     cfg.payload_type = PayloadType::Shellcode;
            else if (t == "pe")       cfg.payload_type = PayloadType::PE;
            else if (t == "csharp")   cfg.payload_type = PayloadType::CSharp;
            else { std::cerr << "[!] Unknown type: " << t << "\n"; std::exit(1); }
        } else if (arg == "--peinject") {
            cfg.pe_to_shellcode = true;
        } else if (arg == "--arguments") {
            cfg.arguments = get_next(i);
        } else if (arg == "--key") {
            cfg.key = get_next(i);
        } else if (arg == "--cipher") {
            auto c = to_lower(get_next(i));
            if (c == "aes-ecb")       cfg.cipher = CipherMode::AES_ECB;
            else if (c == "aes-cbc")  cfg.cipher = CipherMode::AES_CBC;
            else if (c == "xor")      cfg.cipher = CipherMode::XOR;
            else if (c == "rc4")      cfg.cipher = CipherMode::RC4;
            else { std::cerr << "[!] Unknown cipher: " << c << "\n"; std::exit(1); }
        } else if (arg == "--crypto-backend") {
            auto b = to_lower(get_next(i));
            if (b == "cng")           cfg.crypto_backend = CryptoBackend::CNG;
            else if (b == "tiny-aes") cfg.crypto_backend = CryptoBackend::TinyAES;
            else { std::cerr << "[!] Unknown backend: " << b << "\n"; std::exit(1); }
        } else if (arg == "--encode") {
            auto e = to_lower(get_next(i));
            if (e == "none")          cfg.encoding = EncodingMethod::None;
            else if (e == "base64")   cfg.encoding = EncodingMethod::Base64;
            else if (e == "hex")      cfg.encoding = EncodingMethod::Hex;
            else if (e == "mac")      cfg.encoding = EncodingMethod::MAC;
            else if (e == "uuid")     cfg.encoding = EncodingMethod::UUID;
            else { std::cerr << "[!] Unknown encoding: " << e << "\n"; std::exit(1); }
        } else if (arg == "--compress") {
            auto c = to_lower(get_next(i));
            if (c == "none")          cfg.compression = CompressionMethod::None;
            else if (c == "zlib")     cfg.compression = CompressionMethod::Zlib;
            else if (c == "lz4")      cfg.compression = CompressionMethod::LZ4;
            else if (c == "rle")      cfg.compression = CompressionMethod::RLE;
            else { std::cerr << "[!] Unknown compression: " << c << "\n"; std::exit(1); }
        } else if (arg == "--obfuscate") {
            cfg.obfuscate = true;
        } else if (arg == "--llvm-obfuscate") {
            cfg.llvm_obfuscate = true;
        } else if (arg == "--obf-seed") {
            cfg.obf_seed = static_cast<uint32_t>(std::stoul(get_next(i)));
        } else if (arg == "--inject") {
            auto m = to_lower(get_next(i));
            if (m == "local")         cfg.injection = InjectionMethod::Local;
            else if (m == "remote")   cfg.injection = InjectionMethod::Remote;
            else { std::cerr << "[!] Unknown inject: " << m << "\n"; std::exit(1); }
        } else if (arg == "--execute") {
            auto p = to_lower(get_next(i));
            if (p == "direct")        cfg.exec_prim = ExecutionPrimitive::Direct;
            else if (p == "thread")   cfg.exec_prim = ExecutionPrimitive::Thread;
            else if (p == "apc")      cfg.exec_prim = ExecutionPrimitive::APC;
            else if (p == "callback") cfg.exec_prim = ExecutionPrimitive::Callback;
            else if (p == "fiber")    cfg.exec_prim = ExecutionPrimitive::Fiber;
            else { std::cerr << "[!] Unknown execute: " << p << "\n"; std::exit(1); }
        } else if (arg == "--process") {
            cfg.target_process = get_next(i);
        } else if (arg == "--ppid") {
            cfg.ppid_process = get_next(i);
        } else if (arg == "--block-dlls") {
            cfg.block_dlls = true;
        } else if (arg == "--module-stomp") {
            cfg.module_stomp = true;
        } else if (arg == "--drip") {
            cfg.drip_load = true;
        } else if (arg == "--syscall") {
            auto s = to_lower(get_next(i));
            if (s == "indirect")        cfg.syscall_method = SyscallMethod::Indirect;
            else if (s == "hellsgate")  cfg.syscall_method = SyscallMethod::HellsGate;
            else if (s == "halosgate")  cfg.syscall_method = SyscallMethod::HalosGate;
            else { std::cerr << "[!] Unknown syscall: " << s << "\n"; std::exit(1); }
        } else if (arg == "--sleep") {
            cfg.sleep_seconds = static_cast<uint32_t>(std::stoul(get_next(i)));
        } else if (arg == "--amsi") {
            auto methods = split_csv(get_next(i));
            cfg.amsi_bypass = AmsiBypass::None;
            for (auto& m : methods)
                cfg.amsi_bypass |= parse_amsi_method(to_lower(m));
        } else if (arg == "--etw") {
            auto methods = split_csv(get_next(i));
            cfg.etw_bypass = EtwBypass::None;
            for (auto& m : methods)
                cfg.etw_bypass |= parse_etw_method(to_lower(m));
        } else if (arg == "--no-antidebug") {
            cfg.anti_debug = false;
        } else if (arg == "--sandbox") {
            auto checks = split_csv(get_next(i));
            for (auto& c : checks) {
                auto cl = to_lower(c);
                if (cl == "domain")        cfg.sandbox.domain = true;
                else if (cl == "diskspace") cfg.sandbox.diskspace = true;
                else if (cl == "memory")    cfg.sandbox.memory = true;
                else if (cl == "emulated")  cfg.sandbox.emulated = true;
            }
        } else if (arg == "--self-delete") {
            cfg.self_delete = true;
        } else if (arg == "--unhook") {
            cfg.unhook_dlls = split_csv(get_next(i));
        } else if (arg == "--output") {
            cfg.output_file = get_next(i);
        } else if (arg == "--dll") {
            cfg.output_format = OutputFormat::DLL;
        } else if (arg == "--dll-export") {
            cfg.dll_exports = split_csv(get_next(i));
        } else if (arg == "--hide") {
            cfg.hide_window = true;
        } else if (arg == "--source-only") {
            cfg.source_only = true;
        } else if (arg == "--verbose") {
            cfg.verbose = true;
        } else if (arg == "--entropy-reduce") {
            cfg.entropy_reduce = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            std::exit(0);
        } else {
            std::cerr << "[!] Unknown argument: " << arg << std::endl;
            print_usage();
            std::exit(1);
        }
    }

    if (cfg.input_file.empty()) {
        std::cerr << "[!] --file is required\n";
        print_usage();
        std::exit(1);
    }

    if (cfg.key.empty())
        cfg.key = generate_random_key();

    if (cfg.output_file.empty()) {
        cfg.output_file = generate_random_name();
        cfg.output_file += (cfg.output_format == OutputFormat::DLL) ? ".dll" : ".exe";
    }

    if (cfg.obf_seed == 0) {
        std::random_device rd;
        cfg.obf_seed = rd();
    }

    return cfg;
}

} // namespace calypso
