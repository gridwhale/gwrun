# gw Agent Guide

`gw` is the GridWhale command-line interface. Use it as a small, predictable
gateway to GridWhale capabilities. Do not treat it as a replacement for an
agent; use it as a tool that an agent can call.

## Rules For Agents

- Prefer `--output json`.
- Prefer `--json-file` and `--seq-file` over inline JSON.
- JSON and sequence files may be UTF-8 with or without a BOM; `gw` strips a
  leading UTF-8 BOM before sending.
- Use `gw manifest --output json` for command discovery.
- Use `gw tools list --output json` and `gw tools describe <tool> --output json`
  for GridWhale capability discovery.
- Use `gw resources list --output json` and `gw resources read <uri>` for
  GridWhale documentation, examples, and other read-only context.
- Use `gw tools call` or `gw call` for one-shot tool invocation.
- Use `gw program ...` wrappers for program create/write/read/compile/run
  workflows instead of hand-writing tool JSON.
- Use `gw process start`, `gw process view`, and `gw process input` for
  interactive or long-running programs.
- Avoid `gw run` for agent workflows except simple fire-and-capture smoke tests.

## Tool Workflow

```text
gw tools list --output json
gw tools describe <tool> --output json
gw tools call <tool> --json-file args.json --output json
```

For the short form:

```text
gw call <tool> --json-file args.json --output json
```

## Docs And Resource Workflow

Use resources before guessing GridLang syntax or GridWhale conventions:

```text
gw resources list --output json
gw resources read <uri> --output text
```

Use `--output json` when the resource metadata matters or when the content is
not plain text.

## Program Authoring Workflow

Prefer these wrappers over raw `program_*` tool calls:

```text
gw program create --name Hello --output json
gw program write <programID> --file hello.grid --output json
gw program compile <programID> --output json
gw program run <programID> --json-file args.json --output json
```

If the server advertises `program_read`, inspect examples with:

```text
gw program read <programID> --output text
```

`gw program write --file` reads the source file and sends `source` as a string.
This avoids common PowerShell object-serialization mistakes.

## Process Workflow

Start a process:

```text
gw process start <programID> --json-file args.json --output json
```

The `<program>` value must be `PROGRAMID.entryPoint`, for example
`NUEG3K9Y.HelloWorld`. Do not pass `/file/PROGRAMID`, a bare program ID, or a
leading-dot entry point.

View from the beginning:

```text
gw process view <processID> --seq 0 --output json
```

If the view result contains `INPUT`, write `INPUT.seq` exactly as returned to a
file and send input:

```text
gw process input <processID> --text <answer> --seq-file input.seq.json --output json
```

An `INPUT` object looks like:

```json
{"prompt":"What is your name? ","seq":["AEON2011:ipInteger:v1","KzFQSQEAAAAD"],"type":"consoleText"}
```

Use the `$Seq` returned by `process input` as the next view sequence:

```text
gw process view <processID> --seq-file next.seq.json --output json
```

This avoids duplicate local input echo while preserving the full process
history for later viewers.

## JSON Output

Successful commands return an envelope like:

```json
{"ok":true,"command":"tools.list","server":"https://dev.gridwhale.io/mcp/","durationMs":123,"result":{}}
```

Failed commands return:

```json
{"ok":false,"command":"tools.list","status":"remote_error","httpStatus":500,"durationMs":123,"error":{"message":"..."}}
```

If the server sends a JSON error body, `gw` includes it in `result`.

## Auth

For agents, set:

```text
GRIDWHALE_AUTH_HEADER=Basic ...
```

Interactive username/password auth works for humans, but agents should avoid
prompts.

If the local GridWhale MCP config exists on this machine, an agent can load the
configured auth header without printing it:

```powershell
$cfg = Get-Content 'C:\Users\gpm\Documents\GridWhale\.mcp.json' -Raw | ConvertFrom-Json
$argsList = $cfg.mcpServers.'gridwhale-dev'.args
$header = $argsList[($argsList.IndexOf('--header') + 1)]
$env:GRIDWHALE_AUTH_HEADER = $header.Substring('Authorization: '.Length)
```

If `GRIDWHALE_AUTH_HEADER` is absent, commands that need the server may prompt
for username and password. In automated workflows, set the environment variable
first instead of relying on prompts.
