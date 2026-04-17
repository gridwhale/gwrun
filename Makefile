CC ?= gcc
CFLAGS ?= -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O2 -Iinclude
LDLIBS ?= -lcurl

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
BIN := gwrun

.PHONY: all clean copy-runtime

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

clean:
	rm -f $(OBJ) $(BIN)

copy-runtime: $(BIN)
	@if command -v ldd >/dev/null 2>&1; then \
		ldd ./$(BIN).exe 2>/dev/null | awk '/\/ucrt64\/bin/ { print $$3 }' | while read dll; do \
			cp -u "$$dll" .; \
		done; \
	else \
		echo "copy-runtime requires ldd"; \
		exit 1; \
	fi
