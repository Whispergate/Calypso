# Calypso — C++23 Syscall Packer
# Build the packer tool (not the loader — the loader is generated at pack time)
# Cross-platform: works on Windows (MSVC/MinGW) and Linux (g++/clang++)

UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)

SOURCES = \
	src/main.cpp \
	src/cli.cpp \
	src/crypto.cpp \
	src/encoding.cpp \
	src/compression.cpp \
	src/payload.cpp \
	src/pe_parser.cpp \
	src/stub_generator.cpp \
	src/compiler.cpp \
	src/obfuscation.cpp

C_SOURCES = \
	third_party/tiny-aes/aes.c \
	third_party/miniz/miniz.c \
	third_party/lz4/lz4.c

INCLUDES = -Iinclude -Ithird_party/tiny-aes -Ithird_party/miniz -Ithird_party/lz4

OUTDIR = build

ifeq ($(UNAME_S),Linux)
    CXX = g++
    CC = gcc
    CXXFLAGS = -std=c++23 -O2 -Wall -Wextra -DNOMINMAX
    CFLAGS = -O2 -Wall -c
    TARGET = $(OUTDIR)/calypso
    LDFLAGS = -lstdc++fs
    MKDIR_P = mkdir -p
    RM_RF = rm -rf
else ifeq ($(UNAME_S),Darwin)
    CXX = clang++
    CC = clang
    CXXFLAGS = -std=c++2b -O2 -Wall -Wextra -DNOMINMAX
    CFLAGS = -O2 -Wall -c
    TARGET = $(OUTDIR)/calypso
    LDFLAGS =
    MKDIR_P = mkdir -p
    RM_RF = rm -rf
else
    # Windows (MSVC via nmake, or MinGW via make)
    CXX = g++
    CC = gcc
    CXXFLAGS = -std=c++23 -O2 -Wall -DWIN32 -D_WINDOWS -DNOMINMAX
    CFLAGS = -O2 -Wall -c
    TARGET = $(OUTDIR)/Calypso.exe
    LDFLAGS =
    MKDIR_P = mkdir -p
    RM_RF = rm -rf
endif

CPP_OBJS = $(patsubst src/%.cpp,$(OUTDIR)/%.o,$(SOURCES))
C_OBJS = $(patsubst third_party/%.c,$(OUTDIR)/tp_%.o,$(C_SOURCES))

all: $(TARGET) tools

$(OUTDIR):
	$(MKDIR_P) $(OUTDIR)

$(OUTDIR)/%.o: src/%.cpp | $(OUTDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(OUTDIR)/tp_tiny-aes_aes.o: third_party/tiny-aes/aes.c | $(OUTDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@

$(OUTDIR)/tp_miniz_miniz.o: third_party/miniz/miniz.c | $(OUTDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@

$(OUTDIR)/tp_lz4_lz4.o: third_party/lz4/lz4.c | $(OUTDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@

C_OBJS_EXPLICIT = $(OUTDIR)/tp_tiny-aes_aes.o $(OUTDIR)/tp_miniz_miniz.o $(OUTDIR)/tp_lz4_lz4.o

$(TARGET): $(CPP_OBJS) $(C_OBJS_EXPLICIT)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

# ---------- Tools ----------
# Build external tools in tools/ that have their own Makefile
tools: tools/donut

tools/donut: tools/donut_ollvm/Makefile
	@echo "[*] Building donut_ollvm..."
	$(MAKE) -C tools/donut_ollvm donut
	cp tools/donut_ollvm/donut tools/donut
	@echo "[+] tools/donut ready"

clean: clean-tools
	$(RM_RF) $(OUTDIR)

clean-tools:
	-$(MAKE) -C tools/donut_ollvm clean 2>/dev/null || true
	$(RM_RF) tools/donut

.PHONY: all clean clean-tools tools
