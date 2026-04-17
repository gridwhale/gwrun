#ifndef GWRUN_H
#define GWRUN_H

#include <stddef.h>

#define GWRUN_VERSION "0.1.0"
#define GWRUN_DEFAULT_SERVER "https://dev.gridwhale.io/mcp/"

typedef struct GwBuffer {
	char *data;
	size_t len;
	size_t cap;
} GwBuffer;

typedef struct GwOptions {
	const char *server;
	const char *output;
	long timeout_ms;
	int include_raw;
} GwOptions;

typedef struct GwHttpResponse {
	long status;
	GwBuffer body;
	char error[512];
} GwHttpResponse;

void buffer_init(GwBuffer *buf);
void buffer_free(GwBuffer *buf);
int buffer_append(GwBuffer *buf, const char *data, size_t len);
int buffer_append_cstr(GwBuffer *buf, const char *s);
char *json_escape_alloc(const char *s);
char *read_file_alloc(const char *path, char *error, size_t error_len);
long monotonic_ms(void);

int http_post_json(const GwOptions *opts, const char *body, GwHttpResponse *response);
void http_response_free(GwHttpResponse *response);

int mcp_call(const GwOptions *opts, const char *method, const char *params_json, GwHttpResponse *response);

void print_usage(void);
int command_version(const GwOptions *opts);
int command_check(const GwOptions *opts);
int command_tools_list(const GwOptions *opts);
int command_tools_describe(const GwOptions *opts, const char *name);
int command_call(const GwOptions *opts, const char *tool_name, const char *args_json);
int command_agent_manifest(const GwOptions *opts);

#endif
