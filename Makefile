CC ?= gcc
CFLAGS ?= -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O2 -Iinclude
LDLIBS ?= -lcurl

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
BIN := gw
ALIAS := gridwhale

.PHONY: all clean copy-runtime

all: $(BIN) $(ALIAS)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(ALIAS): $(BIN)
	@if [ -f "$(BIN).exe" ]; then \
		cp "$(BIN).exe" "$@.exe"; \
	else \
		cp "$(BIN)" "$@"; \
	fi
	@touch "$@"

clean:
	rm -f $(OBJ) $(BIN) $(BIN).exe $(ALIAS) $(ALIAS).exe gwrun gwrun.exe

copy-runtime: $(BIN)
	@if command -v ldd >/dev/null 2>&1; then \
		ldd ./$(BIN).exe 2>/dev/null | awk '/\/ucrt64\/bin/ { print $$3 }' | while read dll; do \
			cp -u "$$dll" .; \
		done; \
		if [ -f /ucrt64/etc/ssl/certs/ca-bundle.crt ]; then \
			cp -u /ucrt64/etc/ssl/certs/ca-bundle.crt .; \
		fi; \
	else \
		echo "copy-runtime requires ldd"; \
		exit 1; \
	fi
