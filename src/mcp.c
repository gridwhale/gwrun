#include "gwrun.h"

#include <stdio.h>
#include <stdlib.h>

int mcp_call(const GwOptions *opts, const char *method, const char *params_json, GwHttpResponse *response)
{
	static int next_id = 1;
	GwBuffer req;
	char id_text[32];
	char *method_json;
	int ok;

	method_json = json_escape_alloc(method);
	if (!method_json) {
		response->status = 0;
		response->error[0] = '\0';
		snprintf(response->error, sizeof(response->error), "out of memory escaping method");
		buffer_init(&response->body);
		return 0;
	}

	snprintf(id_text, sizeof(id_text), "%d", next_id++);
	buffer_init(&req);
	ok = buffer_append_cstr(&req, "{\"jsonrpc\":\"2.0\",\"id\":") &&
		buffer_append_cstr(&req, id_text) &&
		buffer_append_cstr(&req, ",\"method\":") &&
		buffer_append_cstr(&req, method_json) &&
		buffer_append_cstr(&req, ",\"params\":") &&
		buffer_append_cstr(&req, params_json ? params_json : "{}") &&
		buffer_append_cstr(&req, "}");

	free(method_json);
	if (!ok) {
		buffer_free(&req);
		response->status = 0;
		response->error[0] = '\0';
		snprintf(response->error, sizeof(response->error), "out of memory building request");
		buffer_init(&response->body);
		return 0;
	}

	ok = http_post_json(opts, req.data, response);
	buffer_free(&req);
	return ok;
}

int mcp_call_no_prompt(const GwOptions *opts, const char *method, const char *params_json, GwHttpResponse *response)
{
	GwOptions no_prompt_opts = *opts;
	no_prompt_opts.auth_prompt = 0;
	return mcp_call(&no_prompt_opts, method, params_json, response);
}
