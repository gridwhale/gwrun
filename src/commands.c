#include "gwrun.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "help_text.h"
#include "help_agents_text.h"

static int wants_json(const GwOptions *opts)
{
	return strcmp(opts->output, "json") == 0;
}

static char *copy_cstr_alloc(const char *s)
{
	size_t len;
	char *out;

	if (!s) {
		return NULL;
	}
	len = strlen(s);
	out = (char *)malloc(len + 1);
	if (!out) {
		return NULL;
	}
	memcpy(out, s, len + 1);
	return out;
}

static const char *find_range(const char *start, const char *end, const char *needle)
{
	size_t needle_len = strlen(needle);
	const char *p;

	if (needle_len == 0) {
		return start;
	}

	for (p = start; p && p + needle_len <= end; p++) {
		if (memcmp(p, needle, needle_len) == 0) {
			return p;
		}
	}
	return NULL;
}

static const char *find_last_range(const char *start, const char *end, const char *needle)
{
	const char *p = start;
	const char *last = NULL;

	while ((p = find_range(p, end, needle)) != NULL) {
		last = p;
		p++;
	}
	return last;
}

static const char *find_matching_object_end(const char *object_start)
{
	const char *p;
	int depth = 0;
	int in_string = 0;
	int escape = 0;

	for (p = object_start; *p; p++) {
		if (in_string) {
			if (escape) {
				escape = 0;
			} else if (*p == '\\') {
				escape = 1;
			} else if (*p == '"') {
				in_string = 0;
			}
			continue;
		}

		if (*p == '"') {
			in_string = 1;
		} else if (*p == '{') {
			depth++;
		} else if (*p == '}') {
			depth--;
			if (depth == 0) {
				return p + 1;
			}
		}
	}
	return NULL;
}

static const char *find_matching_array_end(const char *array_start)
{
	const char *p;
	int depth = 0;
	int in_string = 0;
	int escape = 0;

	for (p = array_start; *p; p++) {
		if (in_string) {
			if (escape) {
				escape = 0;
			} else if (*p == '\\') {
				escape = 1;
			} else if (*p == '"') {
				in_string = 0;
			}
			continue;
		}

		if (*p == '"') {
			in_string = 1;
		} else if (*p == '[') {
			depth++;
		} else if (*p == ']') {
			depth--;
			if (depth == 0) {
				return p + 1;
			}
		}
	}
	return NULL;
}

static const char *find_json_value_end(const char *value_start)
{
	const char *p = value_start;

	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
		p++;
	}

	if (*p == '{') {
		return find_matching_object_end(p);
	}
	if (*p == '[') {
		return find_matching_array_end(p);
	}
	if (*p == '"') {
		int escape = 0;
		p++;
		while (*p) {
			if (escape) {
				escape = 0;
			} else if (*p == '\\') {
				escape = 1;
			} else if (*p == '"') {
				return p + 1;
			}
			p++;
		}
		return NULL;
	}

	while (*p && *p != ',' && *p != '}' && *p != ']') {
		p++;
	}
	return p;
}

static char *json_value_dup(const char *value_start)
{
	const char *start = value_start;
	const char *end;
	char *out;
	size_t len;

	while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
		start++;
	}
	end = find_json_value_end(start);
	if (!end || end < start) {
		return NULL;
	}
	while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
		end--;
	}

	len = (size_t)(end - start);
	out = (char *)malloc(len + 1);
	if (!out) {
		return NULL;
	}
	memcpy(out, start, len);
	out[len] = '\0';
	return out;
}

static char *json_object_member_dup(const char *json, const char *name)
{
	GwBuffer key;
	const char *p;
	const char *colon;
	char *value;

	buffer_init(&key);
	if (!buffer_append_cstr(&key, "\"") ||
		!buffer_append_cstr(&key, name) ||
		!buffer_append_cstr(&key, "\"")) {
		buffer_free(&key);
		return NULL;
	}

	p = strstr(json, key.data);
	buffer_free(&key);
	if (!p) {
		return NULL;
	}
	colon = strchr(p, ':');
	if (!colon) {
		return NULL;
	}
	value = json_value_dup(colon + 1);
	return value;
}

static char *json_object_path_dup(const char *json, const char *object_name, const char *member_name)
{
	char *object_json = json_object_member_dup(json, object_name);
	char *value = NULL;

	if (object_json) {
		value = json_object_member_dup(object_json, member_name);
		free(object_json);
	}
	return value;
}

static int is_simple_program_id(const char *program_id)
{
	const unsigned char *p = (const unsigned char *)program_id;

	if (!program_id || !program_id[0]) {
		return 0;
	}
	if (strncmp(program_id, "/file/", 6) == 0) {
		return 0;
	}
	while (*p) {
		if (!((*p >= 'A' && *p <= 'Z') ||
			(*p >= 'a' && *p <= 'z') ||
			(*p >= '0' && *p <= '9') ||
			*p == '_' || *p == '-')) {
			return 0;
		}
		p++;
	}
	return 1;
}

static const char *msys_rewritten_program_id_suffix(const char *program_id)
{
	if (!program_id) {
		return NULL;
	}
	if (strncmp(program_id, "C:/Program Files/Git/file/", 26) == 0) {
		return program_id + 26;
	}
	if (strncmp(program_id, "C:\\Program Files\\Git\\file\\", 26) == 0) {
		return program_id + 26;
	}
	return NULL;
}

static char *normalize_program_id_alloc(const char *program_id)
{
	GwBuffer out;
	const char *rewritten_suffix;

	if (!program_id) {
		return NULL;
	}
	rewritten_suffix = msys_rewritten_program_id_suffix(program_id);
	if (rewritten_suffix) {
		buffer_init(&out);
		if (!buffer_append_cstr(&out, "/file/") ||
			!buffer_append_cstr(&out, rewritten_suffix)) {
			buffer_free(&out);
			return NULL;
		}
		return out.data;
	}
	if (!is_simple_program_id(program_id)) {
		return copy_cstr_alloc(program_id);
	}
	buffer_init(&out);
	if (!buffer_append_cstr(&out, "/file/") ||
		!buffer_append_cstr(&out, program_id)) {
		buffer_free(&out);
		return NULL;
	}
	return out.data;
}

static char *normalize_process_program_alloc(const char *program)
{
	GwBuffer out;
	const char *rewritten_suffix;

	if (!program) {
		return NULL;
	}
	rewritten_suffix = msys_rewritten_program_id_suffix(program);
	if (rewritten_suffix) {
		buffer_init(&out);
		if (!buffer_append_cstr(&out, "/file/") ||
			!buffer_append_cstr(&out, rewritten_suffix)) {
			buffer_free(&out);
			return NULL;
		}
		return out.data;
	}
	if (!is_simple_program_id(program)) {
		return copy_cstr_alloc(program);
	}
	buffer_init(&out);
	if (!buffer_append_cstr(&out, "/file/") ||
		!buffer_append_cstr(&out, program)) {
		buffer_free(&out);
		return NULL;
	}
	return out.data;
}

static char *json_string_dup(const char *quote, const char **end_out);

static unsigned int read_u32_le(const unsigned char *p)
{
	return ((unsigned int)p[0]) |
		((unsigned int)p[1] << 8) |
		((unsigned int)p[2] << 16) |
		((unsigned int)p[3] << 24);
}

static int base64_value(char ch)
{
	if (ch >= 'A' && ch <= 'Z') return ch - 'A';
	if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
	if (ch >= '0' && ch <= '9') return ch - '0' + 52;
	if (ch == '+') return 62;
	if (ch == '/') return 63;
	return -1;
}

static unsigned char *base64_decode_alloc(const char *text, size_t *len_out)
{
	size_t len = strlen(text);
	size_t cap = (len / 4) * 3 + 3;
	unsigned char *out = (unsigned char *)malloc(cap);
	size_t out_len = 0;
	int vals[4];
	int n = 0;
	size_t i;

	if (!out) {
		return NULL;
	}

	for (i = 0; i < len; i++) {
		int v;
		char ch = text[i];

		if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
			continue;
		}
		if (ch == '=') {
			vals[n++] = -2;
		} else {
			v = base64_value(ch);
			if (v < 0) {
				free(out);
				return NULL;
			}
			vals[n++] = v;
		}

		if (n == 4) {
			if (vals[0] < 0 || vals[1] < 0) {
				free(out);
				return NULL;
			}
			out[out_len++] = (unsigned char)((vals[0] << 2) | (vals[1] >> 4));
			if (vals[2] != -2) {
				if (vals[2] < 0) {
					free(out);
					return NULL;
				}
				out[out_len++] = (unsigned char)(((vals[1] & 15) << 4) | (vals[2] >> 2));
			}
			if (vals[3] != -2) {
				if (vals[2] < 0 || vals[3] < 0) {
					free(out);
					return NULL;
				}
				out[out_len++] = (unsigned char)(((vals[2] & 3) << 6) | vals[3]);
			}
			n = 0;
		}
	}

	if (n != 0) {
		free(out);
		return NULL;
	}
	*len_out = out_len;
	return out;
}

static char *decode_aeon_text_lines_alloc(const char *encoded)
{
	unsigned char *bytes;
	size_t len;
	size_t pos;
	unsigned int version;
	unsigned int line_count;
	unsigned int i;
	GwBuffer out;

	bytes = base64_decode_alloc(encoded, &len);
	if (!bytes) {
		return NULL;
	}
	if (len < 12) {
		free(bytes);
		return NULL;
	}

	line_count = read_u32_le(bytes);
	version = read_u32_le(bytes + 4);
	if (version != 1) {
		free(bytes);
		return NULL;
	}

	buffer_init(&out);
	pos = 12;
	for (i = 0; i < line_count; i++) {
		unsigned int line_len;

		if (pos + 4 > len) {
			buffer_free(&out);
			free(bytes);
			return NULL;
		}
		line_len = read_u32_le(bytes + pos);
		pos += 4;
		if (pos + line_len > len) {
			buffer_free(&out);
			free(bytes);
			return NULL;
		}
		if (i > 0 && !buffer_append_cstr(&out, "\n")) {
			buffer_free(&out);
			free(bytes);
			return NULL;
		}
		if (!buffer_append(&out, (const char *)(bytes + pos), line_len)) {
			buffer_free(&out);
			free(bytes);
			return NULL;
		}
		pos += line_len;
		pos = (pos + 3) & ~(size_t)3;
	}

	free(bytes);
	if (!out.data && !buffer_append_cstr(&out, "")) {
		return NULL;
	}
	return out.data;
}

static char *program_read_source_from_value_dup(const char *value)
{
	const char *quote;
	char *source;

	if (!value) {
		return NULL;
	}
	while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n') {
		value++;
	}

	if (*value == '"') {
		return json_string_dup(value, NULL);
	}
	if (*value != '[') {
		return NULL;
	}

	quote = strchr(value, '"');
	if (!quote) {
		return NULL;
	}
	source = json_string_dup(quote, NULL);
	if (!source) {
		return NULL;
	}
	if (strcmp(source, "AEON2011:textLines:v1") == 0) {
		const char *comma = strchr(quote + 1, ',');
		char *encoded;
		char *decoded;

		free(source);
		if (!comma) {
			return NULL;
		}
		quote = strchr(comma, '"');
		if (!quote) {
			return NULL;
		}
		encoded = json_string_dup(quote, NULL);
		if (!encoded) {
			return NULL;
		}
		decoded = decode_aeon_text_lines_alloc(encoded);
		free(encoded);
		return decoded;
	}
	return source;
}

static char *program_read_source_dup(const char *json)
{
	const char *scan = json;
	char *value;
	char *source;

	value = json_object_path_dup(json, "structuredContent", "sourceCode");
	source = program_read_source_from_value_dup(value);
	free(value);
	if (source) {
		return source;
	}

	while ((scan = strstr(scan, "\"text\"")) != NULL) {
		const char *colon = strchr(scan, ':');
		char *text;

		if (!colon) {
			break;
		}
		while (*colon == ':' || *colon == ' ' || *colon == '\t' || *colon == '\r' || *colon == '\n') {
			colon++;
		}
		if (*colon != '"') {
			scan = colon;
			continue;
		}

		text = json_string_dup(colon, NULL);
		if (text) {
			value = json_object_member_dup(text, "sourceCode");
			source = program_read_source_from_value_dup(value);
			free(value);
			free(text);
			if (source) {
				return source;
			}
		}
		scan = colon + 1;
	}

	value = json_object_member_dup(json, "sourceCode");
	source = program_read_source_from_value_dup(value);
	free(value);
	return source;
}

static char *json_string_dup(const char *quote, const char **end_out)
{
	GwBuffer out;
	const char *p;
	char ch;

	if (!quote || *quote != '"') {
		return NULL;
	}

	buffer_init(&out);
	for (p = quote + 1; *p; p++) {
		if (*p == '"') {
			if (end_out) {
				*end_out = p + 1;
			}
			if (!out.data) {
				if (!buffer_append_cstr(&out, "")) {
					return NULL;
				}
			}
			return out.data;
		}
		if (*p == '\\') {
			p++;
			if (!*p) {
				break;
			}
			switch (*p) {
			case '"': ch = '"'; break;
			case '\\': ch = '\\'; break;
			case '/': ch = '/'; break;
			case 'b': ch = '\b'; break;
			case 'f': ch = '\f'; break;
			case 'n': ch = '\n'; break;
			case 'r': ch = '\r'; break;
			case 't': ch = '\t'; break;
			case 'u':
				ch = '?';
				if (p[1] && p[2] && p[3] && p[4]) {
					p += 4;
				}
				break;
			default:
				ch = *p;
				break;
			}
		} else {
			ch = *p;
		}
		if (!buffer_append(&out, &ch, 1)) {
			buffer_free(&out);
			return NULL;
		}
	}

	buffer_free(&out);
	return NULL;
}

static void print_indented_text(const char *text, const char *indent)
{
	const char *p;

	if (!text || !text[0]) {
		return;
	}

	printf("%s", indent);
	for (p = text; *p; p++) {
		putchar(*p);
		if (*p == '\n' && p[1]) {
			printf("%s", indent);
		}
	}
	if (p > text && p[-1] != '\n') {
		putchar('\n');
	}
}

static int print_tools_list_text(const char *json)
{
	const char *tools = strstr(json, "\"tools\"");
	const char *scan;
	int count = 0;

	printf("Tools:\n");
	if (!tools) {
		printf("  (none)\n");
		return 0;
	}

	scan = strchr(tools, '[');
	if (!scan) {
		printf("  (none)\n");
		return 0;
	}
	scan++;

	while (*scan) {
		const char *object_start;
		const char *object_end;
		const char *name_key;
		const char *name_colon;
		const char *name_quote;
		const char *name_end = NULL;
		const char *desc_key;
		char *name;
		char *desc = NULL;

		while (*scan && *scan != '{' && *scan != ']') {
			scan++;
		}
		if (*scan == ']') {
			break;
		}
		if (*scan != '{') {
			break;
		}

		object_start = scan;
		object_end = find_matching_object_end(object_start);
		if (!object_end) {
			break;
		}

		name_key = find_last_range(object_start, object_end, "\"name\"");
		if (!name_key) {
			scan = object_end;
			continue;
		}
		name_colon = strchr(name_key, ':');
		if (!name_colon) {
			break;
		}
		name_quote = strchr(name_colon, '"');
		if (!name_quote) {
			break;
		}

		name = json_string_dup(name_quote, &name_end);
		if (!name) {
			break;
		}

		desc_key = find_range(object_start, object_end, "\"description\"");
		if (desc_key) {
			const char *desc_colon = strchr(desc_key, ':');
			const char *desc_quote = desc_colon ? strchr(desc_colon, '"') : NULL;
			if (desc_quote && desc_quote < object_end) {
				desc = json_string_dup(desc_quote, NULL);
			}
		}

		printf("  %s\n", name);
		if (desc && desc[0]) {
			print_indented_text(desc, "    ");
		}
		free(name);
		free(desc);

		count++;
		(void)name_end;
		scan = object_end;
	}

	if (count == 0) {
		printf("  (none)\n");
	}
	return count;
}

static int print_remote_result(const GwOptions *opts, const char *command, GwHttpResponse *res, long duration_ms)
{
	if (res->status >= 200 && res->status < 300) {
		if (wants_json(opts)) {
			printf("{\"ok\":true,\"command\":\"%s\",\"server\":", command);
			char *server = json_escape_alloc(opts->server);
			printf("%s", server ? server : "\"\"");
			free(server);
			printf(",\"durationMs\":%ld,\"result\":%s}\n", duration_ms, res->body.data ? res->body.data : "null");
		} else {
			printf("%s\n", res->body.data ? res->body.data : "");
		}
		return 0;
	}

	if (wants_json(opts)) {
		char *err = json_escape_alloc(res->error[0] ? res->error : "remote request failed");
		printf("{\"ok\":false,\"command\":\"%s\",\"status\":\"remote_error\",\"httpStatus\":%ld,\"durationMs\":%ld,\"error\":{\"message\":%s}",
			command, res->status, duration_ms, err ? err : "\"\"");
		if (res->body.data && res->body.data[0]) {
			printf(",\"result\":%s", res->body.data);
		}
		printf("}\n");
		free(err);
	} else {
		fprintf(stderr, "gw: remote request failed");
		if (res->status) {
			fprintf(stderr, " (HTTP %ld)", res->status);
		}
		if (res->error[0]) {
			fprintf(stderr, ": %s", res->error);
		}
		fprintf(stderr, "\n");
		if (res->body.data && res->body.data[0]) {
			fprintf(stderr, "%s\n", res->body.data);
		}
	}
	return 3;
}

static char *mcp_endpoint_url_alloc(const GwOptions *opts, const char *name)
{
	const char *server = opts->server;
	const char *mcp = strstr(server, "/mcp");
	GwBuffer out;

	buffer_init(&out);
	if (mcp) {
		size_t prefix_len = (size_t)(mcp - server);
		if (!buffer_append(&out, server, prefix_len) ||
			!buffer_append_cstr(&out, "/mcp/") ||
			!buffer_append_cstr(&out, name)) {
			buffer_free(&out);
			return NULL;
		}
		return out.data;
	}

	if (!buffer_append_cstr(&out, server)) {
		return NULL;
	}
	if (out.len > 0 && out.data[out.len - 1] == '/') {
		out.len--;
		out.data[out.len] = '\0';
	}
	if (!buffer_append_cstr(&out, "/mcp/") ||
		!buffer_append_cstr(&out, name)) {
		buffer_free(&out);
		return NULL;
	}
	return out.data;
}

static int process_post(const GwOptions *opts, const char *endpoint, const char *body, GwHttpResponse *res)
{
	char *url = mcp_endpoint_url_alloc(opts, endpoint);
	int ok;

	if (!url) {
		res->status = 0;
		res->error[0] = '\0';
		snprintf(res->error, sizeof(res->error), "out of memory building process endpoint URL");
		buffer_init(&res->body);
		return 0;
	}
	ok = http_post_json_url(opts, url, body, res);
	free(url);
	return ok;
}

static int validate_process_program_ref(const char *program)
{
	const char *dot;

	if (!program || !program[0] || program[0] == '.') {
		return 0;
	}
	if (strncmp(program, "/file/", 6) == 0) {
		const char *id = program + 6;
		return id[0] && strchr(id, '.') == NULL && strchr(id, '/') == NULL;
	}
	if (program[0] == '/') {
		return 0;
	}
	dot = strchr(program, '.');
	if (!dot || dot == program || !dot[1]) {
		return 0;
	}
	if (strchr(dot + 1, '.')) {
		return 0;
	}
	return 1;
}

static int reject_invalid_process_program_ref(const GwOptions *opts, const char *command, const char *program)
{
	char *program_json;

	if (validate_process_program_ref(program)) {
		return 0;
	}

	program_json = json_escape_alloc(program ? program : "");
	if (wants_json(opts)) {
		printf("{\"ok\":false,\"command\":\"%s\",\"status\":\"invalid_arguments\",\"error\":{\"message\":\"process start expects PROGRAMID.entryPoint, /file/PROGRAMID, or a bare PROGRAMID, for example NUEG3K9Y.HelloWorld, /file/7QGK2YY9, or 7QGK2YY9\",\"program\":%s}}\n",
			command, program_json ? program_json : "\"\"");
	} else {
		fprintf(stderr, "gw: process start expects PROGRAMID.entryPoint, /file/PROGRAMID, or a bare PROGRAMID, for example NUEG3K9Y.HelloWorld, /file/7QGK2YY9, or 7QGK2YY9; got %s\n",
			program ? program : "");
	}
	free(program_json);
	return 2;
}

static int build_process_start_body(GwBuffer *body, const char *program, const char *args_json)
{
	char *program_json = json_escape_alloc(program);
	int ok;

	if (!program_json) {
		return 0;
	}
	buffer_init(body);
	ok = buffer_append_cstr(body, "{\"program\":") &&
		buffer_append_cstr(body, program_json) &&
		buffer_append_cstr(body, ",\"args\":") &&
		buffer_append_cstr(body, args_json ? args_json : "{}") &&
		buffer_append_cstr(body, "}");
	free(program_json);
	if (!ok) {
		buffer_free(body);
	}
	return ok;
}

static int build_process_view_body(GwBuffer *body, const char *process_id, const char *seq_json)
{
	char *id_json = json_escape_alloc(process_id);
	int ok;

	if (!id_json) {
		return 0;
	}
	buffer_init(body);
	ok = buffer_append_cstr(body, "{\"processID\":") &&
		buffer_append_cstr(body, id_json) &&
		buffer_append_cstr(body, ",\"seq\":") &&
		buffer_append_cstr(body, seq_json ? seq_json : "0") &&
		buffer_append_cstr(body, "}");
	free(id_json);
	if (!ok) {
		buffer_free(body);
	}
	return ok;
}

static int build_process_input_body(GwBuffer *body, const char *process_id, const char *input_text, const char *seq_json)
{
	char *id_json = json_escape_alloc(process_id);
	char *input_json = json_escape_alloc(input_text ? input_text : "");
	int ok;

	if (!id_json || !input_json) {
		free(id_json);
		free(input_json);
		return 0;
	}
	buffer_init(body);
	ok = buffer_append_cstr(body, "{\"processID\":") &&
		buffer_append_cstr(body, id_json) &&
		buffer_append_cstr(body, ",\"inputText\":") &&
		buffer_append_cstr(body, input_json) &&
		buffer_append_cstr(body, ",\"seq\":") &&
		buffer_append_cstr(body, seq_json ? seq_json : "0") &&
		buffer_append_cstr(body, "}");
	free(id_json);
	free(input_json);
	if (!ok) {
		buffer_free(body);
	}
	return ok;
}

static int print_json_string_array_member(const char *json, const char *member_name)
{
	char *array_json = json_object_member_dup(json, member_name);
	const char *p;
	int count = 0;

	if (!array_json || array_json[0] != '[') {
		free(array_json);
		return 0;
	}

	p = array_json + 1;
	while (*p) {
		while (*p && *p != '"' && *p != ']') {
			p++;
		}
		if (*p == ']') {
			break;
		}
		if (*p == '"') {
			const char *end = NULL;
			char *text = json_string_dup(p, &end);
			if (!text) {
				break;
			}
			fputs(text, stdout);
			if (text[0] && text[strlen(text) - 1] != '\n') {
				putchar('\n');
			}
			free(text);
			count++;
			p = end ? end : p + 1;
		}
	}
	free(array_json);
	return count;
}

static int print_resources_list_text(const char *json)
{
	const char *resources = strstr(json, "\"resources\"");
	const char *scan;
	int count = 0;

	printf("Resources:\n");
	if (!resources) {
		printf("  (none)\n");
		return 0;
	}

	scan = strchr(resources, '[');
	if (!scan) {
		printf("  (none)\n");
		return 0;
	}
	scan++;

	while (*scan) {
		const char *object_start;
		const char *object_end;
		const char *uri_key;
		const char *uri_colon;
		const char *uri_quote;
		const char *name_key;
		char *uri;
		char *name = NULL;

		while (*scan && *scan != '{' && *scan != ']') {
			scan++;
		}
		if (*scan == ']') {
			break;
		}
		if (*scan != '{') {
			break;
		}

		object_start = scan;
		object_end = find_matching_object_end(object_start);
		if (!object_end) {
			break;
		}

		uri_key = find_range(object_start, object_end, "\"uri\"");
		if (!uri_key) {
			scan = object_end;
			continue;
		}
		uri_colon = strchr(uri_key, ':');
		uri_quote = uri_colon ? strchr(uri_colon, '"') : NULL;
		if (!uri_quote || uri_quote >= object_end) {
			scan = object_end;
			continue;
		}

		uri = json_string_dup(uri_quote, NULL);
		if (!uri) {
			break;
		}

		name_key = find_range(object_start, object_end, "\"name\"");
		if (name_key) {
			const char *name_colon = strchr(name_key, ':');
			const char *name_quote = name_colon ? strchr(name_colon, '"') : NULL;
			if (name_quote && name_quote < object_end) {
				name = json_string_dup(name_quote, NULL);
			}
		}

		printf("  %s", uri);
		if (name && name[0]) {
			printf("  %s", name);
		}
		putchar('\n');
		free(uri);
		free(name);
		count++;
		scan = object_end;
	}

	if (count == 0) {
		printf("  (none)\n");
	}
	return count;
}

static int print_resource_read_text(const char *json)
{
	const char *contents = strstr(json, "\"contents\"");
	const char *scan;
	int count = 0;

	if (!contents) {
		printf("%s\n", json ? json : "");
		return 0;
	}

	scan = strchr(contents, '[');
	if (!scan) {
		printf("%s\n", json ? json : "");
		return 0;
	}
	scan++;

	while (*scan) {
		const char *object_start;
		const char *object_end;
		const char *text_key;
		char *text = NULL;

		while (*scan && *scan != '{' && *scan != ']') {
			scan++;
		}
		if (*scan == ']') {
			break;
		}
		if (*scan != '{') {
			break;
		}

		object_start = scan;
		object_end = find_matching_object_end(object_start);
		if (!object_end) {
			break;
		}

		text_key = find_range(object_start, object_end, "\"text\"");
		if (text_key) {
			const char *text_colon = strchr(text_key, ':');
			const char *text_quote = text_colon ? strchr(text_colon, '"') : NULL;
			if (text_quote && text_quote < object_end) {
				text = json_string_dup(text_quote, NULL);
			}
		}
		if (text) {
			fputs(text, stdout);
			if (text[0] && text[strlen(text) - 1] != '\n') {
				putchar('\n');
			}
			free(text);
			count++;
		}
		scan = object_end;
	}

	if (count == 0) {
		printf("%s\n", json ? json : "");
	}
	return count;
}

void print_usage(void)
{
	printf("GridWhale CLI %s\n\n", GWRUN_VERSION);
	printf("Usage:\n");
	printf("  gw [--server URL] [--output text|json] [--insecure] version\n");
	printf("  gw help [agents]\n");
	printf("  gw [--server URL] [--output text|json] check\n");
	printf("  gw [--server URL] [--output text|json] manifest\n");
	printf("  gw [--server URL] [--output text|json] tools list\n");
	printf("  gw [--server URL] [--output text|json] tools describe <name>\n");
	printf("  gw [--server URL] [--output text|json] tools call <name> --json <object>\n");
	printf("  gw [--server URL] [--output text|json] call <name> --json-file <path>\n");
	printf("  gw [--server URL] [--output text|json] resources list\n");
	printf("  gw [--server URL] [--output text|json] resources read <uri>\n");
	printf("  gw [--server URL] [--output text|json] program create --name <name>\n");
	printf("  gw [--server URL] [--output text|json] program write <programID> --file <path>\n");
	printf("  gw [--server URL] [--output text|json] program read <programID>\n");
	printf("  gw [--server URL] [--output text|json] program compile <programID>\n");
	printf("  gw [--server URL] [--output text|json] program run <programID> --json-file <path>\n");
	printf("  gw [--server URL] [--output text|json] process list\n");
	printf("  gw [--server URL] [--output text|json] process kill <processID>\n");
	printf("  gw [--server URL] [--output text|json] process start <programID-or-entryPoint> --json <object>\n");
	printf("  gw [--server URL] [--output text|json] process view <processID> --seq <json>\n");
	printf("  gw [--server URL] [--output text|json] process view <processID> --seq-file <path>\n");
	printf("  gw [--server URL] [--output text|json] process input <processID> --text <text> --seq-file <path>\n");
	printf("  gw [--server URL] run <programID-or-entryPoint> --json-file <path>\n\n");
	printf("Use --insecure only for local development servers with self-signed TLS certificates.\n");
	printf("Agents: run `gw help agents` for guidance and `gw manifest --output json` for machine-readable discovery.\n");
}

int command_help(void)
{
	fputs(GW_HELP_TEXT, stdout);
	return 0;
}

int command_help_agents(void)
{
	fputs(GW_HELP_AGENTS_TEXT, stdout);
	return 0;
}

int command_version(const GwOptions *opts)
{
	if (wants_json(opts)) {
		printf("{\"ok\":true,\"gw\":{\"version\":\"%s\"}}\n", GWRUN_VERSION);
	} else {
		printf("gw %s\n", GWRUN_VERSION);
	}
	return 0;
}

int command_check(const GwOptions *opts)
{
	GwHttpResponse res;
	long start = monotonic_ms();
	int ok = mcp_call(opts, "tools/list", "{}", &res);
	long duration = monotonic_ms() - start;

	if (wants_json(opts)) {
		char *server = json_escape_alloc(opts->server);
		printf("{\"ok\":%s,\"command\":\"check\",\"server\":%s,\"durationMs\":%ld,\"auth\":{\"configured\":%s},\"remote\":{\"reachable\":%s,\"httpStatus\":%ld",
			ok ? "true" : "false",
			server ? server : "\"\"",
			duration,
			auth_header_configured() ? "true" : "false",
			ok ? "true" : "false",
			res.status);
		free(server);
		if (!ok) {
			char *err = json_escape_alloc(res.error[0] ? res.error : "remote request failed");
			printf(",\"error\":{\"message\":%s}", err ? err : "\"\"");
			free(err);
		}
		printf("}}\n");
	} else {
		printf("server: %s\n", opts->server);
		printf("auth: %s\n", auth_header_configured() ? "configured" : "missing");
		printf("remote: %s", ok ? "reachable" : "failed");
		if (!ok && res.error[0]) {
			printf(" (%s)", res.error);
		}
		printf("\n");
	}

	http_response_free(&res);
	return ok ? 0 : 3;
}

int command_tools_list(const GwOptions *opts)
{
	GwHttpResponse res;
	long start = monotonic_ms();
	int ok = mcp_call(opts, "tools/list", "{}", &res);
	long duration = monotonic_ms() - start;
	int code;
	(void)ok;

	if (res.status >= 200 && res.status < 300 && !wants_json(opts)) {
		printf("Server: %s\n", opts->server);
		printf("Duration: %ld ms\n", duration);
		print_tools_list_text(res.body.data ? res.body.data : "");
		code = 0;
	} else {
		code = print_remote_result(opts, "tools.list", &res, duration);
	}

	http_response_free(&res);
	return code;
}

int command_tools_describe(const GwOptions *opts, const char *name)
{
	GwHttpResponse res;
	long start = monotonic_ms();
	int ok = mcp_call(opts, "tools/list", "{}", &res);
	long duration = monotonic_ms() - start;

	if (!ok) {
		int code = print_remote_result(opts, "tools.describe", &res, duration);
		http_response_free(&res);
		return code;
	}

	if (wants_json(opts)) {
		char *tool = json_escape_alloc(name);
		printf("{\"ok\":true,\"command\":\"tools.describe\",\"tool\":%s,\"durationMs\":%ld,\"note\":\"v1 returns tools/list for client-side selection\",\"result\":%s}\n",
			tool ? tool : "\"\"", duration, res.body.data ? res.body.data : "null");
		free(tool);
	} else {
		printf("%s\n", res.body.data ? res.body.data : "");
	}
	http_response_free(&res);
	return 0;
}

int command_resources_list(const GwOptions *opts)
{
	GwHttpResponse res;
	long start = monotonic_ms();
	int ok = mcp_call(opts, "resources/list", "{}", &res);
	long duration = monotonic_ms() - start;
	int code;
	(void)ok;

	if (res.status >= 200 && res.status < 300 && !wants_json(opts)) {
		printf("Server: %s\n", opts->server);
		printf("Duration: %ld ms\n", duration);
		print_resources_list_text(res.body.data ? res.body.data : "");
		code = 0;
	} else {
		code = print_remote_result(opts, "resources.list", &res, duration);
	}

	http_response_free(&res);
	return code;
}

int command_resources_read(const GwOptions *opts, const char *uri)
{
	GwBuffer params;
	GwHttpResponse res;
	char *uri_json;
	long start;
	long duration;
	int ok;
	int code;

	uri_json = json_escape_alloc(uri);
	if (!uri_json) {
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}

	buffer_init(&params);
	ok = buffer_append_cstr(&params, "{\"uri\":") &&
		buffer_append_cstr(&params, uri_json) &&
		buffer_append_cstr(&params, "}");
	free(uri_json);

	if (!ok) {
		buffer_free(&params);
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}

	start = monotonic_ms();
	ok = mcp_call(opts, "resources/read", params.data, &res);
	duration = monotonic_ms() - start;
	buffer_free(&params);
	(void)ok;

	if (res.status >= 200 && res.status < 300 && !wants_json(opts)) {
		print_resource_read_text(res.body.data ? res.body.data : "");
		code = 0;
	} else {
		code = print_remote_result(opts, "resources.read", &res, duration);
	}

	http_response_free(&res);
	return code;
}

static int has_tool_suffix(const char *name, const char *suffix)
{
	size_t name_len = strlen(name);
	size_t suffix_len = strlen(suffix);

	if (name_len == suffix_len && strcmp(name, suffix) == 0) {
		return 1;
	}
	if (name_len <= suffix_len + 1) {
		return 0;
	}
	return name[name_len - suffix_len - 1] == '.' &&
		strcmp(name + name_len - suffix_len, suffix) == 0;
}

static char *find_tool_name_by_suffix(const char *json, const char *suffix)
{
	const char *tools = strstr(json, "\"tools\"");
	const char *scan;

	if (!tools) {
		return NULL;
	}
	scan = strchr(tools, '[');
	if (!scan) {
		return NULL;
	}
	scan++;

	while (*scan) {
		const char *object_start;
		const char *object_end;
		const char *name_key;
		const char *name_colon;
		const char *name_quote;
		char *name;

		while (*scan && *scan != '{' && *scan != ']') {
			scan++;
		}
		if (*scan == ']') {
			break;
		}
		if (*scan != '{') {
			break;
		}

		object_start = scan;
		object_end = find_matching_object_end(object_start);
		if (!object_end) {
			break;
		}

		name_key = find_last_range(object_start, object_end, "\"name\"");
		name_colon = name_key ? strchr(name_key, ':') : NULL;
		name_quote = name_colon ? strchr(name_colon, '"') : NULL;
		if (name_quote && name_quote < object_end) {
			name = json_string_dup(name_quote, NULL);
			if (name && has_tool_suffix(name, suffix)) {
				return name;
			}
			free(name);
		}
		scan = object_end;
	}
	return NULL;
}

static int call_tool_suffix_response(const GwOptions *opts, const char *command, const char *suffix, const char *args_json, GwHttpResponse *call_res, long *duration_out)
{
	GwHttpResponse list_res;
	GwBuffer params;
	char *tool_name;
	char *tool_json;
	long start = monotonic_ms();
	long duration;
	int ok;
	int code;

	ok = mcp_call(opts, "tools/list", "{}", &list_res);
	if (!ok) {
		duration = monotonic_ms() - start;
		if (duration_out) {
			*duration_out = duration;
		}
		code = print_remote_result(opts, command, &list_res, duration);
		http_response_free(&list_res);
		return code;
	}

	tool_name = find_tool_name_by_suffix(list_res.body.data ? list_res.body.data : "", suffix);
	http_response_free(&list_res);
	if (!tool_name) {
		char *err = json_escape_alloc(suffix);
		if (wants_json(opts)) {
			printf("{\"ok\":false,\"command\":\"%s\",\"status\":\"tool_not_found\",\"error\":{\"message\":\"server does not advertise a %s tool\",\"toolSuffix\":%s}}\n",
				command, suffix, err ? err : "\"\"");
		} else {
			fprintf(stderr, "gw: server does not advertise a %s tool\n", suffix);
		}
		free(err);
		return 3;
	}

	tool_json = json_escape_alloc(tool_name);
	free(tool_name);
	if (!tool_json) {
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}

	buffer_init(&params);
	ok = buffer_append_cstr(&params, "{\"name\":") &&
		buffer_append_cstr(&params, tool_json) &&
		buffer_append_cstr(&params, ",\"arguments\":") &&
		buffer_append_cstr(&params, args_json ? args_json : "{}") &&
		buffer_append_cstr(&params, "}");
	free(tool_json);
	if (!ok) {
		buffer_free(&params);
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}

	ok = mcp_call(opts, "tools/call", params.data, call_res);
	duration = monotonic_ms() - start;
	if (duration_out) {
		*duration_out = duration;
	}
	buffer_free(&params);
	(void)ok;
	return 0;
}

static int command_call_tool_suffix(const GwOptions *opts, const char *command, const char *suffix, const char *args_json)
{
	GwHttpResponse call_res;
	long duration = 0;
	int code = call_tool_suffix_response(opts, command, suffix, args_json, &call_res, &duration);

	if (code != 0) {
		return code;
	}
	code = print_remote_result(opts, command, &call_res, duration);
	http_response_free(&call_res);
	return code;
}

int command_program_create(const GwOptions *opts, const char *name)
{
	GwBuffer args;
	GwHttpResponse res;
	char *name_json = json_escape_alloc(name);
	long duration = 0;
	int ok;
	int code;

	if (!name_json) {
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}
	buffer_init(&args);
	ok = buffer_append_cstr(&args, "{\"name\":") &&
		buffer_append_cstr(&args, name_json) &&
		buffer_append_cstr(&args, "}");
	free(name_json);
	if (!ok) {
		buffer_free(&args);
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}
	code = call_tool_suffix_response(opts, "program.create", "program_create", args.data, &res, &duration);
	buffer_free(&args);
	if (code != 0) {
		return code;
	}

	if (res.status >= 200 && res.status < 300 && wants_json(opts)) {
		char *server = json_escape_alloc(opts->server);
		char *program_id_json = json_object_path_dup(res.body.data ? res.body.data : "", "structuredContent", "programID");
		char *program_id = NULL;
		char *bare_id = NULL;
		if (program_id_json && program_id_json[0] == '"') {
			program_id = json_string_dup(program_id_json, NULL);
		}
		if (program_id && strncmp(program_id, "/file/", 6) == 0) {
			bare_id = json_escape_alloc(program_id + 6);
		}
		printf("{\"ok\":true,\"command\":\"program.create\",\"server\":%s,\"durationMs\":%ld,\"result\":%s",
			server ? server : "\"\"", duration, res.body.data ? res.body.data : "null");
		if (bare_id) {
			printf(",\"derived\":{\"programIDBare\":%s}", bare_id);
		}
		printf("}\n");
		free(server);
		free(program_id_json);
		free(program_id);
		free(bare_id);
		code = 0;
	} else {
		code = print_remote_result(opts, "program.create", &res, duration);
	}
	http_response_free(&res);
	return code;
}

int command_program_write(const GwOptions *opts, const char *program_id, const char *source)
{
	GwBuffer args;
	char *normalized_id = normalize_program_id_alloc(program_id);
	char *id_json = normalized_id ? json_escape_alloc(normalized_id) : NULL;
	char *source_json = json_escape_alloc(source);
	int ok;
	int code;

	if (!id_json || !source_json) {
		free(normalized_id);
		free(id_json);
		free(source_json);
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}
	buffer_init(&args);
	ok = buffer_append_cstr(&args, "{\"programID\":") &&
		buffer_append_cstr(&args, id_json) &&
		buffer_append_cstr(&args, ",\"source\":") &&
		buffer_append_cstr(&args, source_json) &&
		buffer_append_cstr(&args, "}");
	free(normalized_id);
	free(id_json);
	free(source_json);
	if (!ok) {
		buffer_free(&args);
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}
	code = command_call_tool_suffix(opts, "program.write", "program_write", args.data);
	buffer_free(&args);
	return code;
}

int command_program_read(const GwOptions *opts, const char *program_id)
{
	GwBuffer args;
	GwHttpResponse res;
	char *normalized_id = normalize_program_id_alloc(program_id);
	char *id_json = normalized_id ? json_escape_alloc(normalized_id) : NULL;
	long duration = 0;
	int ok;
	int code;

	if (!id_json) {
		free(normalized_id);
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}
	buffer_init(&args);
	ok = buffer_append_cstr(&args, "{\"programID\":") &&
		buffer_append_cstr(&args, id_json) &&
		buffer_append_cstr(&args, "}");
	free(normalized_id);
	free(id_json);
	if (!ok) {
		buffer_free(&args);
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}
	code = call_tool_suffix_response(opts, "program.read", "program_read", args.data, &res, &duration);
	buffer_free(&args);
	if (code != 0) {
		return code;
	}

	if (res.status >= 200 && res.status < 300 && !wants_json(opts)) {
		char *source = program_read_source_dup(res.body.data ? res.body.data : "");
		if (source) {
			printf("%s", source);
			if (source[0] && source[strlen(source) - 1] != '\n') {
				printf("\n");
			}
			free(source);
			code = 0;
		} else {
			code = print_remote_result(opts, "program.read", &res, duration);
		}
	} else {
		code = print_remote_result(opts, "program.read", &res, duration);
	}
	http_response_free(&res);
	return code;
}

int command_program_compile(const GwOptions *opts, const char *program_id)
{
	GwBuffer args;
	char *normalized_id = normalize_program_id_alloc(program_id);
	char *id_json = normalized_id ? json_escape_alloc(normalized_id) : NULL;
	int ok;
	int code;

	if (!id_json) {
		free(normalized_id);
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}
	buffer_init(&args);
	ok = buffer_append_cstr(&args, "{\"programID\":") &&
		buffer_append_cstr(&args, id_json) &&
		buffer_append_cstr(&args, "}");
	free(normalized_id);
	free(id_json);
	if (!ok) {
		buffer_free(&args);
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}
	code = command_call_tool_suffix(opts, "program.compile", "program_compile", args.data);
	buffer_free(&args);
	return code;
}

int command_program_run(const GwOptions *opts, const char *program_id, const char *args_json)
{
	GwBuffer args;
	char *normalized_id = normalize_program_id_alloc(program_id);
	char *id_json = normalized_id ? json_escape_alloc(normalized_id) : NULL;
	int ok;
	int code;

	if (!id_json) {
		free(normalized_id);
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}
	buffer_init(&args);
	ok = buffer_append_cstr(&args, "{\"programID\":") &&
		buffer_append_cstr(&args, id_json) &&
		buffer_append_cstr(&args, ",\"args\":") &&
		buffer_append_cstr(&args, args_json ? args_json : "{}") &&
		buffer_append_cstr(&args, "}");
	free(normalized_id);
	free(id_json);
	if (!ok) {
		buffer_free(&args);
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}
	code = command_call_tool_suffix(opts, "program.run", "program_run", args.data);
	buffer_free(&args);
	return code;
}

int command_process_list(const GwOptions *opts)
{
	return command_call_tool_suffix(opts, "process.list", "process_list", "{}");
}

int command_process_kill(const GwOptions *opts, const char *process_id)
{
	GwBuffer args;
	char *id_json = json_escape_alloc(process_id);
	int ok;
	int code;

	if (!id_json) {
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}
	buffer_init(&args);
	ok = buffer_append_cstr(&args, "{\"processID\":") &&
		buffer_append_cstr(&args, id_json) &&
		buffer_append_cstr(&args, "}");
	free(id_json);
	if (!ok) {
		buffer_free(&args);
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}
	code = command_call_tool_suffix(opts, "process.kill", "process_kill", args.data);
	buffer_free(&args);
	return code;
}

int command_call(const GwOptions *opts, const char *tool_name, const char *args_json)
{
	GwBuffer params;
	GwHttpResponse res;
	char *tool_json;
	long start;
	long duration;
	int ok;

	tool_json = json_escape_alloc(tool_name);
	if (!tool_json) {
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}

	buffer_init(&params);
	ok = buffer_append_cstr(&params, "{\"name\":") &&
		buffer_append_cstr(&params, tool_json) &&
		buffer_append_cstr(&params, ",\"arguments\":") &&
		buffer_append_cstr(&params, args_json ? args_json : "{}") &&
		buffer_append_cstr(&params, "}");
	free(tool_json);

	if (!ok) {
		buffer_free(&params);
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}

	start = monotonic_ms();
	ok = mcp_call(opts, "tools/call", params.data, &res);
	duration = monotonic_ms() - start;
	buffer_free(&params);

	(void)ok;
	ok = print_remote_result(opts, "call", &res, duration);
	http_response_free(&res);
	return ok;
}

int command_process_start(const GwOptions *opts, const char *program, const char *args_json)
{
	GwBuffer body;
	GwHttpResponse res;
	char *normalized_program = normalize_process_program_alloc(program);
	long start;
	long duration;
	int ok;
	int invalid;

	if (!normalized_program) {
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}

	invalid = reject_invalid_process_program_ref(opts, "process.start", normalized_program);
	if (invalid) {
		free(normalized_program);
		return invalid;
	}

	if (!build_process_start_body(&body, normalized_program, args_json)) {
		free(normalized_program);
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}
	free(normalized_program);

	start = monotonic_ms();
	ok = process_post(opts, "processStart", body.data, &res);
	duration = monotonic_ms() - start;
	buffer_free(&body);
	(void)ok;

	ok = print_remote_result(opts, "process.start", &res, duration);
	http_response_free(&res);
	return ok;
}

int command_process_view(const GwOptions *opts, const char *process_id, const char *seq_json)
{
	GwBuffer body;
	GwHttpResponse res;
	long start;
	long duration;
	int ok;

	if (!build_process_view_body(&body, process_id, seq_json ? seq_json : "0")) {
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}

	start = monotonic_ms();
	ok = process_post(opts, "processView", body.data, &res);
	duration = monotonic_ms() - start;
	buffer_free(&body);
	(void)ok;

	ok = print_remote_result(opts, "process.view", &res, duration);
	http_response_free(&res);
	return ok;
}

int command_process_input(const GwOptions *opts, const char *process_id, const char *input_text, const char *seq_json)
{
	GwBuffer body;
	GwHttpResponse res;
	long start;
	long duration;
	int ok;

	if (!build_process_input_body(&body, process_id, input_text, seq_json ? seq_json : "0")) {
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}

	start = monotonic_ms();
	ok = process_post(opts, "processInput", body.data, &res);
	duration = monotonic_ms() - start;
	buffer_free(&body);
	(void)ok;

	ok = print_remote_result(opts, "process.input", &res, duration);
	http_response_free(&res);
	return ok;
}

int command_process_attach(const GwOptions *opts, const char *program, const char *args_json)
{
	GwBuffer body;
	GwHttpResponse res;
	char *process_id = NULL;
	char *normalized_program = normalize_process_program_alloc(program);
	char *seq = NULL;
	int exit_code = 0;
	int invalid;

	if (!normalized_program) {
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}

	invalid = reject_invalid_process_program_ref(opts, "run", normalized_program);
	if (invalid) {
		free(normalized_program);
		return invalid;
	}

	if (!build_process_start_body(&body, normalized_program, args_json)) {
		free(normalized_program);
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}
	free(normalized_program);
	if (!process_post(opts, "processStart", body.data, &res)) {
		buffer_free(&body);
		exit_code = print_remote_result(opts, "process.start", &res, 0);
		http_response_free(&res);
		return exit_code;
	}
	buffer_free(&body);

	process_id = json_string_dup(res.body.data, NULL);
	if (!process_id) {
		fprintf(stderr, "gw: processStart did not return a process ID string\n");
		http_response_free(&res);
		return 3;
	}
	http_response_free(&res);

	seq = (char *)malloc(2);
	if (!seq) {
		free(process_id);
		fprintf(stderr, "gw: out of memory\n");
		return 4;
	}
	strcpy(seq, "0");

	for (;;) {
		char *next_seq;
		char *status_json;
		char *status = NULL;
		char *input_seq = NULL;
		char *prompt = NULL;

		if (!build_process_view_body(&body, process_id, seq)) {
			exit_code = 4;
			break;
		}
		if (!process_post(opts, "processView", body.data, &res)) {
			buffer_free(&body);
			exit_code = print_remote_result(opts, "process.view", &res, 0);
			http_response_free(&res);
			break;
		}
		buffer_free(&body);

		print_json_string_array_member(res.body.data ? res.body.data : "", "CON");

		next_seq = json_object_member_dup(res.body.data ? res.body.data : "", "$Seq");
		if (next_seq) {
			free(seq);
			seq = next_seq;
		}

		status_json = json_object_member_dup(res.body.data ? res.body.data : "", "$Status");
		if (status_json && status_json[0] == '"') {
			status = json_string_dup(status_json, NULL);
		}
		free(status_json);

		input_seq = json_object_path_dup(res.body.data ? res.body.data : "", "INPUT", "seq");
		prompt = json_object_path_dup(res.body.data ? res.body.data : "", "INPUT", "prompt");

		if (input_seq) {
			char line[4096];
			char *prompt_text = NULL;
			if (prompt && prompt[0] == '"') {
				prompt_text = json_string_dup(prompt, NULL);
			}
			if (prompt_text && prompt_text[0]) {
				fprintf(stderr, "%s", prompt_text);
				if (prompt_text[strlen(prompt_text) - 1] != ' ') {
					fprintf(stderr, " ");
				}
			}
			fflush(stderr);
			if (!fgets(line, sizeof(line), stdin)) {
				free(prompt_text);
				free(prompt);
				free(input_seq);
				free(status);
				http_response_free(&res);
				exit_code = 1;
				break;
			}
			line[strcspn(line, "\r\n")] = '\0';
			if (!build_process_input_body(&body, process_id, line, input_seq)) {
				free(prompt_text);
				free(prompt);
				free(input_seq);
				free(status);
				http_response_free(&res);
				exit_code = 4;
				break;
			}
			http_response_free(&res);
			if (!process_post(opts, "processInput", body.data, &res)) {
				buffer_free(&body);
				free(prompt_text);
				free(prompt);
				free(input_seq);
				free(status);
				exit_code = print_remote_result(opts, "process.input", &res, 0);
				http_response_free(&res);
				break;
			}
			buffer_free(&body);
			next_seq = json_object_member_dup(res.body.data ? res.body.data : "", "$Seq");
			if (next_seq) {
				free(seq);
				seq = next_seq;
			}
			http_response_free(&res);
			free(prompt_text);
			free(prompt);
			free(input_seq);
			free(status);
			continue;
		}

		free(prompt);
		free(input_seq);
		if (status && strcmp(status, "terminated") == 0) {
			free(status);
			http_response_free(&res);
			break;
		}
		free(status);
		http_response_free(&res);
	}

	free(seq);
	free(process_id);
	return exit_code;
}

int command_agent_manifest(const GwOptions *opts)
{
	GwHttpResponse res;
	long start = monotonic_ms();
	int ok = mcp_call_no_prompt(opts, "tools/list", "{}", &res);
	long duration = monotonic_ms() - start;

	if (wants_json(opts)) {
		char *server = json_escape_alloc(opts->server);
		printf("{\"ok\":%s,\"command\":\"manifest\",\"gw\":{\"version\":\"%s\",\"protocolVersion\":\"1\"},",
			ok ? "true" : "false", GWRUN_VERSION);
		printf("\"server\":{\"url\":%s,\"reachable\":%s,\"auth\":{\"configured\":%s,\"source\":\"GRIDWHALE_AUTH_HEADER\",\"type\":\"Basic\",\"redacted\":\"Basic ***\",\"interactiveFallback\":true,\"agentGuidance\":\"Set GRIDWHALE_AUTH_HEADER to a full Basic auth value before invoking server commands. Agents should avoid interactive prompts.\"}},",
			server ? server : "\"\"",
			ok ? "true" : "false",
			auth_header_configured() ? "true" : "false");
		printf("\"auth\":{\"configured\":%s,\"preferredEnv\":\"GRIDWHALE_AUTH_HEADER\",\"interactiveFallback\":true,\"agentGuidance\":\"Set GRIDWHALE_AUTH_HEADER to a full Basic auth value. If local GridWhale MCP config is available, load the Authorization header from that config without printing it.\"},",
			auth_header_configured() ? "true" : "false");
		printf("\"programIDs\":{\"serverCreateResult\":\"/file/PROGRAMID\",\"programWrappersAccept\":[\"/file/PROGRAMID\",\"PROGRAMID\"],\"processStartAccept\":[\"PROGRAMID.entryPoint\",\"/file/PROGRAMID\",\"PROGRAMID\"]},");
		printf("\"capabilities\":{\"localRun\":false,\"remoteTools\":true,\"toolDiscovery\":true,\"toolInvocation\":true,\"resources\":true,\"resourceRead\":true,\"programWrappers\":true,\"remoteProcess\":true,\"interactiveIO\":true,\"jsonOutput\":true,\"jsonlOutput\":false,\"schemas\":true,\"cache\":false},");
		printf("\"defaults\":{\"output\":\"json\",\"timeout\":\"30s\"},");
		printf("\"commands\":[");
		printf("{\"name\":\"help\",\"argv\":[\"gw\",\"help\"]},");
		printf("{\"name\":\"help.agents\",\"argv\":[\"gw\",\"help\",\"agents\"]},");
		printf("{\"name\":\"tools.list\",\"argv\":[\"gw\",\"tools\",\"list\",\"--output\",\"json\"]},");
		printf("{\"name\":\"tools.describe\",\"argv\":[\"gw\",\"tools\",\"describe\",\"<name>\",\"--output\",\"json\"]},");
		printf("{\"name\":\"tools.call\",\"argv\":[\"gw\",\"tools\",\"call\",\"<name>\",\"--json-file\",\"<path>\",\"--output\",\"json\"]},");
		printf("{\"name\":\"call\",\"argv\":[\"gw\",\"call\",\"<name>\",\"--json-file\",\"<path>\",\"--output\",\"json\"]},");
		printf("{\"name\":\"resources.list\",\"argv\":[\"gw\",\"resources\",\"list\",\"--output\",\"json\"]},");
		printf("{\"name\":\"resources.read\",\"argv\":[\"gw\",\"resources\",\"read\",\"<uri>\",\"--output\",\"json\"]},");
		printf("{\"name\":\"program.create\",\"argv\":[\"gw\",\"program\",\"create\",\"--name\",\"<name>\",\"--output\",\"json\"]},");
		printf("{\"name\":\"program.write\",\"argv\":[\"gw\",\"program\",\"write\",\"<programID>\",\"--file\",\"<path>\",\"--output\",\"json\"]},");
		printf("{\"name\":\"program.read\",\"argv\":[\"gw\",\"program\",\"read\",\"<programID>\",\"--output\",\"json\"]},");
		printf("{\"name\":\"program.compile\",\"argv\":[\"gw\",\"program\",\"compile\",\"<programID>\",\"--output\",\"json\"]},");
		printf("{\"name\":\"program.run\",\"argv\":[\"gw\",\"program\",\"run\",\"<programID>\",\"--json-file\",\"<path>\",\"--output\",\"json\"]},");
		printf("{\"name\":\"process.start\",\"argv\":[\"gw\",\"process\",\"start\",\"<programID-or-entryPoint>\",\"--json-file\",\"<path>\",\"--output\",\"json\"]},");
		printf("{\"name\":\"process.view\",\"argv\":[\"gw\",\"process\",\"view\",\"<processID>\",\"--seq-file\",\"<path>\",\"--output\",\"json\"]},");
		printf("{\"name\":\"process.input\",\"argv\":[\"gw\",\"process\",\"input\",\"<processID>\",\"--text\",\"<text>\",\"--seq-file\",\"<path>\",\"--output\",\"json\"]},");
		printf("{\"name\":\"run\",\"argv\":[\"gw\",\"run\",\"<programID-or-entryPoint>\",\"--json-file\",\"<path>\"]}");
		printf("],\"durationMs\":%ld,\"toolDiscoveryResult\":", duration);
		if (ok) {
			printf("%s", res.body.data ? res.body.data : "null");
		} else {
			printf("null");
		}
		if (!ok) {
			char *err = json_escape_alloc(res.error[0] ? res.error : "remote request failed");
			printf(",\"error\":{\"message\":%s}", err ? err : "\"\"");
			free(err);
		}
		printf("}\n");
		free(server);
	} else {
		printf("gw %s\n", GWRUN_VERSION);
		printf("server: %s\n", opts->server);
		printf("auth: %s\n", auth_header_configured() ? "configured" : "missing");
		printf("remote tools: %s\n", ok ? "available" : "unavailable");
		printf("agent discovery: gw manifest --output json\n");
	}

	http_response_free(&res);
	return ok ? 0 : 3;
}
