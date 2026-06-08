# qwen_asr — Qwen3-ASR Pure C/C++ Inference Engine
# Makefile

CC = gcc
CXX = g++
CFLAGS_BASE = -Wall -Wextra -O3 -march=native -ffast-math
LDFLAGS = -lm -lpthread -lstdc++

# Platform detection
UNAME_S := $(shell uname -s)

# Source files
C_SRCS = qwen_asr_kernels.c qwen_asr_kernels_generic.c qwen_asr_kernels_neon.c qwen_asr_kernels_avx.c
CXX_SRCS = qwen_asr.cpp qwen_asr_audio.cpp qwen_asr_encoder.cpp qwen_asr_decoder.cpp qwen_asr_tokenizer.cpp qwen_asr_safetensors.cpp
OBJS = $(C_SRCS:.c=.o) $(CXX_SRCS:.cpp=.o)
MAIN = main.cc
TARGET = qwen_asr

# Debug build flags
DEBUG_CFLAGS = -Wall -Wextra -g -O0 -DDEBUG -fsanitize=address

.PHONY: all clean debug info help blas test test-stream-cache

# Default: show available targets
all: help

help:
	@echo "qwen_asr — Qwen3-ASR Pure C/C++ Inference - Build Targets"
	@echo ""
	@echo "Choose a backend:"
	@echo "  make blas     - With BLAS acceleration (Accelerate/OpenBLAS)"
	@echo ""
	@echo "Other targets:"
	@echo "  make debug    - Debug build with AddressSanitizer"
	@echo "  make test     - Run regression suite (requires ./qwen_asr and model files)"
	@echo "  make test-stream-cache - Run stream cache on/off equivalence check"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make info     - Show build configuration"
	@echo ""
	@echo "Example: make blas && ./qwen_asr -d model_dir -i audio.wav"

# =============================================================================
# Backend: blas (Accelerate on macOS, OpenBLAS on Linux)
# =============================================================================
ifeq ($(UNAME_S),Darwin)
blas: CFLAGS = $(CFLAGS_BASE) -DUSE_BLAS -DACCELERATE_NEW_LAPACK
blas: LDFLAGS += -framework Accelerate
else
blas: CFLAGS = $(CFLAGS_BASE) -DUSE_BLAS -DUSE_OPENBLAS -I/usr/include/openblas
blas: LDFLAGS += -lopenblas
endif
blas:
	@$(MAKE) clean
	@$(MAKE) $(TARGET) CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)"
	@echo ""
	@echo "Built with BLAS backend"

# =============================================================================
# Build rules
# =============================================================================
$(TARGET): $(OBJS) main.o
	$(CXX) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c qwen_asr.h qwen_asr_kernels.h
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CFLAGS) -std=c++11 -c -o $@ $<

%.o: %.cc
	$(CXX) $(CFLAGS) -std=c++11 -c -o $@ $<

# Debug build
debug: CFLAGS = $(DEBUG_CFLAGS)
debug: LDFLAGS += -fsanitize=address
debug:
	@$(MAKE) clean
	@$(MAKE) $(TARGET) CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)"

# =============================================================================
# Utilities
# =============================================================================
clean:
	rm -f $(OBJS) main.o $(TARGET)

info:
	@echo "Platform: $(UNAME_S)"
	@echo "Compiler: $(CC) / $(CXX)"
	@echo ""
ifeq ($(UNAME_S),Darwin)
	@echo "Backend: blas (Apple Accelerate)"
else
	@echo "Backend: blas (OpenBLAS)"
endif

test:
	./asr_regression.py --binary ./qwen_asr --model-dir qwen3-asr-1.7b

# =============================================================================
# Dependencies
# =============================================================================
qwen_asr.o: qwen_asr.cpp qwen_asr.h qwen_asr_kernels.h qwen_asr_safetensors.h qwen_asr_audio.h qwen_asr_tokenizer.h log.h
qwen_asr_encoder.o: qwen_asr_encoder.cpp qwen_asr.h qwen_asr_kernels.h qwen_asr_safetensors.h log.h
qwen_asr_decoder.o: qwen_asr_decoder.cpp qwen_asr.h qwen_asr_kernels.h qwen_asr_safetensors.h log.h
qwen_asr_audio.o: qwen_asr_audio.cpp qwen_asr_audio.h log.h
qwen_asr_tokenizer.o: qwen_asr_tokenizer.cpp qwen_asr_tokenizer.h log.h
qwen_asr_safetensors.o: qwen_asr_safetensors.cpp qwen_asr_safetensors.h log.h
qwen_asr_kernels.o: qwen_asr_kernels.c qwen_asr_kernels.h qwen_asr_kernels_impl.h
qwen_asr_kernels_generic.o: qwen_asr_kernels_generic.c qwen_asr_kernels_impl.h
qwen_asr_kernels_neon.o: qwen_asr_kernels_neon.c qwen_asr_kernels_impl.h
qwen_asr_kernels_avx.o: qwen_asr_kernels_avx.c qwen_asr_kernels_impl.h
main.o: main.cc qwen_asr.h qwen_asr_kernels.h log.h
