# gw

`gw` is the GridWhale command-line interface for agents and humans.

It provides one stable way to discover GridWhale tools, call tools, and work
with remote GridWhale processes.

## Global Options

```text
gw [--server URL] [--output text|json] [--timeout ms] [--insecure] <command>
```

- `--server URL`: GridWhale MCP endpoint. Defaults to `https://dev.gridwhale.io/mcp/`.
- `--output text|json`: Human text or machine-readable JSON. Defaults to text.
- `--timeout ms`: Request timeout in milliseconds. Defaults to 30000.
- `--insecure`: Disable TLS certificate verification. Use only for local development servers.

## Auth

For noninteractive use, set:

```text
GRIDWHALE_AUTH_HEADER=Basic ...
```

If `GRIDWHALE_AUTH_HEADER` is not set, `gw` prompts for a username and password.

## Discovery

```text
gw manifest --output json
gw tools list --output json
gw tools describe <tool> --output json
```

## Resources And Docs

Use resources for read-only documentation, examples, schemas, and other
context exposed by GridWhale:

```text
gw resources list --output json
gw resources read <uri> --output text
```

## Tool Calls

Prefer JSON files so shells do not rewrite quotes:

```text
gw tools call <tool> --json-file args.json --output json
gw call <tool> --json-file args.json --output json
```

`gw call` is a short alias for `gw tools call`.
JSON files may be UTF-8 with or without a BOM; `gw` strips a leading UTF-8 BOM
before sending.

## Program Authoring

Use program wrappers for common program workflows. They construct the tool JSON
for you and avoid shell-specific JSON mistakes:

```text
gw program create --name Hello --output json
gw program write <programID> --file hello.grid --output json
gw program read <programID> --output text
gw program compile <programID> --output json
gw program run <programID> --json-file args.json --output json
```

Use `gw process start <programID.entryPoint>` for interactive entry points, or
`gw process start <programID>` after creating a program with `gw program create`.
`process start` accepts `PROGRAMID.entryPoint` or `/file/PROGRAMID`, for example
`NUEG3K9Y.HelloWorld` or `/file/7QGK2YY9`.

## Remote Processes

Agents should use the decomposed process commands:

```text
gw process start <programID> --json-file args.json --output json
gw process list --output json
gw process kill <processID> --output json
gw process view <processID> --seq 0 --output json
gw process input <processID> --text <text> --seq-file input.seq.json --output json
gw process view <processID> --seq-file next.seq.json --output json
```

When `process view` returns `INPUT.seq`, pass that exact JSON value to
`process input`. After input, use the `$Seq` returned by `process input` for the
next view.

Humans can use terminal-style interactive mode:

```text
gw run <program> --json-file args.json
```

## More Agent Guidance

```text
gw help agents
```
