#include "gwrun.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

static char *cached_auth_header = NULL;

static int read_line(const char *prompt, char *buf, size_t buf_len, int echo)
{
	size_t len;

	if (buf_len == 0) {
		return 0;
	}

	fprintf(stderr, "%s", prompt);
	fflush(stderr);

#ifdef _WIN32
	if (!echo) {
		HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
		DWORD mode = 0;
		int changed = 0;
		if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
			SetConsoleMode(h, mode & ~(DWORD)ENABLE_ECHO_INPUT);
			changed = 1;
		}
		if (!fgets(buf, (int)buf_len, stdin)) {
			if (changed) {
				SetConsoleMode(h, mode);
			}
			fprintf(stderr, "\n");
			return 0;
		}
		if (changed) {
			SetConsoleMode(h, mode);
		}
		fprintf(stderr, "\n");
	} else
#else
	if (!echo) {
		struct termios old_term;
		struct termios new_term;
		int changed = 0;
		if (tcgetattr(STDIN_FILENO, &old_term) == 0) {
			new_term = old_term;
			new_term.c_lflag &= (tcflag_t)~ECHO;
			if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_term) == 0) {
				changed = 1;
			}
		}
		if (!fgets(buf, (int)buf_len, stdin)) {
			if (changed) {
				tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_term);
			}
			fprintf(stderr, "\n");
			return 0;
		}
		if (changed) {
			tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_term);
		}
		fprintf(stderr, "\n");
	} else
#endif
	{
		if (!fgets(buf, (int)buf_len, stdin)) {
			return 0;
		}
	}

	len = strlen(buf);
	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
		buf[--len] = '\0';
	}
	return 1;
}

static char *base64_alloc(const unsigned char *data, size_t len)
{
	static const char alphabet[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t out_len = ((len + 2) / 3) * 4;
	char *out = (char *)malloc(out_len + 1);
	size_t i;
	size_t j = 0;

	if (!out) {
		return NULL;
	}

	for (i = 0; i < len; i += 3) {
		unsigned int octet_a = data[i];
		unsigned int octet_b = (i + 1 < len) ? data[i + 1] : 0;
		unsigned int octet_c = (i + 2 < len) ? data[i + 2] : 0;
		unsigned int triple = (octet_a << 16) | (octet_b << 8) | octet_c;

		out[j++] = alphabet[(triple >> 18) & 0x3f];
		out[j++] = alphabet[(triple >> 12) & 0x3f];
		out[j++] = (i + 1 < len) ? alphabet[(triple >> 6) & 0x3f] : '=';
		out[j++] = (i + 2 < len) ? alphabet[triple & 0x3f] : '=';
	}

	out[j] = '\0';
	return out;
}

static char *prompt_basic_auth_alloc(void)
{
	char username[256];
	char password[512];
	GwBuffer plain;
	GwBuffer header;
	char *encoded;
	char *result = NULL;

	if (!read_line("GridWhale username: ", username, sizeof(username), 1)) {
		return NULL;
	}
	if (!read_line("GridWhale password: ", password, sizeof(password), 0)) {
		return NULL;
	}

	buffer_init(&plain);
	if (!buffer_append_cstr(&plain, username) ||
		!buffer_append_cstr(&plain, ":") ||
		!buffer_append_cstr(&plain, password)) {
		buffer_free(&plain);
		return NULL;
	}

	encoded = base64_alloc((const unsigned char *)plain.data, plain.len);
	memset(plain.data, 0, plain.len);
	buffer_free(&plain);
	memset(password, 0, sizeof(password));

	if (!encoded) {
		return NULL;
	}

	buffer_init(&header);
	if (buffer_append_cstr(&header, "Basic ") &&
		buffer_append_cstr(&header, encoded)) {
		result = header.data;
	} else {
		buffer_free(&header);
	}

	memset(encoded, 0, strlen(encoded));
	free(encoded);
	return result;
}

const char *auth_header_get(int prompt)
{
	const char *env_auth;

	if (cached_auth_header) {
		return cached_auth_header;
	}

	env_auth = getenv("GRIDWHALE_AUTH_HEADER");
	if (env_auth && env_auth[0]) {
		return env_auth;
	}

	if (!prompt) {
		return NULL;
	}

	cached_auth_header = prompt_basic_auth_alloc();
	return cached_auth_header;
}

int auth_header_configured(void)
{
	return auth_header_get(0) != NULL;
}
