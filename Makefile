CC      ?= cc
CFLAGS  ?= -O2 -g -std=c11 -Wall -Wextra -Wshadow
# prefix of the AArch64 binutils used to assemble the tests
CROSS   ?= aarch64-linux-gnu

LIB_SRC  = src/machine.c src/fdt.c src/boot.c src/cpu.c src/display.c src/input.c src/vm.c
HDRS     = $(wildcard src/*.h)

lxemu: src/main.c $(LIB_SRC) $(HDRS)
	$(CC) $(CFLAGS) -pthread -o $@ src/main.c $(LIB_SRC)

lib: build/liblxemu.a
build/liblxemu.a: $(LIB_SRC) $(HDRS)
	mkdir -p build/obj
	for f in $(LIB_SRC); do $(CC) $(CFLAGS) -c $$f -o build/obj/$$(basename $$f .c).o || exit 1; done
	ar rcs $@ build/obj/*.o

check: lxemu build/liblxemu.a
	CROSS=$(CROSS) sh tests/run.sh

clean:
	rm -rf lxemu build
.PHONY: lib check clean
