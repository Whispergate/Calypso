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

---

## Features

- **Payload types** - Shellcode, PE (Run-PE), C# assemblies (CLR hosting), PE-to-shellcode via Donut
- **Encryption** - AES-256-ECB, AES-256-CBC, XOR, RC4 with two backends (Windows CNG or vendored tiny-AES-c)
- **Encoding** - Base64, hex, MAC-address format, UUID format
- **Compression** - Zlib (miniz), LZ4, and RLE (run-length encoding)
- **Syscall methods** - Hell's Gate, Halo's Gate, Indirect Syscalls
- **Injection** - Local (direct/thread/APC/callback/fiber) and Remote (with PPID spoofing, DLL blocking)
- **Memory evasion** - Module stomping (file-backed memory), DripLoader (chunked writes with delays), entropy reduction (XOR masking)
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

The Docker image includes everything needed: `g++-14`, `mingw-w64`, LLVM 21, and the [eshard/obfuscator-llvm](https://github.com/eshard/obfuscator-llvm) plugin pre-built. No host dependencies required.

```bash
docker build -t calypso .
```

Mount your payloads directory and an output directory, then run as if calypso were installed locally:

```bash
docker run --rm \
    -v $(pwd)/payloads:/payloads \
    -v $(pwd)/output:/output \
    calypso --file /payloads/beacon.bin --output /output/loader.exe
```

With OLLVM obfuscation (the plugin is pre-installed in the image):

```bash
docker run --rm \
    -v $(pwd)/payloads:/payloads \
    -v $(pwd)/output:/output \
    calypso --file /payloads/beacon.bin --llvm-obfuscate --output /output/loader.exe
```

Full evasion example:

```bash
docker run --rm \
    -v $(pwd)/payloads:/payloads \
    -v $(pwd)/output:/output \
    calypso --file /payloads/beacon.bin \
        --cipher rc4 --compress rle --encode uuid \
        --execute fiber --module-stomp --drip --entropy-reduce \
        --amsi context-corrupt --etw complus \
        --sandbox domain,memory,diskspace \
        --self-delete --hide --llvm-obfuscate \
        --output /output/loader.exe
```

You can also create a shell alias for convenience:

```bash
alias calypso='docker run --rm -v $(pwd):/work -w /work calypso'
calypso --file beacon.bin --output loader.exe
```

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
| `--cipher <mode>` | `aes-ecb` (default), `aes-cbc`, `xor`, `rc4` |
| `--crypto-backend <be>` | `cng` (default, Windows BCrypt), `tiny-aes` (vendored, portable) |

### Encoding & Compression

| Flag | Description |
|------|-------------|
| `--encode <method>` | `none` (default), `base64`, `hex`, `mac`, `uuid` |
| `--compress <method>` | `none` (default), `zlib`, `lz4`, `rle` |

### Obfuscation

| Flag | Description |
|------|-------------|
| `--obfuscate` | Source-level: string encryption, junk code, opaque predicates, control flow flattening |
| `--llvm-obfuscate` | IR-level: requires [eshard/obfuscator-llvm](https://github.com/eshard/obfuscator-llvm) plugin (see [Installing Obfuscator-LLVM](#installing-obfuscator-llvm)) |
| `--ollvm-plugin <path>` | Path to `libLLVMObfuscator.so` (default: `/opt/llvm/libLLVMObfuscator.so`) |
| `--obf-seed <seed>` | Custom seed for reproducible obfuscation |

### Injection

| Flag | Description |
|------|-------------|
| `--inject <method>` | `local` (default), `remote` |
| `--execute <prim>` | `direct` (default), `thread`, `apc`, `callback`, `fiber` |
| `--process <name>` | Target process for remote injection (default: `RuntimeBroker.exe`) |
| `--ppid <name>` | Parent process for PPID spoofing |
| `--block-dlls` | Block non-Microsoft DLLs in spawned process |
| `--module-stomp` | Overwrite a sacrificial DLL's .text section instead of allocating new memory |
| `--drip` | Write shellcode in small 4KB chunks with variable delays |
| `--entropy-reduce` | XOR payload with English-frequency mask to lower .data section entropy |

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
| **Indirect** | Walks in-memory ntdll from PEB, resolves SSNs via Hell's/Halo's Gate, finds a clean `syscall; ret` gadget in .text, and jumps through it (no second ntdll load) |

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

### Memory Evasion Techniques

| Method | Technique |
|--------|-----------|
| `--module-stomp` | Load a sacrificial DLL (amsi.dll/dbghelp.dll), overwrite its .text section with shellcode - memory appears file-backed to scanners (evades Moneta, PE-Sieve unbacked detection) |
| `--drip` | DripLoader: write shellcode in 4KB chunks with variable NtDelayExecution delays between writes - evades real-time memory scanning during injection |
| `--execute fiber` | CaroKann: ConvertThreadToFiber → CreateFiber → SwitchToFiber execution - avoids CreateThread/NtCreateThreadEx-based detection |
| `--entropy-reduce` | XOR payload with English-frequency byte mask before embedding - reduces .data section entropy from ~7.9 to ~5.5, evading static entropy analysis |

### Obfuscation Layers

1. **Compile-time string encryption** - `obf("string")` macro using constexpr XOR with `__COUNTER__`-derived keys (inspired by ADVobfuscator)
2. **Compile-time call obfuscation** - Indirect function calls via volatile trampoline templates
3. **Source-level** - Junk code insertion, opaque predicates, control flow flattening (inspired by Obfusk8)
4. **LLVM IR-level** - Control flow flattening, instruction substitution, bogus control flow, basic block splitting, string obfuscation via [eshard/obfuscator-llvm](https://github.com/eshard/obfuscator-llvm) pass plugin

---

## Installing Obfuscator-LLVM

Calypso uses [eshard/obfuscator-llvm](https://github.com/eshard/obfuscator-llvm) as an LLVM new pass manager plugin. It works with standard LLVM 17+ (tested with LLVM 21).

### Prerequisites

```bash
sudo apt install llvm-21 llvm-21-dev clang-21 ninja-build cmake
```

### Building the Plugin

```bash
cd /opt
sudo git clone https://github.com/eshard/obfuscator-llvm
cd obfuscator-llvm
mkdir build && cd build
cmake -G "Ninja" -DLLVM_DIR=/usr/lib/llvm-21/lib/cmake/llvm ..
ninja -j$(nproc)
```

The built plugin is at `/opt/obfuscator-llvm/build/libLLVMObfuscator.so`. Copy it to the default search path:

```bash
sudo cp /opt/obfuscator-llvm/build/libLLVMObfuscator.so /opt/llvm/libLLVMObfuscator.so
```

### Usage

```bash
calypso --file beacon.bin --llvm-obfuscate
```

Calypso searches for `libLLVMObfuscator.so` in `/opt/llvm/`, `/usr/lib/`, and `/usr/local/lib/`. To use a custom path:

```bash
calypso --file beacon.bin --llvm-obfuscate --ollvm-plugin /path/to/libLLVMObfuscator.so
```

When `--llvm-obfuscate` is active, `clang++` cross-compiles the loader with `--target=x86_64-w64-mingw32` and the following OLLVM passes are applied:

| Pass | Effect |
|------|--------|
| **Flattening** | Transforms function control flow into flat switch dispatchers |
| **Substitution** | Replaces standard operations with equivalent complex sequences |
| **Bogus Control Flow** | Inserts fake basic blocks with opaque predicates |
| **Split Basic Blocks** | Splits blocks to increase CFG complexity |
| **String Encryption** | Encrypts string literals, decrypted at runtime |

---

## Credits & References

| Project | Author | Use |
|---------|--------|-----|
| [NimSyscallPacker](https://github.com/ShitSecure/NimSyscallPacker) | @ShitSecure (Fabian Mosch) | Primary reference architecture, syscall techniques, evasion, injection |
| [Obfusk8](https://github.com/x86byte/Obfusk8) | @x86byte | Compile-time C++ obfuscation patterns |
| [ADVobfuscator](https://github.com/andrivet/advobfuscator) | Sebastien Andrivet | Compile-time metaprogramming, `__COUNTER__`-based key generation |
| [obfuscator-llvm](https://github.com/eshard/obfuscator-llvm) | eshard | LLVM new pass manager plugin - flattening, substitution, bogus control flow, split basic blocks, string encryption |
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
