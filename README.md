# gwrun

`gwrun` is a small portable command-line client for GridWhale agent tooling.

The first version focuses on remote GridWhale MCP calls:

- `gwrun agent manifest --output json`
- `gwrun tools list --output json`
- `gwrun tools describe <name> --output json`
- `gwrun call <name> --json <object> --output json`
- `gwrun call <name> --json-file <path> --output json`

Local process execution via `gwrun run` is intentionally deferred.

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
the required runtime DLLs next to `gwrun.exe`:

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
