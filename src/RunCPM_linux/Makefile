CORE := ../RunCPM_7xtgv-terminal
OUTPUT := ../../build/RunCPM_linux
PROGRAM := $(OUTPUT)/RunCPM
DATA := $(OUTPUT)/data
RUN_ROOT ?= $(DATA)

CC ?= cc
CFLAGS ?= -std=gnu11 -O2 -g -Wall -Wextra
CPPFLAGS += -I$(CORE)

.PHONY: all setup run smoke

all: $(PROGRAM)

$(PROGRAM): main.c abstraction_posix.h $(wildcard $(CORE)/*.h)
	mkdir -p $(OUTPUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) main.c -o $@

setup:
	mkdir -p $(DATA)/A/0

run: $(PROGRAM) setup
	test -d "$(RUN_ROOT)/A/0"
	cd "$(RUN_ROOT)" && $(abspath $(PROGRAM))

smoke: $(PROGRAM)
	python3 tests/smoke.py $(abspath $(PROGRAM)) $(abspath tests/VTDRV.COM)
