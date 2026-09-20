#include "calypso/payload.hpp"
#include "calypso/pe_parser.hpp"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <filesystem>

namespace calypso {

Payload read_payload(const std::string& path, PayloadType type_override) {
    Payload payload;
    payload.original_path = path;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[!] Cannot open payload file: " << path << std::endl;
        std::exit(1);
    }

    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    payload.data.resize(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(payload.data.data()), size);

    if (payload.data.empty()) {
        std::cerr << "[!] Payload file is empty: " << path << std::endl;
        std::exit(1);
    }

    if (type_override != PayloadType::Auto) {
        payload.type = type_override;
    } else {
        if (payload.data.size() >= 2 &&
            payload.data[0] == 'M' && payload.data[1] == 'Z') {
            auto pe_info = parse_pe(payload.data);
            if (pe_info.valid && pe_info.is_dotnet)
                payload.type = PayloadType::CSharp;
            else
                payload.type = PayloadType::PE;
        } else {
            payload.type = PayloadType::Shellcode;
        }
    }

    return payload;
}

static std::string find_donut() {
    namespace fs = std::filesystem;

    std::string exe_name = "donut";
#ifdef _WIN32
    exe_name = "donut.exe";
#endif

    // Search order: tools/donut, tools/donut_ollvm/donut, then PATH
    std::vector<std::string> search_paths = {
        "tools/" + exe_name,
        "tools/donut_ollvm/" + exe_name,
    };

    // Also try relative to the running executable
    auto self = fs::path("/proc/self/exe");
    if (fs::exists(self)) {
        auto exe_dir = fs::read_symlink(self).parent_path();
        search_paths.push_back((exe_dir / "tools" / exe_name).string());
        search_paths.push_back((exe_dir / "tools" / "donut_ollvm" / exe_name).string());
        search_paths.push_back((exe_dir / ".." / "tools" / exe_name).string());
    }

    for (const auto& p : search_paths) {
        if (fs::exists(p))
            return p;
    }

    // Fall back to PATH
    return exe_name;
}

bool convert_pe_to_shellcode(const std::string& pe_path, std::vector<uint8_t>& out) {
    namespace fs = std::filesystem;

    std::string donut_path = find_donut();
    std::string out_file = pe_path + ".bin";

    // donut_ollvm flags:
    //   -f 1  = raw binary output
    //   -a 2  = x64 architecture
    //   -b 3  = AMSI/WLDP/ETW bypass (continue on failure)
    //   -i    = input file
    //   -o    = output file
    std::string cmd = donut_path + " -f 1 -a 2 -b 3"
                      " -i \"" + pe_path + "\""
                      " -o \"" + out_file + "\"";

    std::cout << "[*] Running donut: " << cmd << std::endl;
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        std::cerr << "[!] Donut failed (exit " << ret << ").\n"
                  << "    Ensure donut is in tools/ or PATH.\n"
                  << "    Build from donut_ollvm: cd donut_ollvm && make\n";
        return false;
    }

    std::ifstream f(out_file, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        std::cerr << "[!] Donut output not found: " << out_file << std::endl;
        return false;
    }

    auto size = f.tellg();
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(out.data()), size);

    fs::remove(out_file);
    return true;
}

} // namespace calypso
