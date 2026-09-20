#include "calypso/pe_parser.hpp"
#include <cstring>

namespace calypso {

// PE structures defined here to avoid Windows header dependency in the packer
#pragma pack(push, 1)
struct DosHeader {
    uint16_t e_magic;
    uint16_t e_cblp, e_cp, e_crlc, e_cparhdr, e_minalloc, e_maxalloc;
    uint16_t e_ss, e_sp, e_csum, e_ip, e_cs, e_lfarlc, e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid, e_oeminfo;
    uint16_t e_res2[10];
    int32_t  e_lfanew;
};

struct FileHeader {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
};

struct DataDirectory {
    uint32_t VirtualAddress;
    uint32_t Size;
};

struct OptionalHeader32 {
    uint16_t Magic;
    uint8_t  MajorLinkerVersion, MinorLinkerVersion;
    uint32_t SizeOfCode, SizeOfInitializedData, SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode, BaseOfData;
    uint32_t ImageBase;
    uint32_t SectionAlignment, FileAlignment;
    uint16_t MajorOperatingSystemVersion, MinorOperatingSystemVersion;
    uint16_t MajorImageVersion, MinorImageVersion;
    uint16_t MajorSubsystemVersion, MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage, SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem, DllCharacteristics;
    uint32_t SizeOfStackReserve, SizeOfStackCommit;
    uint32_t SizeOfHeapReserve, SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    DataDirectory DataDirectories[16];
};

struct OptionalHeader64 {
    uint16_t Magic;
    uint8_t  MajorLinkerVersion, MinorLinkerVersion;
    uint32_t SizeOfCode, SizeOfInitializedData, SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint64_t ImageBase;
    uint32_t SectionAlignment, FileAlignment;
    uint16_t MajorOperatingSystemVersion, MinorOperatingSystemVersion;
    uint16_t MajorImageVersion, MinorImageVersion;
    uint16_t MajorSubsystemVersion, MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage, SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem, DllCharacteristics;
    uint64_t SizeOfStackReserve, SizeOfStackCommit;
    uint64_t SizeOfHeapReserve, SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    DataDirectory DataDirectories[16];
};
#pragma pack(pop)

static constexpr uint16_t IMAGE_DOS_SIGNATURE    = 0x5A4D;
static constexpr uint32_t IMAGE_NT_SIGNATURE     = 0x00004550;
static constexpr uint16_t IMAGE_FILE_MACHINE_AMD64 = 0x8664;
static constexpr uint16_t PE32_MAGIC  = 0x10b;
static constexpr uint16_t PE32P_MAGIC = 0x20b;
static constexpr int IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR = 14;

PEInfo parse_pe(const std::vector<uint8_t>& data) {
    PEInfo info;

    if (data.size() < sizeof(DosHeader))
        return info;

    DosHeader dos;
    std::memcpy(&dos, data.data(), sizeof(dos));
    if (dos.e_magic != IMAGE_DOS_SIGNATURE)
        return info;

    auto nt_offset = static_cast<size_t>(dos.e_lfanew);
    if (nt_offset + 4 + sizeof(FileHeader) > data.size())
        return info;

    uint32_t nt_sig;
    std::memcpy(&nt_sig, data.data() + nt_offset, 4);
    if (nt_sig != IMAGE_NT_SIGNATURE)
        return info;

    FileHeader fh;
    std::memcpy(&fh, data.data() + nt_offset + 4, sizeof(fh));

    info.is_64bit = (fh.Machine == IMAGE_FILE_MACHINE_AMD64);

    auto opt_offset = nt_offset + 4 + sizeof(FileHeader);
    uint16_t opt_magic;
    if (opt_offset + 2 > data.size())
        return info;
    std::memcpy(&opt_magic, data.data() + opt_offset, 2);

    if (opt_magic == PE32P_MAGIC) {
        if (opt_offset + sizeof(OptionalHeader64) > data.size())
            return info;
        OptionalHeader64 opt;
        std::memcpy(&opt, data.data() + opt_offset, sizeof(opt));
        info.entry_rva = opt.AddressOfEntryPoint;
        info.image_base = opt.ImageBase;
        if (opt.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR) {
            auto& clr = opt.DataDirectories[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR];
            info.is_dotnet = (clr.VirtualAddress != 0 && clr.Size != 0);
        }
    } else if (opt_magic == PE32_MAGIC) {
        if (opt_offset + sizeof(OptionalHeader32) > data.size())
            return info;
        OptionalHeader32 opt;
        std::memcpy(&opt, data.data() + opt_offset, sizeof(opt));
        info.entry_rva = opt.AddressOfEntryPoint;
        info.image_base = opt.ImageBase;
        if (opt.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR) {
            auto& clr = opt.DataDirectories[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR];
            info.is_dotnet = (clr.VirtualAddress != 0 && clr.Size != 0);
        }
    } else {
        return info;
    }

    info.valid = true;
    return info;
}

} // namespace calypso
