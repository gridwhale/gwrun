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
			fprintf(stderr, "gw: --server requires a value\n");
			return 0;
		}
		opts->server = argv[++(*i)];
		return 1;
	}
	if (is_option(argv[*i], "--output")) {
		if (*i + 1 >= argc) {
			fprintf(stderr, "gw: --output requires a value\n");
			return 0;
		}
		opts->output = argv[++(*i)];
		if (strcmp(opts->output, "text") != 0 && strcmp(opts->output, "json") != 0) {
			fprintf(stderr, "gw: unsupported output: %s\n", opts->output);
			return 0;
		}
		return 1;
	}
	if (is_option(argv[*i], "--timeout")) {
		if (*i + 1 >= argc) {
			fprintf(stderr, "gw: --timeout requires milliseconds for v1\n");
			return 0;
		}
		opts->timeout_ms = atol(argv[++(*i)]);
		if (opts->timeout_ms <= 0) {
			fprintf(stderr, "gw: --timeout must be a positive millisecond value in v1\n");
			return 0;
		}
		return 1;
	}
	if (is_option(argv[*i], "--insecure")) {
		opts->insecure_tls = 1;
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
	opts.auth_prompt = 1;
	opts.insecure_tls = 0;

	cmdv = (char **)calloc((size_t)argc, sizeof(char *));
	if (!cmdv) {
		fprintf(stderr, "gw: out of memory\n");
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
			is_option(argv[i], "--timeout") ||
			is_option(argv[i], "--insecure")) {
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

	if (strcmp(cmdv[0], "help") == 0) {
		int code;
		if (cmdc == 1) {
			code = command_help();
		} else if (cmdc == 2 && strcmp(cmdv[1], "agents") == 0) {
			code = command_help_agents();
		} else {
			fprintf(stderr, "gw: expected `help` or `help agents`\n");
			free(cmdv);
			return 2;
		}
		free(cmdv);
		return code;
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

	if (strcmp(cmdv[0], "manifest") == 0) {
		int code = command_agent_manifest(&opts);
		free(cmdv);
		return code;
	}

	if (strcmp(cmdv[0], "agent") == 0) {
		if (cmdc > 1 && strcmp(cmdv[1], "manifest") == 0) {
			int code = command_agent_manifest(&opts);
			free(cmdv);
			return code;
		}
		fprintf(stderr, "gw: expected `manifest`\n");
		free(cmdv);
		return 2;
	}

	if (strcmp(cmdv[0], "tools") == 0) {
		if (cmdc < 2) {
			fprintf(stderr, "gw: expected tools subcommand\n");
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
				fprintf(stderr, "gw: tools describe requires a tool name\n");
				free(cmdv);
				return 2;
			}
			{
				int code = command_tools_describe(&opts, cmdv[2]);
				free(cmdv);
				return code;
			}
		}
		if (strcmp(cmdv[1], "call") == 0) {
			const char *tool_name;
			const char *json = NULL;
			char *json_file_data = NULL;
			char error[256];
			int j;
			int code;

			if (cmdc < 3) {
				fprintf(stderr, "gw: tools call requires a tool name\n");
				free(cmdv);
				return 2;
			}
			tool_name = cmdv[2];
			for (j = 3; j < cmdc; j++) {
				if (strcmp(cmdv[j], "--json") == 0) {
					if (j + 1 >= cmdc) {
						fprintf(stderr, "gw: --json requires a value\n");
						free(json_file_data);
						free(cmdv);
						return 2;
					}
					json = cmdv[++j];
				} else if (strcmp(cmdv[j], "--json-file") == 0) {
					if (j + 1 >= cmdc) {
						fprintf(stderr, "gw: --json-file requires a path\n");
						free(json_file_data);
						free(cmdv);
						return 2;
					}
					json_file_data = read_json_file_alloc(cmdv[++j], error, sizeof(error));
					if (!json_file_data) {
						fprintf(stderr, "gw: %s\n", error);
						free(cmdv);
						return 2;
					}
					json = json_file_data;
				} else {
					fprintf(stderr, "gw: unknown tools call option: %s\n", cmdv[j]);
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
		fprintf(stderr, "gw: unknown tools subcommand: %s\n", cmdv[1]);
		free(cmdv);
		return 2;
	}

	if (strcmp(cmdv[0], "resources") == 0) {
		if (cmdc < 2) {
			fprintf(stderr, "gw: expected resources subcommand\n");
			free(cmdv);
			return 2;
		}
		if (strcmp(cmdv[1], "list") == 0) {
			int code = command_resources_list(&opts);
			free(cmdv);
			return code;
		}
		if (strcmp(cmdv[1], "read") == 0) {
			if (cmdc < 3) {
				fprintf(stderr, "gw: resources read requires a URI\n");
				free(cmdv);
				return 2;
			}
			{
				int code = command_resources_read(&opts, cmdv[2]);
				free(cmdv);
				return code;
			}
		}
		fprintf(stderr, "gw: unknown resources subcommand: %s\n", cmdv[1]);
		free(cmdv);
		return 2;
	}

	if (strcmp(cmdv[0], "program") == 0) {
		int code;
		if (cmdc < 2) {
			fprintf(stderr, "gw: expected program subcommand\n");
			free(cmdv);
			return 2;
		}

		if (strcmp(cmdv[1], "create") == 0) {
			const char *name = NULL;
			int j;
			for (j = 2; j < cmdc; j++) {
				if (strcmp(cmdv[j], "--name") == 0) {
					if (j + 1 >= cmdc) {
						fprintf(stderr, "gw: --name requires a value\n");
						free(cmdv);
						return 2;
					}
					name = cmdv[++j];
				} else {
					fprintf(stderr, "gw: unknown program create option: %s\n", cmdv[j]);
					free(cmdv);
					return 2;
				}
			}
			if (!name) {
				fprintf(stderr, "gw: program create requires --name\n");
				free(cmdv);
				return 2;
			}
			code = command_program_create(&opts, name);
			free(cmdv);
			return code;
		}

		if (strcmp(cmdv[1], "write") == 0) {
			const char *program_id;
			const char *source = NULL;
			char *source_file_data = NULL;
			char error[256];
			int j;

			if (cmdc < 3) {
				fprintf(stderr, "gw: program write requires a program ID\n");
				free(cmdv);
				return 2;
			}
			program_id = cmdv[2];
			for (j = 3; j < cmdc; j++) {
				if (strcmp(cmdv[j], "--file") == 0) {
					if (j + 1 >= cmdc) {
						fprintf(stderr, "gw: --file requires a path\n");
						free(source_file_data);
						free(cmdv);
						return 2;
					}
					source_file_data = read_json_file_alloc(cmdv[++j], error, sizeof(error));
					if (!source_file_data) {
						fprintf(stderr, "gw: %s\n", error);
						free(cmdv);
						return 2;
					}
					source = source_file_data;
				} else if (strcmp(cmdv[j], "--text") == 0) {
					if (j + 1 >= cmdc) {
						fprintf(stderr, "gw: --text requires a value\n");
						free(source_file_data);
						free(cmdv);
						return 2;
					}
					source = cmdv[++j];
				} else {
					fprintf(stderr, "gw: unknown program write option: %s\n", cmdv[j]);
					free(source_file_data);
					free(cmdv);
					return 2;
				}
			}
			if (!source) {
				fprintf(stderr, "gw: program write requires --file or --text\n");
				free(cmdv);
				return 2;
			}
			code = command_program_write(&opts, program_id, source);
			free(source_file_data);
			free(cmdv);
			return code;
		}

		if (strcmp(cmdv[1], "read") == 0 ||
			strcmp(cmdv[1], "compile") == 0) {
			const char *program_id;
			if (cmdc < 3) {
				fprintf(stderr, "gw: program %s requires a program ID\n", cmdv[1]);
				free(cmdv);
				return 2;
			}
			program_id = cmdv[2];
			if (strcmp(cmdv[1], "read") == 0) {
				code = command_program_read(&opts, program_id);
			} else {
				code = command_program_compile(&opts, program_id);
			}
			free(cmdv);
			return code;
		}

		if (strcmp(cmdv[1], "run") == 0) {
			const char *program_id;
			const char *json = "{}";
			char *json_file_data = NULL;
			char error[256];
			int j;

			if (cmdc < 3) {
				fprintf(stderr, "gw: program run requires a program ID\n");
				free(cmdv);
				return 2;
			}
			program_id = cmdv[2];
			for (j = 3; j < cmdc; j++) {
				if (strcmp(cmdv[j], "--json") == 0) {
					if (j + 1 >= cmdc) {
						fprintf(stderr, "gw: --json requires a value\n");
						free(json_file_data);
						free(cmdv);
						return 2;
					}
					json = cmdv[++j];
				} else if (strcmp(cmdv[j], "--json-file") == 0) {
					if (j + 1 >= cmdc) {
						fprintf(stderr, "gw: --json-file requires a path\n");
						free(json_file_data);
						free(cmdv);
						return 2;
					}
					json_file_data = read_json_file_alloc(cmdv[++j], error, sizeof(error));
					if (!json_file_data) {
						fprintf(stderr, "gw: %s\n", error);
						free(cmdv);
						return 2;
					}
					json = json_file_data;
				} else {
					fprintf(stderr, "gw: unknown program run option: %s\n", cmdv[j]);
					free(json_file_data);
					free(cmdv);
					return 2;
				}
			}
			code = command_program_run(&opts, program_id, json);
			free(json_file_data);
			free(cmdv);
			return code;
		}

		fprintf(stderr, "gw: unknown program subcommand: %s\n", cmdv[1]);
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
			fprintf(stderr, "gw: call requires a tool name\n");
			free(cmdv);
			return 2;
		}
		tool_name = cmdv[1];
		for (j = 2; j < cmdc; j++) {
			if (strcmp(cmdv[j], "--json") == 0) {
				if (j + 1 >= cmdc) {
					fprintf(stderr, "gw: --json requires a value\n");
					free(json_file_data);
					free(cmdv);
					return 2;
				}
				json = cmdv[++j];
			} else if (strcmp(cmdv[j], "--json-file") == 0) {
				if (j + 1 >= cmdc) {
					fprintf(stderr, "gw: --json-file requires a path\n");
					free(json_file_data);
					free(cmdv);
					return 2;
				}
				json_file_data = read_json_file_alloc(cmdv[++j], error, sizeof(error));
				if (!json_file_data) {
					fprintf(stderr, "gw: %s\n", error);
					free(cmdv);
					return 2;
				}
				json = json_file_data;
			} else {
				fprintf(stderr, "gw: unknown call option: %s\n", cmdv[j]);
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

	if (strcmp(cmdv[0], "run") == 0) {
		const char *program;
		const char *json = "{}";
		char *json_file_data = NULL;
		char error[256];
		int j;
		int code;

		if (cmdc < 2) {
			fprintf(stderr, "gw: run requires a program name\n");
			free(cmdv);
			return 2;
		}
		program = cmdv[1];
		for (j = 2; j < cmdc; j++) {
			if (strcmp(cmdv[j], "--json") == 0) {
				if (j + 1 >= cmdc) {
					fprintf(stderr, "gw: --json requires a value\n");
					free(json_file_data);
					free(cmdv);
					return 2;
				}
				json = cmdv[++j];
			} else if (strcmp(cmdv[j], "--json-file") == 0) {
				if (j + 1 >= cmdc) {
					fprintf(stderr, "gw: --json-file requires a path\n");
					free(json_file_data);
					free(cmdv);
					return 2;
				}
				json_file_data = read_json_file_alloc(cmdv[++j], error, sizeof(error));
				if (!json_file_data) {
					fprintf(stderr, "gw: %s\n", error);
					free(cmdv);
					return 2;
				}
				json = json_file_data;
			} else {
				fprintf(stderr, "gw: unknown run option: %s\n", cmdv[j]);
				free(json_file_data);
				free(cmdv);
				return 2;
			}
		}
		code = command_process_attach(&opts, program, json);
		free(json_file_data);
		free(cmdv);
		return code;
	}

	if (strcmp(cmdv[0], "process") == 0) {
		int code;
		if (cmdc < 2) {
			fprintf(stderr, "gw: expected process subcommand\n");
			free(cmdv);
			return 2;
		}

		if (strcmp(cmdv[1], "list") == 0) {
			code = command_process_list(&opts);
			free(cmdv);
			return code;
		}

		if (strcmp(cmdv[1], "kill") == 0) {
			if (cmdc < 3) {
				fprintf(stderr, "gw: process kill requires a process ID\n");
				free(cmdv);
				return 2;
			}
			code = command_process_kill(&opts, cmdv[2]);
			free(cmdv);
			return code;
		}

		if (strcmp(cmdv[1], "start") == 0 || strcmp(cmdv[1], "attach") == 0) {
			const char *program;
			const char *json = "{}";
			char *json_file_data = NULL;
			char error[256];
			int j;

			if (cmdc < 3) {
				fprintf(stderr, "gw: process %s requires a program name\n", cmdv[1]);
				free(cmdv);
				return 2;
			}
			program = cmdv[2];
			for (j = 3; j < cmdc; j++) {
				if (strcmp(cmdv[j], "--json") == 0) {
					if (j + 1 >= cmdc) {
						fprintf(stderr, "gw: --json requires a value\n");
						free(json_file_data);
						free(cmdv);
						return 2;
					}
					json = cmdv[++j];
				} else if (strcmp(cmdv[j], "--json-file") == 0) {
					if (j + 1 >= cmdc) {
						fprintf(stderr, "gw: --json-file requires a path\n");
						free(json_file_data);
						free(cmdv);
						return 2;
					}
					json_file_data = read_json_file_alloc(cmdv[++j], error, sizeof(error));
					if (!json_file_data) {
						fprintf(stderr, "gw: %s\n", error);
						free(cmdv);
						return 2;
					}
					json = json_file_data;
				} else {
					fprintf(stderr, "gw: unknown process %s option: %s\n", cmdv[1], cmdv[j]);
					free(json_file_data);
					free(cmdv);
					return 2;
				}
			}

			if (strcmp(cmdv[1], "start") == 0) {
				code = command_process_start(&opts, program, json);
			} else {
				code = command_process_attach(&opts, program, json);
			}
			free(json_file_data);
			free(cmdv);
			return code;
		}

		if (strcmp(cmdv[1], "view") == 0) {
			const char *process_id;
			const char *seq = "0";
			char *seq_file_data = NULL;
			char error[256];
			int j;

			if (cmdc < 3) {
				fprintf(stderr, "gw: process view requires a process ID\n");
				free(cmdv);
				return 2;
			}
			process_id = cmdv[2];
			for (j = 3; j < cmdc; j++) {
				if (strcmp(cmdv[j], "--seq") == 0) {
					if (j + 1 >= cmdc) {
						fprintf(stderr, "gw: --seq requires a JSON value\n");
						free(seq_file_data);
						free(cmdv);
						return 2;
					}
					seq = cmdv[++j];
				} else if (strcmp(cmdv[j], "--seq-file") == 0) {
					if (j + 1 >= cmdc) {
						fprintf(stderr, "gw: --seq-file requires a path\n");
						free(seq_file_data);
						free(cmdv);
						return 2;
					}
					seq_file_data = read_json_file_alloc(cmdv[++j], error, sizeof(error));
					if (!seq_file_data) {
						fprintf(stderr, "gw: %s\n", error);
						free(cmdv);
						return 2;
					}
					seq = seq_file_data;
				} else {
					fprintf(stderr, "gw: unknown process view option: %s\n", cmdv[j]);
					free(seq_file_data);
					free(cmdv);
					return 2;
				}
			}
			code = command_process_view(&opts, process_id, seq);
			free(seq_file_data);
			free(cmdv);
			return code;
		}

		if (strcmp(cmdv[1], "input") == 0) {
			const char *process_id;
			const char *text = NULL;
			const char *seq = NULL;
			char *seq_file_data = NULL;
			char error[256];
			int j;

			if (cmdc < 3) {
				fprintf(stderr, "gw: process input requires a process ID\n");
				free(cmdv);
				return 2;
			}
			process_id = cmdv[2];
			for (j = 3; j < cmdc; j++) {
				if (strcmp(cmdv[j], "--text") == 0) {
					if (j + 1 >= cmdc) {
						fprintf(stderr, "gw: --text requires a value\n");
						free(seq_file_data);
						free(cmdv);
						return 2;
					}
					text = cmdv[++j];
				} else if (strcmp(cmdv[j], "--seq") == 0) {
					if (j + 1 >= cmdc) {
						fprintf(stderr, "gw: --seq requires a JSON value\n");
						free(seq_file_data);
						free(cmdv);
						return 2;
					}
					seq = cmdv[++j];
				} else if (strcmp(cmdv[j], "--seq-file") == 0) {
					if (j + 1 >= cmdc) {
						fprintf(stderr, "gw: --seq-file requires a path\n");
						free(seq_file_data);
						free(cmdv);
						return 2;
					}
					seq_file_data = read_json_file_alloc(cmdv[++j], error, sizeof(error));
					if (!seq_file_data) {
						fprintf(stderr, "gw: %s\n", error);
						free(cmdv);
						return 2;
					}
					seq = seq_file_data;
				} else {
					fprintf(stderr, "gw: unknown process input option: %s\n", cmdv[j]);
					free(seq_file_data);
					free(cmdv);
					return 2;
				}
			}
			if (!text || !seq) {
				fprintf(stderr, "gw: process input requires --text and --seq or --seq-file\n");
				free(seq_file_data);
				free(cmdv);
				return 2;
			}
			code = command_process_input(&opts, process_id, text, seq);
			free(seq_file_data);
			free(cmdv);
			return code;
		}

		fprintf(stderr, "gw: unknown process subcommand: %s\n", cmdv[1]);
		free(cmdv);
		return 2;
	}

	fprintf(stderr, "gw: unknown command: %s\n", cmdv[0]);
	free(cmdv);
	return 2;
}

int main(int argc, char **argv)
{
	return dispatch(argc, argv);
}
