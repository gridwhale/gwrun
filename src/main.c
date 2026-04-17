#include "gwrun.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_option(const char *s, const char *long_name)
{
	return strcmp(s, long_name) == 0;
}

static int parse_global_option(GwOptions *opts, int argc, char **argv, int *i)
{
	if (is_option(argv[*i], "--server")) {
		if (*i + 1 >= argc) {
			fprintf(stderr, "gwrun: --server requires a value\n");
			return 0;
		}
		opts->server = argv[++(*i)];
		return 1;
	}
	if (is_option(argv[*i], "--output")) {
		if (*i + 1 >= argc) {
			fprintf(stderr, "gwrun: --output requires a value\n");
			return 0;
		}
		opts->output = argv[++(*i)];
		if (strcmp(opts->output, "text") != 0 && strcmp(opts->output, "json") != 0) {
			fprintf(stderr, "gwrun: unsupported output: %s\n", opts->output);
			return 0;
		}
		return 1;
	}
	if (is_option(argv[*i], "--timeout")) {
		if (*i + 1 >= argc) {
			fprintf(stderr, "gwrun: --timeout requires milliseconds for v1\n");
			return 0;
		}
		opts->timeout_ms = atol(argv[++(*i)]);
		if (opts->timeout_ms <= 0) {
			fprintf(stderr, "gwrun: --timeout must be a positive millisecond value in v1\n");
			return 0;
		}
		return 1;
	}
	return 0;
}

static int dispatch(int argc, char **argv)
{
	GwOptions opts;
	char **cmdv;
	int cmdc = 0;
	int i;

	opts.server = GWRUN_DEFAULT_SERVER;
	opts.output = "text";
	opts.timeout_ms = 30000;
	opts.include_raw = 0;

	cmdv = (char **)calloc((size_t)argc, sizeof(char *));
	if (!cmdv) {
		fprintf(stderr, "gwrun: out of memory\n");
		return 4;
	}

	for (i = 1; i < argc; i++) {
		if (is_option(argv[i], "--help")) {
			print_usage();
			free(cmdv);
			return 0;
		}
		if (is_option(argv[i], "--server") ||
			is_option(argv[i], "--output") ||
			is_option(argv[i], "--timeout")) {
			if (!parse_global_option(&opts, argc, argv, &i)) {
				free(cmdv);
				return 2;
			}
		} else {
			cmdv[cmdc++] = argv[i];
		}
	}

	if (cmdc == 0) {
		print_usage();
		free(cmdv);
		return 0;
	}

	if (strcmp(cmdv[0], "version") == 0) {
		int code = command_version(&opts);
		free(cmdv);
		return code;
	}

	if (strcmp(cmdv[0], "check") == 0) {
		int code = command_check(&opts);
		free(cmdv);
		return code;
	}

	if (strcmp(cmdv[0], "agent") == 0) {
		if (cmdc > 1 && strcmp(cmdv[1], "manifest") == 0) {
			int code = command_agent_manifest(&opts);
			free(cmdv);
			return code;
		}
		fprintf(stderr, "gwrun: expected `agent manifest`\n");
		free(cmdv);
		return 2;
	}

	if (strcmp(cmdv[0], "tools") == 0) {
		if (cmdc < 2) {
			fprintf(stderr, "gwrun: expected tools subcommand\n");
			free(cmdv);
			return 2;
		}
		if (strcmp(cmdv[1], "list") == 0) {
			int code = command_tools_list(&opts);
			free(cmdv);
			return code;
		}
		if (strcmp(cmdv[1], "describe") == 0) {
			if (cmdc < 3) {
				fprintf(stderr, "gwrun: tools describe requires a tool name\n");
				free(cmdv);
				return 2;
			}
			{
				int code = command_tools_describe(&opts, cmdv[2]);
				free(cmdv);
				return code;
			}
		}
		fprintf(stderr, "gwrun: unknown tools subcommand: %s\n", cmdv[1]);
		free(cmdv);
		return 2;
	}

	if (strcmp(cmdv[0], "call") == 0) {
		const char *tool_name;
		const char *json = NULL;
		char *json_file_data = NULL;
		char error[256];
		int j;
		int code;

		if (cmdc < 2) {
			fprintf(stderr, "gwrun: call requires a tool name\n");
			free(cmdv);
			return 2;
		}
		tool_name = cmdv[1];
		for (j = 2; j < cmdc; j++) {
			if (strcmp(cmdv[j], "--json") == 0) {
				if (j + 1 >= cmdc) {
					fprintf(stderr, "gwrun: --json requires a value\n");
					free(json_file_data);
					free(cmdv);
					return 2;
				}
				json = cmdv[++j];
			} else if (strcmp(cmdv[j], "--json-file") == 0) {
				if (j + 1 >= cmdc) {
					fprintf(stderr, "gwrun: --json-file requires a path\n");
					free(json_file_data);
					free(cmdv);
					return 2;
				}
				json_file_data = read_file_alloc(cmdv[++j], error, sizeof(error));
				if (!json_file_data) {
					fprintf(stderr, "gwrun: %s\n", error);
					free(cmdv);
					return 2;
				}
				json = json_file_data;
			} else {
				fprintf(stderr, "gwrun: unknown call option: %s\n", cmdv[j]);
				free(json_file_data);
				free(cmdv);
				return 2;
			}
		}

		if (!json) {
			json = "{}";
		}

		code = command_call(&opts, tool_name, json);
		free(json_file_data);
		free(cmdv);
		return code;
	}

	fprintf(stderr, "gwrun: unknown command: %s\n", cmdv[0]);
	free(cmdv);
	return 2;
}

int main(int argc, char **argv)
{
	return dispatch(argc, argv);
}
