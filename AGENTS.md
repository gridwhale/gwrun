# gw Agent Notes

## Project

`gw` is a small C11 command-line client for GridWhale MCP-style remote tools.
The source is intentionally dependency-light: the only external runtime/library
dependency is libcurl.

Remote process execution is available via `gw process` and `gw run`.

## Repository Location

Expected local checkout:

```text
C:\Users\gpm\Documents\GWExtra\gwrun
```

The GridWhale workspace containing the MCP auth configuration is usually:

```text
C:\Users\gpm\Documents\GridWhale
```

## Editing

- Keep the code portable C11.
- Avoid adding large dependencies. Prefer small internal helpers unless a real
  parser/library is justified.
- The current text output parser in `src/commands.c` is intentionally narrow:
  it formats the current MCP `tools/list` response for humans. JSON output is
  the stable agent-facing interface.
- Generated Windows runtime files are ignored by git:
  `*.dll`, `ca-bundle.crt`, `gw`, `gw.exe`, `gridwhale`, and `gridwhale.exe`.

## Build On Windows

The verified local toolchain is MSYS2 UCRT64.

Install prerequisites if missing:

```powershell
winget install --id MSYS2.MSYS2 -e --accept-package-agreements --accept-source-agreements
C:\msys64\usr\bin\bash.exe -lc "pacman -Sy --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-curl make"
```

Build and copy runtime DLLs plus the CA bundle next to `gw.exe`:

```powershell
C:\msys64\usr\bin\bash.exe -lc "cd /c/Users/gpm/Documents/GWExtra/gwrun && PATH=/ucrt64/bin:`$PATH make clean all copy-runtime"
```

If only compiling inside MSYS2 and not running directly from PowerShell:

```powershell
C:\msys64\usr\bin\bash.exe -lc "cd /c/Users/gpm/Documents/GWExtra/gwrun && PATH=/ucrt64/bin:`$PATH make clean all"
```

## Running

After `make copy-runtime`, PowerShell can run the executable directly:

```powershell
.\gw.exe version --output json
```

Expected version shape:

```json
{"ok":true,"gw":{"version":"0.1.0"}}
```

If runtime DLLs have not been copied, PowerShell may return exit code
`-1073741515` with no output. Run `make copy-runtime`.

If HTTPS fails with a CA certificate error, ensure `ca-bundle.crt` was copied by
`make copy-runtime`, or run with `CURL_CA_BUNDLE` pointing to a valid CA bundle.

## Auth

Preferred noninteractive agent path:

```powershell
$env:GRIDWHALE_AUTH_HEADER = 'Basic ...'
```

When `GRIDWHALE_AUTH_HEADER` is absent, `gw` prompts for username/password and
uses those credentials for Basic auth during the current process.

To load the existing local GridWhale MCP auth value without printing it:

```powershell
$cfg = Get-Content 'C:\Users\gpm\Documents\GridWhale\.mcp.json' -Raw | ConvertFrom-Json
$argsList = $cfg.mcpServers.'gridwhale-dev'.args
$header = $argsList[($argsList.IndexOf('--header') + 1)]
$env:GRIDWHALE_AUTH_HEADER = $header.Substring('Authorization: '.Length)
```

The default server is:

```text
https://dev.gridwhale.io/mcp/
```

Override with:

```powershell
.\gw.exe --server https://dev.gridwhale.io/mcp/ tools list --output json
```

## Verification

Run these from `C:\Users\gpm\Documents\GWExtra\gwrun` after building.

Version:

```powershell
.\gw.exe version --output json
```

Authenticated remote check:

```powershell
.\gw.exe check --output json --timeout 30000
```

Expected check shape:

```json
{"ok":true,"command":"check","server":"https://dev.gridwhale.io/mcp/","auth":{"configured":true},"remote":{"reachable":true,"httpStatus":200}}
```

Tool discovery, agent JSON:

```powershell
.\gw.exe tools list --output json --timeout 30000
```

Tool discovery, human text:

```powershell
.\gw.exe tools list --output text --timeout 30000
```

Expected text output starts with:

```text
Server: https://dev.gridwhale.io/mcp/
Duration: ...
Tools:
  NUEG3K9Y.HelloWorld
```

Tool invocation with a JSON file is preferred because shells rewrite inline JSON:

```powershell
Set-Content -Path .\hello.args.json -Value '{"name":"Codex"}' -NoNewline -Encoding ascii
.\gw.exe call NUEG3K9Y.HelloWorld --json-file .\hello.args.json --output json --timeout 30000
Remove-Item .\hello.args.json
```

Expected result contains:

```json
"text":"Hello, Codex!"
```

## Git Notes

This repo has its own git history separate from the main GridWhale tree. Before
editing, check:

```powershell
git status --short --branch
```

Do not commit copied runtime DLLs, `ca-bundle.crt`, object files, or built
executables.
