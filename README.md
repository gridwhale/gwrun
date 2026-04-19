# gw

`gw` is a small portable command-line client for GridWhale agent tooling.
The build also creates `gridwhale` as an optional long-form executable alias.

The first version focuses on remote GridWhale MCP calls:

- `gw manifest --output json`
- `gw help`
- `gw help agents`
- `gw tools list --output json`
- `gw tools describe <name> --output json`
- `gw tools call <name> --json <object> --output json`
- `gw call <name> --json-file <path> --output json`
- `gw process start <program> --json-file <path> --output json`
- `gw process view <processID> --seq 0 --output json`
- `gw process input <processID> --text <text> --seq-file <path> --output json`
- `gw run <program> --json-file <path>`

Agents should use `process start`, `process view`, and `process input` for
remote processes. Humans can use `gw run` for terminal-style interactive IO.

## License

MIT. See `LICENSE`.

## Build

Requires a C11 compiler and libcurl.

```sh
make
```

On Windows, the verified local setup uses MSYS2 UCRT64:

```powershell
winget install --id MSYS2.MSYS2 -e --accept-package-agreements --accept-source-agreements
C:\msys64\usr\bin\bash.exe -lc "pacman -Sy --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-curl make"
C:\msys64\usr\bin\bash.exe -lc "cd /c/Users/gpm/Documents/GWExtra/gwrun && PATH=/ucrt64/bin:`$PATH make clean all"
```

To run the executable directly from PowerShell without changing `PATH`, copy
the required runtime DLLs and CA certificate bundle next to `gw.exe`:

```powershell
C:\msys64\usr\bin\bash.exe -lc "cd /c/Users/gpm/Documents/GWExtra/gwrun && PATH=/ucrt64/bin:`$PATH make copy-runtime"
```

Alternatively, run the executable with the UCRT runtime on `PATH`:

```powershell
$env:PATH='C:\msys64\ucrt64\bin;' + $env:PATH
.\gw.exe version --output json
```

## Auth

For now, `gw` expects Basic auth to be supplied as a full Authorization
header through:

```sh
GRIDWHALE_AUTH_HEADER="Basic ..."
```

If `GRIDWHALE_AUTH_HEADER` is not set, `gw` prompts for a username and
password and uses those credentials for Basic auth during the current process.
The password prompt disables terminal echo when the terminal supports it.

The default server is:

```text
https://dev.gridwhale.io/mcp/
```

Override it with:

```sh
gw --server https://dev.gridwhale.io/mcp/ tools list --output json
```

For complex arguments, prefer `--json-file` so shells do not rewrite JSON
quoting:

```sh
gw call NUEG3K9Y.HelloWorld --json-file args.json --output json
```

## Remote Processes

`gw process` uses the GridWhale process endpoints:

- `/mcp/processStart`
- `/mcp/processView`
- `/mcp/processInput`

Start a process:

```sh
gw process start NUEG3K9Y.HelloWorld --json-file args.json --output json
```

Poll for a process view. The sequence value is JSON; use `0` for the first
view and pass later `$Seq` values back exactly as returned:

```sh
gw process view <processID> --seq 0 --output json
```

When a view returns an `INPUT.seq`, send console input with that sequence:

```sh
gw process input <processID> --text "42" --seq '["AEON2011:ipInteger:v1","..."]' --output json
```

After input, use the `$Seq` returned by `process input` for the next
`process view`. That skips the console echo already handled by the submitting
client while preserving it in the process history for later viewers.

On shells that rewrite quotes in JSON arguments, write the sequence JSON to a
file and use `--seq-file`:

```sh
gw process input <processID> --text "42" --seq-file seq.json --output json
```

For an interactive console loop, use:

```sh
gw run NUEG3K9Y.HelloWorld --json-file args.json
```
