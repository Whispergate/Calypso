# Calypso

```
   ____      _
  / ___|__ _| |_   _ _ __  ___  ___
 | |   / _` | | | | | '_ \/ __|/ _ \
 | |__| (_| | | |_| | |_) \__ \ (_) |
  \____\__,_|_|\__, | .__/|___/\___/
               |___/|_|
```

A C++23 syscall packer for authorized security testing. Takes PE files, C# assemblies, or raw shellcode and packs them into loader binaries that use direct/indirect syscalls for execution.

Written by **@Whispergate** and **@Lavender-exe**.

> **Disclaimer:** This tool is intended for authorized penetration testing and red team engagements only. Unauthorized use is illegal.

---

## Features

- **Payload types** - Shellcode, PE (Run-PE), C# assemblies (CLR hosting), PE-to-shellcode via Donut
- **Encryption** - AES-256-ECB, AES-256-CBC, XOR with two backends (Windows CNG or vendored tiny-AES-c)
- **Encoding** - Base64, hex, MAC-address format, UUID format
- **Compression** - Zlib (miniz) and LZ4
- **Syscall methods** - Hell's Gate, Halo's Gate, Indirect Syscalls
- **Injection** - Local (direct/thread/APC/callback) and Remote (with PPID spoofing, DLL blocking)
- **Evasion** - 6 AMSI bypasses, 7 ETW bypasses, anti-debug, sandbox checks, DLL unhooking, self-delete
- **Obfuscation** - Compile-time string encryption, call trampolines, junk code, opaque predicates, control flow flattening, LLVM IR-level passes (Obfuscator-LLVM)
- **Output** - EXE or DLL with custom exports
- **Cross-platform packer** - Builds and runs on Windows, Linux, macOS, and Docker; loader stubs target Windows

---

## Building

### Linux / WSL

```bash
make
```

Requires `g++` with C++23 support (GCC 13+ or Clang 16+). The built binary lands in `build/calypso`.

### Windows (MinGW)

```bash
make
```

Or use the convenience batch script:

```bash
build.bat
```

`build.bat` auto-detects MSVC (via `vswhere`) then falls back to MinGW.

### Docker

```bash
docker build -t calypso .
docker run --rm -v $(pwd)/payloads:/payloads calypso --file /payloads/beacon.bin
```

The Docker image (Ubuntu 24.04) includes `g++-14` and `mingw-w64` for cross-compiling Windows loaders.

### Building donut_ollvm

Calypso uses [donut_ollvm](tools/donut_ollvm/) for PE-to-shellcode conversion (`--peinject`). To build it:

```bash
cd tools/donut_ollvm/donut_ollvm
make
cp donut ../../donut
```

For OLLVM-obfuscated donut loaders (requires Obfuscator-LLVM):

```bash
./build_ollvm.sh --ollvm-path=/path/to/ollvm/bin --profile=aggressive
```

---

## Usage

```
calypso --file <payload> [options]
```

### Quick Examples

Pack shellcode with default settings (AES-256-ECB, indirect syscalls, HWBP AMSI/ETW bypass):

```bash
calypso --file beacon.bin
```

Pack a PE with AES-CBC, Halo's Gate syscalls, and source-level obfuscation:

```bash
calypso --file mimikatz.exe --cipher aes-cbc --syscall halosgate --obfuscate
```

Convert PE to shellcode via Donut, compress with LZ4, encode as UUID:

```bash
calypso --file payload.exe --peinject --compress lz4 --encode uuid
```

Pack a C# assembly as a DLL with CLR hosting:

```bash
calypso --file Rubeus.exe --dll --dll-export Run,Execute --crypto-backend tiny-aes
```

Remote injection into RuntimeBroker with PPID spoofing and DLL blocking:

```bash
calypso --file beacon.bin --inject remote --process RuntimeBroker.exe --ppid explorer.exe --block-dlls
```

Full evasion with multiple AMSI/ETW bypasses, sandbox checks, and sleep:

```bash
calypso --file beacon.bin --sleep 10 \
    --amsi hwbp,context-corrupt \
    --etw hwbp,complus \
    --sandbox domain,diskspace,memory \
    --self-delete --unhook ntdll.dll
```

Emit source only (for manual compilation or inspection):

```bash
calypso --file beacon.bin --source-only --obfuscate
```

### Cross-compiling Loaders on Linux

When running on Linux, Calypso auto-detects `x86_64-w64-mingw32-g++` to cross-compile Windows loader binaries:

```bash
sudo apt install mingw-w64
calypso --file beacon.bin --output loader.exe
```

---

## CLI Reference

### Payload Options

| Flag | Description |
|------|-------------|
| `--file <path>` | Input payload file (required) |
| `--type <type>` | Force type: `shellcode`, `pe`, `csharp` (default: auto-detect) |
| `--peinject` | Convert PE to shellcode via Donut before packing |
| `--arguments <args>` | Hardcode arguments into the loader |

### Encryption

| Flag | Description |
|------|-------------|
| `--key <key>` | Encryption key (random 32-char if omitted) |
| `--cipher <mode>` | `aes-ecb` (default), `aes-cbc`, `xor` |
| `--crypto-backend <be>` | `cng` (default, Windows BCrypt), `tiny-aes` (vendored, portable) |

### Encoding & Compression

| Flag | Description |
|------|-------------|
| `--encode <method>` | `none` (default), `base64`, `hex`, `mac`, `uuid` |
| `--compress <method>` | `none` (default), `zlib`, `lz4` |

### Obfuscation

| Flag | Description |
|------|-------------|
| `--obfuscate` | Source-level: string encryption, junk code, opaque predicates, control flow flattening |
| `--llvm-obfuscate` | IR-level: requires Obfuscator-LLVM (`clang++`) in PATH |
| `--obf-seed <seed>` | Custom seed for reproducible obfuscation |

### Injection

| Flag | Description |
|------|-------------|
| `--inject <method>` | `local` (default), `remote` |
| `--execute <prim>` | `direct` (default), `thread`, `apc`, `callback` |
| `--process <name>` | Target process for remote injection (default: `RuntimeBroker.exe`) |
| `--ppid <name>` | Parent process for PPID spoofing |
| `--block-dlls` | Block non-Microsoft DLLs in spawned process |

### Syscalls

| Flag | Description |
|------|-------------|
| `--syscall <method>` | `indirect` (default), `hellsgate`, `halosgate` |

### Evasion

| Flag | Description |
|------|-------------|
| `--sleep <seconds>` | Delay before decryption |
| `--amsi <methods>` | Comma-separated: `hwbp` (default), `provider-deregister`, `context-corrupt`, `ntcreatesection-hook`, `clr-patch`, `patch`, `none` |
| `--etw <methods>` | Comma-separated: `hwbp` (default), `provider-disable`, `complus`, `unhook-ntdll`, `patch-etwwrite`, `patch-nttrace`, `callback-remove`, `none` |
| `--no-antidebug` | Skip anti-debug checks |
| `--sandbox <checks>` | Comma-separated: `domain`, `diskspace`, `memory`, `emulated` |
| `--self-delete` | Loader deletes itself after execution |
| `--unhook <dlls>` | Unhook DLLs via fresh ntdll copy (comma-separated) |

### Output

| Flag | Description |
|------|-------------|
| `--output <path>` | Output file (random name if omitted) |
| `--dll` | Build as DLL instead of EXE |
| `--dll-export <names>` | Comma-separated DLL export names |
| `--hide` | Build as GUI app (no console window) |
| `--source-only` | Emit generated C++ source, skip compilation |
| `--verbose` | Loader prints debug output at runtime |

---

## Architecture

Calypso is a two-stage packer:

1. **Packer** (offline tool) - reads the payload, compresses, encrypts, encodes, generates C++ loader source from templates, then compiles it
2. **Loader** (output binary) - the packed EXE/DLL that decrypts and executes the payload at runtime using syscalls

```
Payload ──► Compress ──► Encrypt ──► Generate Loader Source ──► Compile ──► Packed Binary
                                          │
                                    Template Engine
                                    (stubs/*.cpp.in)
```

### Syscall Techniques

| Method | Description |
|--------|-------------|
| **Hell's Gate** | Scans ntdll export table for `mov r10,rcx; mov eax,SSN` opcode pattern to extract syscall numbers at runtime |
| **Halo's Gate** | Extends Hell's Gate: when a stub is hooked (JMP/FF), searches neighboring syscall stubs (+-1,2,...) and computes SSN by offset |
| **Indirect** | Maps a fresh copy of ntdll.dll from disk, finds the `syscall` instruction address, and jumps through it |

### AMSI Bypass Methods

| Method | Technique |
|--------|-----------|
| `hwbp` | Hardware breakpoint on `AmsiScanBuffer` via VEH (patchless, default) |
| `provider-deregister` | Deregister AMSI provider COM objects |
| `context-corrupt` | Corrupt the AMSI context signature bytes |
| `ntcreatesection-hook` | Hook `NtCreateSection` to block amsi.dll from loading |
| `clr-patch` | Patch CLR's internal AMSI wrapper instead of amsi.dll |
| `patch` | Classic `AmsiScanBuffer` patch (fallback) |

### ETW Bypass Methods

| Method | Technique |
|--------|-----------|
| `hwbp` | Hardware breakpoint on `EtwEventWrite` via VEH (patchless, default) |
| `provider-disable` | Disable .NET ETW provider registrations |
| `complus` | Set `COMPlus_ETWEnabled=0` before CLR init |
| `unhook-ntdll` | Map clean ntdll from disk, overwrite .text section |
| `patch-etwwrite` | Patch `EtwEventWrite` to `ret` |
| `patch-nttrace` | Patch `NtTraceEvent` to `ret` |
| `callback-remove` | Remove ETW notification callbacks |

### Obfuscation Layers

1. **Compile-time string encryption** - `obf("string")` macro using constexpr XOR with `__COUNTER__`-derived keys (inspired by ADVobfuscator)
2. **Compile-time call obfuscation** - Indirect function calls via volatile trampoline templates
3. **Source-level** - Junk code insertion, opaque predicates, control flow flattening (inspired by Obfusk8)
4. **LLVM IR-level** - Control flow flattening, instruction substitution, bogus control flow via Obfuscator-LLVM passes

---

## Project Structure

```
Calypso/
├── Makefile                     # Cross-platform build (Linux/Mac/Windows)
├── build.bat                    # Windows MSVC/MinGW convenience script
├── Dockerfile                   # Ubuntu 24.04 + mingw-w64
├── include/calypso/             # Packer headers
│   ├── config.hpp               # PackerConfig struct, all enums
│   ├── cli.hpp                  # CLI parsing
│   ├── crypto.hpp               # AES/XOR encryption
│   ├── encoding.hpp             # Base64/hex/MAC/UUID encoding
│   ├── compression.hpp          # Zlib/LZ4 compression
│   ├── obfuscation.hpp          # Source-level obfuscation generators
│   ├── payload.hpp              # Payload reading & detection
│   ├── pe_parser.hpp            # PE header parsing
│   ├── stub_generator.hpp       # Template engine
│   └── compiler.hpp             # Compiler detection & invocation
├── src/                         # Packer implementation
├── stubs/                       # Loader templates (.cpp.in / .hpp.in)
│   ├── loader_shellcode.cpp.in  # Shellcode injection loader
│   ├── loader_pe.cpp.in         # Run-PE loader
│   ├── loader_csharp.cpp.in     # CLR hosting loader
│   ├── syscalls.hpp.in          # Hell's/Halo's Gate, indirect syscalls
│   ├── evasion.hpp.in           # AMSI/ETW bypass, anti-debug, sandbox
│   ├── obfuscate.hpp.in         # Compile-time string/call encryption
│   ├── crypto_stub.hpp.in       # Decryption (CNG or tiny-AES)
│   └── common.hpp.in            # Decompression, decoding, utilities
├── third_party/                 # Vendored dependencies
│   ├── tiny-aes/                # AES-256 (kokke/tiny-AES-c, public domain)
│   ├── miniz/                   # Zlib-compatible (richgel999/miniz, MIT)
│   └── lz4/                     # LZ4 compression (Yann Collet, BSD 2-Clause)
└── tools/
    ├── donut                    # PE-to-shellcode converter (built from donut_ollvm)
    └── donut_ollvm/             # Donut fork with OLLVM obfuscation support
```

---

## Credits & References

| Project | Author | Use |
|---------|--------|-----|
| [NimSyscallPacker](https://github.com/ShitSecure/NimSyscallPacker) | @ShitSecure (Fabian Mosch) | Primary reference architecture, syscall techniques, evasion, injection |
| [Obfusk8](https://github.com/x86byte/Obfusk8) | @x86byte | Compile-time C++ obfuscation patterns |
| [ADVobfuscator](https://github.com/andrivet/advobfuscator) | Sebastien Andrivet | Compile-time metaprogramming, `__COUNTER__`-based key generation |
| [Obfuscator-LLVM](https://github.com/obfuscator-llvm/obfuscator) | - | LLVM IR-level obfuscation passes (FLA, SUB, BCF) |
| [Donut](https://github.com/TheWover/donut) | @TheWover, Odzhan | PE-to-shellcode conversion |
| [Hell's Gate](https://github.com/am0nsec/HellsGate) | @am0nsec, @smelly__vx | Runtime syscall number resolution via opcode matching |
| [Halo's Gate](https://blog.sektor7.net/) | @sektor7 | Neighbor-search for hooked syscall stubs |
| [SysWhispers](https://github.com/jthuraisamy/SysWhispers) | @jthuraisamy | Indirect syscall technique reference |
| [tiny-AES-c](https://github.com/kokke/tiny-AES-c) | kokke | Vendored AES implementation (public domain) |
| [miniz](https://github.com/richgel999/miniz) | richgel999 | Vendored zlib compression (MIT) |
| [LZ4](https://github.com/lz4/lz4) | Yann Collet | Vendored fast compression (BSD 2-Clause) |

### Technique References

- **Hardware Breakpoint AMSI/ETW bypass** - @CCob's concept, @ShitSecure's implementation
- **AMSI Provider Patching** - @RastaMouse, @KorKos (BlackHat Asia 2022)
- **AMSI Context Corruption** - @_RastaMouse
- **NtCreateSection AMSI Hook** - @waawaa
- **ETW Provider Disablement** - @_xpn
- **Fresh ntdll unhooking** - @_RastaMouse, @MDSec

---

## License

See individual third-party licenses in `third_party/`.
