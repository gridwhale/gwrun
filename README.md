# gwrun

`gwrun` is a small portable command-line client for GridWhale agent tooling.

The first version focuses on remote GridWhale MCP calls:

- `gwrun agent manifest --output json`
- `gwrun tools list --output json`
- `gwrun tools describe <name> --output json`
- `gwrun call <name> --json <object> --output json`
- `gwrun call <name> --json-file <path> --output json`
- `gwrun process start <program> --json-file <path> --output json`
- `gwrun process view <processID> --seq 0 --output json`
- `gwrun process input <processID> --text <text> --seq <json> --output json`
- `gwrun process attach <program> --json-file <path>`

Local process execution via `gwrun run` is intentionally deferred.

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
the required runtime DLLs and CA certificate bundle next to `gwrun.exe`:

```powershell
C:\msys64\usr\bin\bash.exe -lc "cd /c/Users/gpm/Documents/GWExtra/gwrun && PATH=/ucrt64/bin:`$PATH make copy-runtime"
```

Alternatively, run the executable with the UCRT runtime on `PATH`:

```powershell
$env:PATH='C:\msys64\ucrt64\bin;' + $env:PATH
.\gwrun.exe version --output json
```

## Auth

For now, `gwrun` expects Basic auth to be supplied as a full Authorization
header through:

```sh
GRIDWHALE_AUTH_HEADER="Basic ..."
```

If `GRIDWHALE_AUTH_HEADER` is not set, `gwrun` prompts for a username and
password and uses those credentials for Basic auth during the current process.
The password prompt disables terminal echo when the terminal supports it.

The default server is:

```text
https://dev.gridwhale.io/mcp/
```

Override it with:

```sh
gwrun --server https://dev.gridwhale.io/mcp/ tools list --output json
```

For complex arguments, prefer `--json-file` so shells do not rewrite JSON
quoting:

```sh
gwrun call NUEG3K9Y.HelloWorld --json-file args.json --output json
```

## Remote Processes

`gwrun process` uses the GridWhale process endpoints:

- `/mcp/processStart`
- `/mcp/processView`
- `/mcp/processInput`

Start a process:

```sh
gwrun process start NUEG3K9Y.HelloWorld --json-file args.json --output json
```

Poll for a process view. The sequence value is JSON; use `0` for the first
view and pass later `$Seq` values back exactly as returned:

```sh
gwrun process view <processID> --seq 0 --output json
```

When a view returns an `INPUT.seq`, send console input with that sequence:

```sh
gwrun process input <processID> --text "42" --seq '["AEON2011:ipInteger:v1","..."]' --output json
```

On shells that rewrite quotes in JSON arguments, write the sequence JSON to a
file and use `--seq-file`:

```sh
gwrun process input <processID> --text "42" --seq-file seq.json --output json
```

For an interactive console loop, use:

```sh
gwrun process attach NUEG3K9Y.HelloWorld --json-file args.json
```
