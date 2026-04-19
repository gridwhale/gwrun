CC ?= gcc
CFLAGS ?= -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O2 -Iinclude
LDLIBS ?= -lcurl

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
BIN := gw
ALIAS := gridwhale
GEN := src/help_text.h src/help_agents_text.h

.PHONY: all clean copy-runtime

all: $(BIN) $(ALIAS)

src/help_text.h: HELP.md tools/embed_help.awk
	awk -v var=GW_HELP_TEXT -f tools/embed_help.awk HELP.md > $@

src/help_agents_text.h: HELP_AGENTS.md tools/embed_help.awk
	awk -v var=GW_HELP_AGENTS_TEXT -f tools/embed_help.awk HELP_AGENTS.md > $@

src/commands.o: src/help_text.h src/help_agents_text.h

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
	rm -f $(OBJ) $(GEN) $(BIN) $(BIN).exe $(ALIAS) $(ALIAS).exe gwrun gwrun.exe

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
