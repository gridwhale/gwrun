# gw

`gw` is the GridWhale command-line interface for agents and humans.

It provides one stable way to discover GridWhale tools, call tools, and work
with remote GridWhale processes.

## Global Options

```text
gw [--server URL] [--output text|json] [--timeout ms] <command>
```

- `--server URL`: GridWhale MCP endpoint. Defaults to `https://dev.gridwhale.io/mcp/`.
- `--output text|json`: Human text or machine-readable JSON. Defaults to text.
- `--timeout ms`: Request timeout in milliseconds. Defaults to 30000.

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

## Remote Processes

Agents should use the decomposed process commands:

```text
gw process start <program> --json-file args.json --output json
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
