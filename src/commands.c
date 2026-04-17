#include "gwrun.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int wants_json(const GwOptions *opts)
{
	return strcmp(opts->output, "json") == 0;
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
			getenv("GRIDWHALE_AUTH_HEADER") ? "true" : "false",
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
		printf("auth: %s\n", getenv("GRIDWHALE_AUTH_HEADER") ? "configured" : "missing");
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
	int code = print_remote_result(opts, "tools.list", &res, duration);
	(void)ok;
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
			getenv("GRIDWHALE_AUTH_HEADER") ? "true" : "false");
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
		printf("auth: %s\n", getenv("GRIDWHALE_AUTH_HEADER") ? "configured" : "missing");
		printf("remote tools: %s\n", ok ? "available" : "unavailable");
		printf("agent discovery: gwrun agent manifest --output json\n");
	}

	http_response_free(&res);
	return ok ? 0 : 3;
}
