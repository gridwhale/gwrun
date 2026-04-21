#include "gwrun.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

static int path_looks_like_msys_rewritten_program_id(const char *path)
{
	return path &&
		(strncmp(path, "C:/Program Files/Git/file/", 26) == 0 ||
		 strncmp(path, "C:\\Program Files\\Git\\file\\", 26) == 0);
}

static int path_looks_like_mangled_windows_backslash_path(const char *path)
{
	const char *p;

	if (!path || !path[0] || !path[1] || path[1] != ':') {
		return 0;
	}
	if (strchr(path, '\\') != NULL || strchr(path, '/') != NULL) {
		return 0;
	}
	for (p = path + 2; *p; p++) {
		if (*p == '.' || *p == '-' || *p == '_') {
			return 1;
		}
	}
	return 0;
}

void buffer_init(GwBuffer *buf)
{
	buf->data = NULL;
	buf->len = 0;
	buf->cap = 0;
}

void buffer_free(GwBuffer *buf)
{
	free(buf->data);
	buf->data = NULL;
	buf->len = 0;
	buf->cap = 0;
}

int buffer_append(GwBuffer *buf, const char *data, size_t len)
{
	size_t next_len;
	size_t next_cap;
	char *next;

	if (len == 0) {
		return 1;
	}

	if (buf->len > ((size_t)-1) - len - 1) {
		return 0;
	}

	next_len = buf->len + len;
	if (next_len + 1 > buf->cap) {
		next_cap = buf->cap ? buf->cap : 256;
		while (next_cap < next_len + 1) {
			if (next_cap > ((size_t)-1) / 2) {
				return 0;
			}
			next_cap *= 2;
		}
		next = (char *)realloc(buf->data, next_cap);
		if (!next) {
			return 0;
		}
		buf->data = next;
		buf->cap = next_cap;
	}

	memcpy(buf->data + buf->len, data, len);
	buf->len = next_len;
	buf->data[buf->len] = '\0';
	return 1;
}

int buffer_append_cstr(GwBuffer *buf, const char *s)
{
	return buffer_append(buf, s, strlen(s));
}

char *json_escape_alloc(const char *s)
{
	GwBuffer out;
	const unsigned char *p;
	char tmp[8];

	buffer_init(&out);
	if (!buffer_append_cstr(&out, "\"")) {
		return NULL;
	}

	for (p = (const unsigned char *)s; *p; ++p) {
		switch (*p) {
		case '"':
			if (!buffer_append_cstr(&out, "\\\"")) goto fail;
			break;
		case '\\':
			if (!buffer_append_cstr(&out, "\\\\")) goto fail;
			break;
		case '\b':
			if (!buffer_append_cstr(&out, "\\b")) goto fail;
			break;
		case '\f':
			if (!buffer_append_cstr(&out, "\\f")) goto fail;
			break;
		case '\n':
			if (!buffer_append_cstr(&out, "\\n")) goto fail;
			break;
		case '\r':
			if (!buffer_append_cstr(&out, "\\r")) goto fail;
			break;
		case '\t':
			if (!buffer_append_cstr(&out, "\\t")) goto fail;
			break;
		default:
			if (*p < 0x20) {
				snprintf(tmp, sizeof(tmp), "\\u%04x", *p);
				if (!buffer_append_cstr(&out, tmp)) goto fail;
			} else {
				if (!buffer_append(&out, (const char *)p, 1)) goto fail;
			}
			break;
		}
	}

	if (!buffer_append_cstr(&out, "\"")) {
		goto fail;
	}

	return out.data;

fail:
	buffer_free(&out);
	return NULL;
}

char *read_file_alloc(const char *path, char *error, size_t error_len)
{
	FILE *f;
	long size;
	char *data;
	size_t got;

	f = fopen(path, "rb");
	if (!f) {
		if (path_looks_like_msys_rewritten_program_id(path)) {
			snprintf(error, error_len,
				"failed to open file: %s (Git Bash/MSYS may have rewritten /file/... into a Windows path; prefer PowerShell, use forward slashes, or set MSYS_NO_PATHCONV=1)",
				path);
		} else if (path_looks_like_mangled_windows_backslash_path(path)) {
			snprintf(error, error_len,
				"failed to open file: %s (this looks like a Windows path with backslashes eaten by bash; prefer forward slashes or quote/escape backslashes)",
				path);
		} else {
			snprintf(error, error_len, "failed to open file: %s", path);
		}
		return NULL;
	}

	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		snprintf(error, error_len, "failed to seek file: %s", path);
		return NULL;
	}

	size = ftell(f);
	if (size < 0) {
		fclose(f);
		snprintf(error, error_len, "failed to determine file size: %s", path);
		return NULL;
	}
	rewind(f);

	data = (char *)malloc((size_t)size + 1);
	if (!data) {
		fclose(f);
		snprintf(error, error_len, "out of memory reading file: %s", path);
		return NULL;
	}

	got = fread(data, 1, (size_t)size, f);
	fclose(f);
	if (got != (size_t)size) {
		free(data);
		snprintf(error, error_len, "failed to read entire file: %s", path);
		return NULL;
	}

	data[got] = '\0';
	return data;
}

char *read_json_file_alloc(const char *path, char *error, size_t error_len)
{
	char *data = read_file_alloc(path, error, error_len);
	size_t len;

	if (!data) {
		return NULL;
	}

	len = strlen(data);
	if (len >= 3 &&
		(unsigned char)data[0] == 0xef &&
		(unsigned char)data[1] == 0xbb &&
		(unsigned char)data[2] == 0xbf) {
		memmove(data, data + 3, len - 2);
	}
	return data;
}

long monotonic_ms(void)
{
#ifdef _WIN32
	return (long)GetTickCount64();
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#endif
}
