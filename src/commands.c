#include "gwrun.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int wants_json(const GwOptions *opts)
{
	return strcmp(opts->output, "json") == 0;
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
		printf("{\"ok\":false,\"command\":\"%s\",\"status\":\"remote_error\",\"httpStatus\":%ld,\"durationMs\":%ld,\"error\":{\"message\":%s}}\n",
			command, res->status, duration_ms, err ? err : "\"\"");
		free(err);
	} else {
		fprintf(stderr, "gwrun: remote request failed");
		if (res->status) {
			fprintf(stderr, " (HTTP %ld)", res->status);
		}
		if (res->error[0]) {
			fprintf(stderr, ": %s", res->error);
		}
		fprintf(stderr, "\n");
	}
	return 3;
}

void print_usage(void)
{
	printf("GridWhale Runner %s\n\n", GWRUN_VERSION);
	printf("Usage:\n");
	printf("  gwrun [--server URL] [--output text|json] version\n");
	printf("  gwrun [--server URL] [--output text|json] check\n");
	printf("  gwrun [--server URL] [--output text|json] agent manifest\n");
	printf("  gwrun [--server URL] [--output text|json] tools list\n");
	printf("  gwrun [--server URL] [--output text|json] tools describe <name>\n");
	printf("  gwrun [--server URL] [--output text|json] call <name> --json <object>\n");
	printf("  gwrun [--server URL] [--output text|json] call <name> --json-file <path>\n\n");
	printf("Agents: run `gwrun agent manifest --output json` for discovery.\n");
}

int command_version(const GwOptions *opts)
{
	if (wants_json(opts)) {
		printf("{\"ok\":true,\"gwrun\":{\"version\":\"%s\"}}\n", GWRUN_VERSION);
	} else {
		printf("gwrun %s\n", GWRUN_VERSION);
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
		fprintf(stderr, "gwrun: out of memory\n");
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
		fprintf(stderr, "gwrun: out of memory\n");
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

int command_agent_manifest(const GwOptions *opts)
{
	GwHttpResponse res;
	long start = monotonic_ms();
	int ok = mcp_call(opts, "tools/list", "{}", &res);
	long duration = monotonic_ms() - start;

	if (wants_json(opts)) {
		char *server = json_escape_alloc(opts->server);
		printf("{\"ok\":%s,\"command\":\"agent.manifest\",\"gwrun\":{\"version\":\"%s\",\"protocolVersion\":\"1\"},",
			ok ? "true" : "false", GWRUN_VERSION);
		printf("\"server\":{\"url\":%s,\"reachable\":%s,\"auth\":{\"configured\":%s,\"source\":\"GRIDWHALE_AUTH_HEADER\",\"type\":\"Basic\",\"redacted\":\"Basic ***\"}},",
			server ? server : "\"\"",
			ok ? "true" : "false",
			auth_header_configured() ? "true" : "false");
		printf("\"capabilities\":{\"localRun\":false,\"remoteTools\":true,\"toolDiscovery\":true,\"toolInvocation\":true,\"jsonOutput\":true,\"jsonlOutput\":false,\"schemas\":true,\"cache\":false},");
		printf("\"defaults\":{\"output\":\"json\",\"timeout\":\"30s\"},");
		printf("\"commands\":[");
		printf("{\"name\":\"tools.list\",\"argv\":[\"gwrun\",\"tools\",\"list\",\"--output\",\"json\"]},");
		printf("{\"name\":\"tools.describe\",\"argv\":[\"gwrun\",\"tools\",\"describe\",\"<name>\",\"--output\",\"json\"]},");
		printf("{\"name\":\"call\",\"argv\":[\"gwrun\",\"call\",\"<name>\",\"--json-file\",\"<path>\",\"--output\",\"json\"]}");
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
		printf("gwrun %s\n", GWRUN_VERSION);
		printf("server: %s\n", opts->server);
		printf("auth: %s\n", auth_header_configured() ? "configured" : "missing");
		printf("remote tools: %s\n", ok ? "available" : "unavailable");
		printf("agent discovery: gwrun agent manifest --output json\n");
	}

	http_response_free(&res);
	return ok ? 0 : 3;
}
