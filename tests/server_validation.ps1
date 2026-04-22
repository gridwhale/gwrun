param(
    [string]$Server = "https://dev.gridwhale.io/mcp/",
    [string]$GwPath = (Join-Path $PSScriptRoot "..\gw.exe"),
    [int]$TimeoutMs = 30000,
    [switch]$Insecure,
    [switch]$KeepProgram
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"

$script:Failures = 0

function Write-Utf8NoBom {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$Value
    )

    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Value, $encoding)
}

function Invoke-Gw {
    param(
        [Parameter(Mandatory=$true)][string[]]$Arguments
    )

    $globalArgs = @("--server", $Server, "--output", "json", "--timeout", "$TimeoutMs")
    if ($Insecure) {
        $globalArgs += "--insecure"
    }

    $output = & $GwPath @globalArgs @Arguments 2>&1
    $text = ($output -join "`n")
    $json = $null
    if ($text.Trim().Length -gt 0) {
        try {
            $json = $text | ConvertFrom-Json
        } catch {
            throw "gw returned non-JSON output for '$($Arguments -join ' ')': $text"
        }
    }

    [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Text = $text
        Json = $json
    }
}

function Get-McpEndpointUrl {
    param([Parameter(Mandatory=$true)][string]$Endpoint)

    $idx = $Server.IndexOf("/mcp")
    if ($idx -ge 0) {
        return $Server.Substring(0, $idx).TrimEnd("/") + "/mcp/" + $Endpoint
    }

    return $Server.TrimEnd("/") + "/mcp/" + $Endpoint
}

function Invoke-ProcessEndpoint {
    param(
        [Parameter(Mandatory=$true)][string]$Endpoint,
        [Parameter(Mandatory=$true)]$Body
    )

    if ($Insecure) {
        [System.Net.ServicePointManager]::ServerCertificateValidationCallback = { $true }
    }

    $auth = [Environment]::GetEnvironmentVariable("GRIDWHALE_AUTH_HEADER")
    if (-not $auth) {
        throw "GRIDWHALE_AUTH_HEADER must be set for direct process endpoint tests."
    }

    $url = Get-McpEndpointUrl $Endpoint
    $bodyText = $Body | ConvertTo-Json -Depth 20 -Compress
    $headers = @{ Authorization = $auth }

    try {
        $response = Invoke-WebRequest -Uri $url -Method Post -Headers $headers -ContentType "application/json" -Body $bodyText -UseBasicParsing -TimeoutSec ([Math]::Max(1, [int][Math]::Ceiling($TimeoutMs / 1000.0)))
        $statusCode = [int]$response.StatusCode
        $text = [string]$response.Content
    } catch [System.Net.WebException] {
        $statusCode = 0
        $text = ""
        if ($_.Exception.Response) {
            $statusCode = [int]$_.Exception.Response.StatusCode
            $reader = New-Object System.IO.StreamReader($_.Exception.Response.GetResponseStream())
            try {
                $text = $reader.ReadToEnd()
            } finally {
                $reader.Dispose()
            }
        } else {
            $text = $_.Exception.Message
        }
    }

    $json = $null
    if ($text.Trim().Length -gt 0) {
        try {
            $json = $text | ConvertFrom-Json
        } catch {
            $json = $null
        }
    }

    [pscustomobject]@{
        StatusCode = $statusCode
        Text = $text
        Json = $json
    }
}

function Get-McpStructuredContent {
    param([Parameter(Mandatory=$true)]$GwJson)

    if (($GwJson.PSObject.Properties.Name -contains "result") -and
        $null -ne $GwJson.result -and
        ($GwJson.result.PSObject.Properties.Name -contains "result") -and
        $null -ne $GwJson.result.result -and
        ($GwJson.result.result.PSObject.Properties.Name -contains "structuredContent") -and
        $null -ne $GwJson.result.result.structuredContent) {
        return $GwJson.result.result.structuredContent
    }
    return $null
}

function Test-IsValidationError {
    param([Parameter(Mandatory=$true)]$Run)

    $json = $Run.Json

    if (($json.PSObject.Properties.Name -contains "result") -and
        $null -ne $json.result -and
        ($json.result.PSObject.Properties.Name -contains "error") -and
        $null -ne $json.result.error) {
        return $json.result.error.code -eq -32602
    }

    if (($json.PSObject.Properties.Name -contains "result") -and
        $null -ne $json.result -and
        ($json.result.PSObject.Properties.Name -contains "result") -and
        $null -ne $json.result.result -and
        $json.result.result.PSObject.Properties.Name -contains "isError" -and
        $json.result.result.isError) {
        return $true
    }

    $structured = Get-McpStructuredContent $json
    if ($null -ne $structured -and
        $structured.PSObject.Properties.Name -contains "success" -and
        -not $structured.success) {
        return $true
    }

    return $false
}

function Test-IsToolErrorResult {
    param([Parameter(Mandatory=$true)]$Run)

    return Test-IsValidationError $Run
}

function Test-IsDirectValidationError {
    param([Parameter(Mandatory=$true)]$Run)

    if ($Run.StatusCode -eq 500 -or $Run.StatusCode -eq 0) {
        return $false
    }
    if ($Run.StatusCode -ge 400 -and $Run.StatusCode -lt 500) {
        return $true
    }
    if ($null -eq $Run.Json) {
        return $false
    }
    if (($Run.Json.PSObject.Properties.Name -contains "success") -and -not $Run.Json.success) {
        return $true
    }
    if (($Run.Json.PSObject.Properties.Name -contains "isError") -and $Run.Json.isError) {
        return $true
    }
    if (($Run.Json.PSObject.Properties.Name -contains "error") -and $null -ne $Run.Json.error) {
        return $true
    }
    return $false
}

function Assert-True {
    param(
        [Parameter(Mandatory=$true)][bool]$Condition,
        [Parameter(Mandatory=$true)][string]$Message,
        [string]$Expected,
        [string]$Actual
    )

    if ($Condition) {
        Write-Host "PASS: $Message"
    } else {
        Write-Host "FAIL: $Message" -ForegroundColor Red
        if ($Expected) {
            Write-Host "  Expected: $Expected" -ForegroundColor Red
        }
        if ($Actual) {
            Write-Host "  Actual:   $Actual" -ForegroundColor Red
        }
        $script:Failures++
    }
}

function Format-JsonCompact {
    param($Value)

    if ($null -eq $Value) {
        return "<null>"
    }
    return ($Value | ConvertTo-Json -Depth 20 -Compress)
}

function Format-TextForFailure {
    param([AllowNull()][string]$Value)

    if ($null -eq $Value) {
        return "<null>"
    }
    return ($Value | ConvertTo-Json -Compress)
}

function Format-DirectResponse {
    param($Run)

    return (ConvertTo-Json -Depth 20 -Compress @{
        statusCode = $Run.StatusCode
        body = $Run.Text
    })
}

function Get-RequiredStructuredField {
    param(
        [Parameter(Mandatory=$true)]$Run,
        [Parameter(Mandatory=$true)][string]$Field
    )

    $structured = Get-McpStructuredContent $Run.Json
    if ($null -eq $structured -or -not ($structured.PSObject.Properties.Name -contains $Field)) {
        throw "Missing structuredContent.$Field in response: $($Run.Text)"
    }
    return $structured.$Field
}

function Assert-SourceUnchanged {
    param(
        [Parameter(Mandatory=$true)][string]$ProgramID,
        [Parameter(Mandatory=$true)][string]$ExpectedSource,
        [Parameter(Mandatory=$true)][string]$Message
    )

    $readBack = Invoke-Gw @("program", "read", $ProgramID)
    if ($readBack.ExitCode -ne 0) {
        throw "Unable to read validation program after failed write: $($readBack.Text)"
    }
    $actualSource = Get-RequiredStructuredField $readBack "sourceCode"
    Assert-True `
        ($actualSource -eq $ExpectedSource) `
        $Message `
        (Format-TextForFailure $ExpectedSource) `
        (Format-TextForFailure $actualSource)
}

$tempDir = Join-Path ([System.IO.Path]::GetTempPath()) ("gw-server-validation-" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tempDir | Out-Null

try {
    if (-not (Test-Path -LiteralPath $GwPath)) {
        throw "gw executable not found: $GwPath"
    }

    Write-Host "Server: $Server"
    Write-Host "gw: $GwPath"

    $tools = Invoke-Gw @("tools", "list")
    if ($tools.ExitCode -ne 0) {
        throw "Unable to list tools: $($tools.Text)"
    }

    $toolList = $tools.Json.result.result.tools
    $programCreateTool = ($toolList | Where-Object { $_.name -eq "program_create" -or $_.name.EndsWith(".program_create") } | Select-Object -First 1).name
    if (-not $programCreateTool) {
        throw "Server does not advertise program_create."
    }
    $programWriteTool = ($toolList | Where-Object { $_.name -eq "program_write" -or $_.name.EndsWith(".program_write") } | Select-Object -First 1).name
    if (-not $programWriteTool) {
        throw "Server does not advertise program_write."
    }
    $programReadTool = ($toolList | Where-Object { $_.name -eq "program_read" -or $_.name.EndsWith(".program_read") } | Select-Object -First 1).name
    if (-not $programReadTool) {
        throw "Server does not advertise program_read."
    }
    $programCompileTool = ($toolList | Where-Object { $_.name -eq "program_compile" -or $_.name.EndsWith(".program_compile") } | Select-Object -First 1).name
    if (-not $programCompileTool) {
        throw "Server does not advertise program_compile."
    }
    $programRunTool = ($toolList | Where-Object { $_.name -eq "program_run" -or $_.name.EndsWith(".program_run") } | Select-Object -First 1).name
    if (-not $programRunTool) {
        throw "Server does not advertise program_run."
    }
    $processListTool = ($toolList | Where-Object { $_.name -eq "process_list" -or $_.name.EndsWith(".process_list") } | Select-Object -First 1).name
    if (-not $processListTool) {
        throw "Server does not advertise process_list."
    }
    $processKillTool = ($toolList | Where-Object { $_.name -eq "process_kill" -or $_.name.EndsWith(".process_kill") } | Select-Object -First 1).name
    if (-not $processKillTool) {
        throw "Server does not advertise process_kill."
    }
    $helloWorldTool = ($toolList | Where-Object { $_.name -eq "HelloWorld" -or $_.name.EndsWith(".HelloWorld") } | Select-Object -First 1).name
    if (-not $helloWorldTool) {
        throw "Server does not advertise HelloWorld."
    }

    Write-Host "program_create tool: $programCreateTool"
    Write-Host "program_write tool: $programWriteTool"
    Write-Host "program_read tool: $programReadTool"
    Write-Host "program_compile tool: $programCompileTool"
    Write-Host "program_run tool: $programRunTool"
    Write-Host "process_list tool: $processListTool"
    Write-Host "process_kill tool: $processKillTool"
    Write-Host "HelloWorld tool: $helloWorldTool"

    Write-Host ""
    Write-Host "CASE: program_create succeeds with valid name"
    $create = Invoke-Gw @("program", "create", "--name", ("gw-validation-" + [System.DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()))
    if ($create.ExitCode -ne 0) {
        throw "Unable to create validation program: $($create.Text)"
    }
    $programID = Get-RequiredStructuredField $create "programID"
    $bareProgramID = $programID.Substring("/file/".Length)
    Assert-True `
        ($programID -is [string] -and $programID.StartsWith("/file/")) `
        "server creates a program for a valid name" `
        "structuredContent.programID is a /file/... string." `
        (Format-JsonCompact $create.Json)
    Assert-True `
        (($create.Json.PSObject.Properties.Name -contains "derived") -and
         $null -ne $create.Json.derived -and
         ($create.Json.derived.PSObject.Properties.Name -contains "programIDBare") -and
         $create.Json.derived.programIDBare -eq $bareProgramID) `
        "gw derives bare programID for program.create" `
        "top-level derived.programIDBare matches programID without the /file/ prefix." `
        (Format-JsonCompact $create.Json)
    Write-Host "programID: $programID"
    Write-Host "bare programID: $bareProgramID"

    $missingCreateNameArgsPath = Join-Path $tempDir "program-create-missing-name.json"
    Write-Utf8NoBom $missingCreateNameArgsPath (@{} | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: program_create rejects missing name"
    $missingCreateName = Invoke-Gw @("tools", "call", $programCreateTool, "--json-file", $missingCreateNameArgsPath)
    Assert-True `
        (Test-IsValidationError $missingCreateName) `
        "server reports a validation error when name is missing" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='invalid_argument' or 'missing_argument', path='name', expected='string'." `
        (Format-JsonCompact $missingCreateName.Json)
    Assert-True `
        ($missingCreateName.Json.ok -ne $false -or $missingCreateName.Json.httpStatus -ne 500) `
        "missing name validation error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level validation error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $missingCreateName.Json)

    $numberCreateNameArgsPath = Join-Path $tempDir "program-create-number-name.json"
    Write-Utf8NoBom $numberCreateNameArgsPath (@{
        name = 123
    } | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: program_create rejects numeric name"
    $numberCreateName = Invoke-Gw @("tools", "call", $programCreateTool, "--json-file", $numberCreateNameArgsPath)
    Assert-True `
        (Test-IsValidationError $numberCreateName) `
        "server reports a validation error for numeric name" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='invalid_argument', path='name', expected='string', actual='number'." `
        (Format-JsonCompact $numberCreateName.Json)
    Assert-True `
        ($numberCreateName.Json.ok -ne $false -or $numberCreateName.Json.httpStatus -ne 500) `
        "numeric name validation error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level validation error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $numberCreateName.Json)

    $objectCreateNameArgsPath = Join-Path $tempDir "program-create-object-name.json"
    Write-Utf8NoBom $objectCreateNameArgsPath (@{
        name = @{
            value = "bad"
        }
    } | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: program_create rejects object-valued name"
    $objectCreateName = Invoke-Gw @("tools", "call", $programCreateTool, "--json-file", $objectCreateNameArgsPath)
    Assert-True `
        (Test-IsValidationError $objectCreateName) `
        "server reports a validation error for object-valued name" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='invalid_argument', path='name', expected='string', actual='object'." `
        (Format-JsonCompact $objectCreateName.Json)
    Assert-True `
        ($objectCreateName.Json.ok -ne $false -or $objectCreateName.Json.httpStatus -ne 500) `
        "object-valued name validation error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level validation error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $objectCreateName.Json)

    $blankCreateNameArgsPath = Join-Path $tempDir "program-create-blank-name.json"
    Write-Utf8NoBom $blankCreateNameArgsPath (@{
        name = "   "
    } | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: program_create rejects blank name"
    $blankCreateName = Invoke-Gw @("tools", "call", $programCreateTool, "--json-file", $blankCreateNameArgsPath)
    Assert-True `
        (Test-IsValidationError $blankCreateName) `
        "server reports a validation error for blank name" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='invalid_argument', path='name', message indicating name must be non-blank." `
        (Format-JsonCompact $blankCreateName.Json)
    Assert-True `
        ($blankCreateName.Json.ok -ne $false -or $blankCreateName.Json.httpStatus -ne 500) `
        "blank name validation error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level validation error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $blankCreateName.Json)

    $goodSource = 'print("validation sentinel")'
    $goodSourcePath = Join-Path $tempDir "good.grid"
    Write-Utf8NoBom $goodSourcePath $goodSource

    $writeGood = Invoke-Gw @("program", "write", $programID, "--file", $goodSourcePath)
    if ($writeGood.ExitCode -ne 0) {
        throw "Unable to write sentinel source: $($writeGood.Text)"
    }

    $invalidArgsPath = Join-Path $tempDir "invalid-source-object.json"
    Write-Utf8NoBom $invalidArgsPath (@{
        programID = $programID
        source = @{
            value = 'print("corrupted")'
            PSPath = "PowerShellObjectShape"
        }
    } | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: program_write rejects object-valued source"
    $invalidWrite = Invoke-Gw @("tools", "call", $programWriteTool, "--json-file", $invalidArgsPath)
    Assert-True `
        (Test-IsValidationError $invalidWrite) `
        "server reports a validation error for non-string source" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='invalid_argument', path='source', expected='string', actual='object'." `
        (Format-JsonCompact $invalidWrite.Json)
    Assert-True `
        ($invalidWrite.Json.ok -ne $false -or $invalidWrite.Json.httpStatus -ne 500) `
        "validation error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level validation error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $invalidWrite.Json)

    Assert-SourceUnchanged $programID $goodSource "object-source write does not modify existing source"

    $arraySourceArgsPath = Join-Path $tempDir "array-source.json"
    Write-Utf8NoBom $arraySourceArgsPath (@{
        programID = $programID
        source = @('print("corrupted")')
    } | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: program_write rejects array-valued source"
    $arraySourceWrite = Invoke-Gw @("tools", "call", $programWriteTool, "--json-file", $arraySourceArgsPath)
    Assert-True `
        (Test-IsValidationError $arraySourceWrite) `
        "server reports a validation error for array-valued source" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='invalid_argument', path='source', expected='string', actual='array'." `
        (Format-JsonCompact $arraySourceWrite.Json)
    Assert-True `
        ($arraySourceWrite.Json.ok -ne $false -or $arraySourceWrite.Json.httpStatus -ne 500) `
        "array source validation error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level validation error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $arraySourceWrite.Json)
    Assert-SourceUnchanged $programID $goodSource "array-source write does not modify existing source"

    $missingSourceArgsPath = Join-Path $tempDir "missing-source.json"
    Write-Utf8NoBom $missingSourceArgsPath (@{
        programID = $programID
    } | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: program_write rejects missing source"
    $missingSourceWrite = Invoke-Gw @("tools", "call", $programWriteTool, "--json-file", $missingSourceArgsPath)
    Assert-True `
        (Test-IsValidationError $missingSourceWrite) `
        "server reports a validation error when source is missing" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='invalid_argument' or 'missing_argument', path='source', expected='string'." `
        (Format-JsonCompact $missingSourceWrite.Json)
    Assert-True `
        ($missingSourceWrite.Json.ok -ne $false -or $missingSourceWrite.Json.httpStatus -ne 500) `
        "missing source validation error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level validation error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $missingSourceWrite.Json)

    Assert-SourceUnchanged $programID $goodSource "missing-source write does not modify existing source"

    $missingProgramIDArgsPath = Join-Path $tempDir "missing-program-id.json"
    Write-Utf8NoBom $missingProgramIDArgsPath (@{
        source = 'print("corrupted")'
    } | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: program_write rejects missing programID"
    $missingProgramIDWrite = Invoke-Gw @("tools", "call", $programWriteTool, "--json-file", $missingProgramIDArgsPath)
    Assert-True `
        (Test-IsValidationError $missingProgramIDWrite) `
        "server reports a validation error when programID is missing" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='invalid_argument' or 'missing_argument', path='programID', expected='string'." `
        (Format-JsonCompact $missingProgramIDWrite.Json)
    Assert-True `
        ($missingProgramIDWrite.Json.ok -ne $false -or $missingProgramIDWrite.Json.httpStatus -ne 500) `
        "missing programID validation error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level validation error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $missingProgramIDWrite.Json)
    Assert-SourceUnchanged $programID $goodSource "missing-programID write does not modify existing source"

    $numberProgramIDArgsPath = Join-Path $tempDir "number-program-id.json"
    Write-Utf8NoBom $numberProgramIDArgsPath (@{
        programID = 123
        source = 'print("corrupted")'
    } | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: program_write rejects numeric programID"
    $numberProgramIDWrite = Invoke-Gw @("tools", "call", $programWriteTool, "--json-file", $numberProgramIDArgsPath)
    Assert-True `
        (Test-IsValidationError $numberProgramIDWrite) `
        "server reports a validation error for numeric programID" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='invalid_argument', path='programID', expected='string', actual='number'." `
        (Format-JsonCompact $numberProgramIDWrite.Json)
    Assert-True `
        ($numberProgramIDWrite.Json.ok -ne $false -or $numberProgramIDWrite.Json.httpStatus -ne 500) `
        "numeric programID validation error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level validation error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $numberProgramIDWrite.Json)
    Assert-SourceUnchanged $programID $goodSource "numeric-programID write does not modify existing source"

    $malformedProgramIDArgsPath = Join-Path $tempDir "malformed-program-id.json"
    Write-Utf8NoBom $malformedProgramIDArgsPath (@{
        programID = "not-a-file-id"
        source = 'print("corrupted")'
    } | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: program_write rejects malformed programID string"
    $malformedProgramIDWrite = Invoke-Gw @("tools", "call", $programWriteTool, "--json-file", $malformedProgramIDArgsPath)
    Assert-True `
        (Test-IsValidationError $malformedProgramIDWrite) `
        "server reports a validation error for malformed programID string" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='invalid_program_id' or 'invalid_argument', path='programID'." `
        (Format-JsonCompact $malformedProgramIDWrite.Json)
    Assert-True `
        ($malformedProgramIDWrite.Json.ok -ne $false -or $malformedProgramIDWrite.Json.httpStatus -ne 500) `
        "malformed programID validation error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level validation error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $malformedProgramIDWrite.Json)
    Assert-SourceUnchanged $programID $goodSource "malformed-programID write does not modify existing source"

    Write-Host ""
    Write-Host "CASE: program_read succeeds with valid programID"
    $validReadArgsPath = Join-Path $tempDir "program-read-valid.json"
    Write-Utf8NoBom $validReadArgsPath (@{
        programID = $programID
    } | ConvertTo-Json -Depth 5 -Compress)
    $validRead = Invoke-Gw @("tools", "call", $programReadTool, "--json-file", $validReadArgsPath)
    $validReadStructured = Get-McpStructuredContent $validRead.Json
    Assert-True `
        ($validRead.ExitCode -eq 0 -and $null -ne $validReadStructured -and $validReadStructured.sourceCode -eq $goodSource) `
        "server reads sourceCode for a valid programID" `
        "structuredContent.sourceCode equals the sentinel source string." `
        (Format-JsonCompact $validRead.Json)

    Write-Host ""
    Write-Host "CASE: gw program read accepts bare programID"
    $bareRead = Invoke-Gw @("program", "read", $bareProgramID)
    $bareReadStructured = Get-McpStructuredContent $bareRead.Json
    Assert-True `
        ($bareRead.ExitCode -eq 0 -and $null -ne $bareReadStructured -and $bareReadStructured.sourceCode -eq $goodSource) `
        "gw normalizes bare programID for program read" `
        "structuredContent.sourceCode equals the sentinel source string." `
        (Format-JsonCompact $bareRead.Json)

    $missingReadProgramIDArgsPath = Join-Path $tempDir "program-read-missing-program-id.json"
    Write-Utf8NoBom $missingReadProgramIDArgsPath (@{} | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: program_read rejects missing programID"
    $missingReadProgramID = Invoke-Gw @("tools", "call", $programReadTool, "--json-file", $missingReadProgramIDArgsPath)
    Assert-True `
        (Test-IsValidationError $missingReadProgramID) `
        "server reports a validation error when programID is missing" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='invalid_argument' or 'missing_argument', path='programID', expected='string'." `
        (Format-JsonCompact $missingReadProgramID.Json)
    Assert-True `
        ($missingReadProgramID.Json.ok -ne $false -or $missingReadProgramID.Json.httpStatus -ne 500) `
        "missing programID validation error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level validation error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $missingReadProgramID.Json)

    $numberReadProgramIDArgsPath = Join-Path $tempDir "program-read-number-program-id.json"
    Write-Utf8NoBom $numberReadProgramIDArgsPath (@{
        programID = 123
    } | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: program_read rejects numeric programID"
    $numberReadProgramID = Invoke-Gw @("tools", "call", $programReadTool, "--json-file", $numberReadProgramIDArgsPath)
    Assert-True `
        (Test-IsValidationError $numberReadProgramID) `
        "server reports a validation error for numeric programID" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='invalid_argument', path='programID', expected='string', actual='number'." `
        (Format-JsonCompact $numberReadProgramID.Json)
    Assert-True `
        ($numberReadProgramID.Json.ok -ne $false -or $numberReadProgramID.Json.httpStatus -ne 500) `
        "numeric programID validation error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level validation error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $numberReadProgramID.Json)

    $malformedReadProgramIDArgsPath = Join-Path $tempDir "program-read-malformed-program-id.json"
    Write-Utf8NoBom $malformedReadProgramIDArgsPath (@{
        programID = "not-a-file-id"
    } | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: program_read rejects malformed programID string"
    $malformedReadProgramID = Invoke-Gw @("tools", "call", $programReadTool, "--json-file", $malformedReadProgramIDArgsPath)
    Assert-True `
        (Test-IsValidationError $malformedReadProgramID) `
        "server reports a validation error for malformed programID string" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='invalid_program_id' or 'invalid_argument', path='programID'." `
        (Format-JsonCompact $malformedReadProgramID.Json)
    Assert-True `
        ($malformedReadProgramID.Json.ok -ne $false -or $malformedReadProgramID.Json.httpStatus -ne 500) `
        "malformed programID validation error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level validation error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $malformedReadProgramID.Json)

    $nonexistentReadProgramIDArgsPath = Join-Path $tempDir "program-read-nonexistent-program-id.json"
    Write-Utf8NoBom $nonexistentReadProgramIDArgsPath (@{
        programID = "/file/ZZZZZZZZ"
    } | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: program_read rejects nonexistent programID"
    $nonexistentReadProgramID = Invoke-Gw @("tools", "call", $programReadTool, "--json-file", $nonexistentReadProgramIDArgsPath)
    Assert-True `
        (Test-IsValidationError $nonexistentReadProgramID) `
        "server reports a clean error for nonexistent programID" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='not_found' or 'invalid_program_id', path='programID'." `
        (Format-JsonCompact $nonexistentReadProgramID.Json)
    Assert-True `
        ($nonexistentReadProgramID.Json.ok -ne $false -or $nonexistentReadProgramID.Json.httpStatus -ne 500) `
        "nonexistent programID error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level not-found error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $nonexistentReadProgramID.Json)

    Write-Host ""
    Write-Host "CASE: program_compile succeeds with valid programID"
    $validCompileArgsPath = Join-Path $tempDir "program-compile-valid.json"
    Write-Utf8NoBom $validCompileArgsPath (@{
        programID = $programID
    } | ConvertTo-Json -Depth 5 -Compress)
    $validCompile = Invoke-Gw @("tools", "call", $programCompileTool, "--json-file", $validCompileArgsPath)
    $validCompileStructured = Get-McpStructuredContent $validCompile.Json
    Assert-True `
        ($validCompile.ExitCode -eq 0 -and $null -ne $validCompileStructured -and $validCompileStructured.success -eq $true) `
        "server compiles a valid programID" `
        "structuredContent.success=true." `
        (Format-JsonCompact $validCompile.Json)

    Write-Host ""
    Write-Host "CASE: gw program compile accepts bare programID"
    $bareCompile = Invoke-Gw @("program", "compile", $bareProgramID)
    $bareCompileStructured = Get-McpStructuredContent $bareCompile.Json
    Assert-True `
        ($bareCompile.ExitCode -eq 0 -and $null -ne $bareCompileStructured -and $bareCompileStructured.success -eq $true) `
        "gw normalizes bare programID for program compile" `
        "structuredContent.success=true." `
        (Format-JsonCompact $bareCompile.Json)

    $missingCompileProgramIDArgsPath = Join-Path $tempDir "program-compile-missing-program-id.json"
    Write-Utf8NoBom $missingCompileProgramIDArgsPath (@{} | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: program_compile rejects missing programID"
    $missingCompileProgramID = Invoke-Gw @("tools", "call", $programCompileTool, "--json-file", $missingCompileProgramIDArgsPath)
    Assert-True `
        (Test-IsValidationError $missingCompileProgramID) `
        "server reports a validation error when programID is missing" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='invalid_argument' or 'missing_argument', path='programID', expected='string'." `
        (Format-JsonCompact $missingCompileProgramID.Json)
    Assert-True `
        ($missingCompileProgramID.Json.ok -ne $false -or $missingCompileProgramID.Json.httpStatus -ne 500) `
        "missing programID validation error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level validation error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $missingCompileProgramID.Json)

    $numberCompileProgramIDArgsPath = Join-Path $tempDir "program-compile-number-program-id.json"
    Write-Utf8NoBom $numberCompileProgramIDArgsPath (@{
        programID = 123
    } | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: program_compile rejects numeric programID"
    $numberCompileProgramID = Invoke-Gw @("tools", "call", $programCompileTool, "--json-file", $numberCompileProgramIDArgsPath)
    Assert-True `
        (Test-IsValidationError $numberCompileProgramID) `
        "server reports a validation error for numeric programID" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='invalid_argument', path='programID', expected='string', actual='number'." `
        (Format-JsonCompact $numberCompileProgramID.Json)
    Assert-True `
        ($numberCompileProgramID.Json.ok -ne $false -or $numberCompileProgramID.Json.httpStatus -ne 500) `
        "numeric programID validation error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level validation error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $numberCompileProgramID.Json)

    $malformedCompileProgramIDArgsPath = Join-Path $tempDir "program-compile-malformed-program-id.json"
    Write-Utf8NoBom $malformedCompileProgramIDArgsPath (@{
        programID = "not-a-file-id"
    } | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: program_compile rejects malformed programID string"
    $malformedCompileProgramID = Invoke-Gw @("tools", "call", $programCompileTool, "--json-file", $malformedCompileProgramIDArgsPath)
    Assert-True `
        (Test-IsValidationError $malformedCompileProgramID) `
        "server reports a validation error for malformed programID string" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='invalid_program_id' or 'invalid_argument', path='programID'." `
        (Format-JsonCompact $malformedCompileProgramID.Json)
    Assert-True `
        ($malformedCompileProgramID.Json.ok -ne $false -or $malformedCompileProgramID.Json.httpStatus -ne 500) `
        "malformed programID validation error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level validation error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $malformedCompileProgramID.Json)

    $nonexistentCompileProgramIDArgsPath = Join-Path $tempDir "program-compile-nonexistent-program-id.json"
    Write-Utf8NoBom $nonexistentCompileProgramIDArgsPath (@{
        programID = "/file/ZZZZZZZZ"
    } | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: program_compile rejects nonexistent programID"
    $nonexistentCompileProgramID = Invoke-Gw @("tools", "call", $programCompileTool, "--json-file", $nonexistentCompileProgramIDArgsPath)
    Assert-True `
        (Test-IsValidationError $nonexistentCompileProgramID) `
        "server reports a clean error for nonexistent programID" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='not_found' or 'invalid_program_id', path='programID'." `
        (Format-JsonCompact $nonexistentCompileProgramID.Json)
    Assert-True `
        ($nonexistentCompileProgramID.Json.ok -ne $false -or $nonexistentCompileProgramID.Json.httpStatus -ne 500) `
        "nonexistent programID error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level not-found error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $nonexistentCompileProgramID.Json)

    Write-Host ""
    Write-Host "CASE: program_compile returns clean compile errors"
    $badCreate = Invoke-Gw @("program", "create", "--name", ("gw-validation-bad-compile-" + [System.DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()))
    if ($badCreate.ExitCode -ne 0) {
        throw "Unable to create compile-error validation program: $($badCreate.Text)"
    }
    $badProgramID = Get-RequiredStructuredField $badCreate "programID"
    $badSourcePath = Join-Path $tempDir "bad-compile.grid"
    Write-Utf8NoBom $badSourcePath 'if do'
    $writeBad = Invoke-Gw @("program", "write", $badProgramID, "--file", $badSourcePath)
    if ($writeBad.ExitCode -ne 0) {
        throw "Unable to write bad compile source: $($writeBad.Text)"
    }
    $badCompileArgsPath = Join-Path $tempDir "program-compile-bad-source.json"
    Write-Utf8NoBom $badCompileArgsPath (@{
        programID = $badProgramID
    } | ConvertTo-Json -Depth 5 -Compress)
    $badCompile = Invoke-Gw @("tools", "call", $programCompileTool, "--json-file", $badCompileArgsPath)
    Assert-True `
        (Test-IsToolErrorResult $badCompile) `
        "server reports compile errors as a clean tool result" `
        "MCP tool result with isError:true or structuredContent.success:false, including useful compile diagnostics." `
        (Format-JsonCompact $badCompile.Json)
    Assert-True `
        ($badCompile.Json.ok -ne $false -or $badCompile.Json.httpStatus -ne 500) `
        "compile error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level compile error. Never HTTP 500." `
        (Format-JsonCompact $badCompile.Json)

    Write-Host ""
    Write-Host "CASE: program_run succeeds with valid arguments"
    $validRunArgsPath = Join-Path $tempDir "program-run-valid.json"
    Write-Utf8NoBom $validRunArgsPath (@{
        programID = $programID
        args = @{}
    } | ConvertTo-Json -Depth 5 -Compress)
    $validRun = Invoke-Gw @("tools", "call", $programRunTool, "--json-file", $validRunArgsPath)
    $validRunStructured = Get-McpStructuredContent $validRun.Json
    Assert-True `
        ($validRun.ExitCode -eq 0 -and $null -ne $validRunStructured -and $validRunStructured.status -eq "normal" -and $validRunStructured.output -eq "validation sentinel") `
        "server runs a valid program_run request" `
        "structuredContent.status='normal' and structuredContent.output='validation sentinel'." `
        (Format-JsonCompact $validRun.Json)

    Write-Host ""
    Write-Host "CASE: gw program run accepts bare programID"
    $runArgsPath = Join-Path $tempDir "gw-program-run-bare.json"
    Write-Utf8NoBom $runArgsPath (@{} | ConvertTo-Json -Depth 5 -Compress)
    $bareRun = Invoke-Gw @("program", "run", $bareProgramID, "--json-file", $runArgsPath)
    $bareRunStructured = Get-McpStructuredContent $bareRun.Json
    Assert-True `
        ($bareRun.ExitCode -eq 0 -and $null -ne $bareRunStructured -and $bareRunStructured.status -eq "normal" -and $bareRunStructured.output -eq "validation sentinel") `
        "gw normalizes bare programID for program run" `
        "structuredContent.status='normal' and structuredContent.output='validation sentinel'." `
        (Format-JsonCompact $bareRun.Json)

    $missingRunProgramIDArgsPath = Join-Path $tempDir "program-run-missing-program-id.json"
    Write-Utf8NoBom $missingRunProgramIDArgsPath (@{
        args = @{}
    } | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: program_run rejects missing programID"
    $missingRunProgramID = Invoke-Gw @("tools", "call", $programRunTool, "--json-file", $missingRunProgramIDArgsPath)
    Assert-True `
        (Test-IsValidationError $missingRunProgramID) `
        "server reports a validation error when programID is missing" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='invalid_argument' or 'missing_argument', path='programID', expected='string'." `
        (Format-JsonCompact $missingRunProgramID.Json)
    Assert-True `
        ($missingRunProgramID.Json.ok -ne $false -or $missingRunProgramID.Json.httpStatus -ne 500) `
        "missing programID validation error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level validation error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $missingRunProgramID.Json)

    $numberRunProgramIDArgsPath = Join-Path $tempDir "program-run-number-program-id.json"
    Write-Utf8NoBom $numberRunProgramIDArgsPath (@{
        programID = 123
        args = @{}
    } | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: program_run rejects numeric programID"
    $numberRunProgramID = Invoke-Gw @("tools", "call", $programRunTool, "--json-file", $numberRunProgramIDArgsPath)
    Assert-True `
        (Test-IsValidationError $numberRunProgramID) `
        "server reports a validation error for numeric programID" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='invalid_argument', path='programID', expected='string', actual='number'." `
        (Format-JsonCompact $numberRunProgramID.Json)
    Assert-True `
        ($numberRunProgramID.Json.ok -ne $false -or $numberRunProgramID.Json.httpStatus -ne 500) `
        "numeric programID validation error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level validation error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $numberRunProgramID.Json)

    $malformedRunProgramIDArgsPath = Join-Path $tempDir "program-run-malformed-program-id.json"
    Write-Utf8NoBom $malformedRunProgramIDArgsPath (@{
        programID = "not-a-file-id"
        args = @{}
    } | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: program_run rejects malformed programID string"
    $malformedRunProgramID = Invoke-Gw @("tools", "call", $programRunTool, "--json-file", $malformedRunProgramIDArgsPath)
    Assert-True `
        (Test-IsValidationError $malformedRunProgramID) `
        "server reports a validation error for malformed programID string" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='invalid_program_id' or 'invalid_argument', path='programID'." `
        (Format-JsonCompact $malformedRunProgramID.Json)
    Assert-True `
        ($malformedRunProgramID.Json.ok -ne $false -or $malformedRunProgramID.Json.httpStatus -ne 500) `
        "malformed programID validation error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level validation error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $malformedRunProgramID.Json)

    $stringRunArgsPath = Join-Path $tempDir "program-run-string-args.json"
    Write-Utf8NoBom $stringRunArgsPath (@{
        programID = $programID
        args = "not-an-object"
    } | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: program_run rejects string args"
    $stringRunArgs = Invoke-Gw @("tools", "call", $programRunTool, "--json-file", $stringRunArgsPath)
    Assert-True `
        (Test-IsValidationError $stringRunArgs) `
        "server reports a validation error for string args" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='invalid_argument', path='args', expected='object', actual='string'." `
        (Format-JsonCompact $stringRunArgs.Json)
    Assert-True `
        ($stringRunArgs.Json.ok -ne $false -or $stringRunArgs.Json.httpStatus -ne 500) `
        "string args validation error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level validation error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $stringRunArgs.Json)

    $interactiveArgsPath = Join-Path $tempDir "interactive-empty-args.json"
    Write-Utf8NoBom $interactiveArgsPath "{}"

    Write-Host ""
    Write-Host "CASE: processStart rejects missing program"
    $missingProcessProgram = Invoke-ProcessEndpoint "processStart" @{ args = @{} }
    Assert-True `
        (Test-IsDirectValidationError $missingProcessProgram) `
        "server reports a validation error when processStart program is missing" `
        "Direct process endpoint returns a non-500 validation error with path='program' or equivalent." `
        (Format-DirectResponse $missingProcessProgram)

    Write-Host ""
    Write-Host "CASE: processStart rejects malformed program reference"
    $malformedProcessProgram = Invoke-ProcessEndpoint "processStart" @{ program = "not-a-program-ref"; args = @{} }
    Assert-True `
        (Test-IsDirectValidationError $malformedProcessProgram) `
        "server reports a validation error for malformed processStart program" `
        "Direct process endpoint returns a non-500 validation error explaining expected PROGRAMID.entryPoint or /file/PROGRAMID." `
        (Format-DirectResponse $malformedProcessProgram)

    Write-Host ""
    Write-Host "CASE: processView rejects missing processID"
    $missingViewProcessID = Invoke-ProcessEndpoint "processView" @{ seq = 0 }
    Assert-True `
        (Test-IsDirectValidationError $missingViewProcessID) `
        "server reports a validation error when processView processID is missing" `
        "Direct process endpoint returns a non-500 validation error with path='processID' or equivalent." `
        (Format-DirectResponse $missingViewProcessID)

    Write-Host ""
    Write-Host "CASE: processView rejects numeric processID"
    $numberViewProcessID = Invoke-ProcessEndpoint "processView" @{ processID = 123; seq = 0 }
    Assert-True `
        (Test-IsDirectValidationError $numberViewProcessID) `
        "server reports a validation error for numeric processView processID" `
        "Direct process endpoint returns a non-500 validation error with path='processID', expected='string', actual='number'." `
        (Format-DirectResponse $numberViewProcessID)

    Write-Host ""
    Write-Host "CASE: processInput rejects missing inputText"
    $inputValidationStart = Invoke-Gw @("process", "start", $helloWorldTool, "--json-file", $interactiveArgsPath)
    $inputValidationProcessID = $inputValidationStart.Json.result.processID
    $inputValidationView = Invoke-Gw @("process", "view", $inputValidationProcessID, "--seq", "0")
    $inputValidationSeq = $inputValidationView.Json.result.input.seq
    $missingInputText = Invoke-ProcessEndpoint "processInput" @{ processID = $inputValidationProcessID; seq = $inputValidationSeq }
    Assert-True `
        (Test-IsDirectValidationError $missingInputText) `
        "server reports a validation error when processInput inputText is missing" `
        "Direct process endpoint returns a non-500 validation error with path='inputText' or equivalent." `
        (Format-DirectResponse $missingInputText)

    Write-Host ""
    Write-Host "CASE: processInput rejects numeric inputText"
    $numberInputStart = Invoke-Gw @("process", "start", $helloWorldTool, "--json-file", $interactiveArgsPath)
    $numberInputProcessID = $numberInputStart.Json.result.processID
    $numberInputView = Invoke-Gw @("process", "view", $numberInputProcessID, "--seq", "0")
    $numberInputSeq = $numberInputView.Json.result.input.seq
    $numberInputText = Invoke-ProcessEndpoint "processInput" @{ processID = $numberInputProcessID; inputText = 123; seq = $numberInputSeq }
    Assert-True `
        (Test-IsDirectValidationError $numberInputText) `
        "server reports a validation error for numeric processInput inputText" `
        "Direct process endpoint returns a non-500 validation error with path='inputText', expected='string', actual='number'." `
        (Format-DirectResponse $numberInputText)

    Write-Host ""
    Write-Host "CASE: interactive process start/view/input/view succeeds"
    $processStart = Invoke-Gw @("process", "start", $helloWorldTool, "--json-file", $interactiveArgsPath)
    Assert-True `
        ($processStart.ExitCode -eq 0 -and $null -ne $processStart.Json.result -and $processStart.Json.result.processID -is [string] -and $processStart.Json.result.processID.Length -gt 0 -and $processStart.Json.result.status -eq "waitingForInput") `
        "process start returns a processID" `
        "gw process start returns ok:true with result.processID and status='waitingForInput'." `
        (Format-JsonCompact $processStart.Json)
    $processID = $processStart.Json.result.processID

    $processView = Invoke-Gw @("process", "view", $processID, "--seq", "0")
    $inputSeq = $null
    if ($processView.ExitCode -eq 0 -and
        $null -ne $processView.Json.result -and
        ($processView.Json.result.PSObject.Properties.Name -contains "input") -and
        $null -ne $processView.Json.result.input -and
        ($processView.Json.result.input.PSObject.Properties.Name -contains "seq")) {
        $inputSeq = $processView.Json.result.input.seq
    }
    Assert-True `
        ($processView.ExitCode -eq 0 -and $processView.Json.result.status -eq "waitingForInput" -and $null -ne $inputSeq) `
        "process view reports input request with seq" `
        "result.status='waitingForInput' and result.input.seq is present." `
        (Format-JsonCompact $processView.Json)

    $inputSeqPath = Join-Path $tempDir "interactive-input-seq.json"
    Write-Utf8NoBom $inputSeqPath (Format-JsonCompact $inputSeq)

    $processInput = Invoke-Gw @("process", "input", $processID, "--text", "Codex", "--seq-file", $inputSeqPath)
    $nextSeq = $null
    if ($processInput.ExitCode -eq 0 -and
        $null -ne $processInput.Json.result -and
        ($processInput.Json.result.PSObject.Properties.Name -contains 'seq')) {
        $nextSeq = $processInput.Json.result.seq
    }
    Assert-True `
        ($processInput.ExitCode -eq 0 -and $null -ne $nextSeq) `
        "process input accepts text and returns next seq" `
        "gw process input returns ok:true and result.seq." `
        (Format-JsonCompact $processInput.Json)

    $nextSeqPath = Join-Path $tempDir "interactive-next-seq.json"
    Write-Utf8NoBom $nextSeqPath (Format-JsonCompact $nextSeq)

    $finalView = Invoke-Gw @("process", "view", $processID, "--seq-file", $nextSeqPath)
    Assert-True `
        ($finalView.ExitCode -eq 0 -and $finalView.Json.result.status -eq "completed" -and $finalView.Json.result.outputText.Contains("Hello, Codex!")) `
        "process view returns final output after input" `
        "result.status='completed' and outputText contains 'Hello, Codex!'." `
        (Format-JsonCompact $finalView.Json)

    Write-Host ""
    Write-Host "CASE: processStart accepts created /file/PROGRAMID"
    $fileProcessStart = Invoke-Gw @("process", "start", $programID, "--json-file", $interactiveArgsPath)
    Assert-True `
        ($fileProcessStart.ExitCode -eq 0 -and $null -ne $fileProcessStart.Json.result -and $fileProcessStart.Json.result.processID -is [string] -and $fileProcessStart.Json.result.processID.Length -gt 0 -and $fileProcessStart.Json.result.status -eq "completed") `
        "process start accepts /file/PROGRAMID and returns a processID" `
        "gw process start /file/PROGRAMID returns ok:true with result.processID and status='completed'." `
        (Format-JsonCompact $fileProcessStart.Json)

    if ($fileProcessStart.ExitCode -eq 0 -and $null -ne $fileProcessStart.Json.result -and $fileProcessStart.Json.result.processID -is [string]) {
        $fileProcessID = $fileProcessStart.Json.result.processID
        $fileProcessView = Invoke-Gw @("process", "view", $fileProcessID, "--seq", "0")
        Assert-True `
            ($fileProcessView.ExitCode -eq 0 -and $fileProcessView.Json.result.status -eq "completed" -and $fileProcessView.Json.result.outputText.Contains("validation sentinel")) `
            "process view returns output for /file/PROGRAMID process" `
            "result.status='completed' and outputText contains 'validation sentinel'." `
            (Format-JsonCompact $fileProcessView.Json)
    }

    Write-Host ""
    Write-Host "CASE: processStart accepts bare PROGRAMID"
    $bareProcessStart = Invoke-Gw @("process", "start", $bareProgramID, "--json-file", $interactiveArgsPath)
    Assert-True `
        ($bareProcessStart.ExitCode -eq 0 -and $null -ne $bareProcessStart.Json.result -and $bareProcessStart.Json.result.processID -is [string] -and $bareProcessStart.Json.result.processID.Length -gt 0 -and $bareProcessStart.Json.result.status -eq "completed") `
        "gw normalizes bare PROGRAMID for process start" `
        "gw process start PROGRAMID returns ok:true with result.processID and status='completed'." `
        (Format-JsonCompact $bareProcessStart.Json)

    if ($bareProcessStart.ExitCode -eq 0 -and $null -ne $bareProcessStart.Json.result -and $bareProcessStart.Json.result.processID -is [string]) {
        $bareProcessID = $bareProcessStart.Json.result.processID
        $bareProcessView = Invoke-Gw @("process", "view", $bareProcessID, "--seq", "0")
        Assert-True `
            ($bareProcessView.ExitCode -eq 0 -and $bareProcessView.Json.result.status -eq "completed" -and $bareProcessView.Json.result.outputText.Contains("validation sentinel")) `
            "process view returns output for bare PROGRAMID process" `
            "result.status='completed' and outputText contains 'validation sentinel'." `
            (Format-JsonCompact $bareProcessView.Json)
    }

    Write-Host ""
    Write-Host "CASE: process_list succeeds"
    $processList = Invoke-Gw @("process", "list")
    $processListStructured = Get-McpStructuredContent $processList.Json
    $processListRows = $null
    if ($processList.ExitCode -eq 0 -and
        $null -ne $processListStructured -and
        ($processListStructured.PSObject.Properties.Name -contains "processList") -and
        $processListStructured.processList.Count -gt 1) {
        $processListRows = $processListStructured.processList[1].rows
    }
    Assert-True `
        ($processList.ExitCode -eq 0 -and $null -ne $processListRows) `
        "process_list returns a process table" `
        "structuredContent.processList contains table metadata with a rows count." `
        (Format-JsonCompact $processList.Json)

    $missingKillProcessIDArgsPath = Join-Path $tempDir "process-kill-missing-process-id.json"
    Write-Utf8NoBom $missingKillProcessIDArgsPath (@{} | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: process_kill rejects missing processID"
    $missingKillProcessID = Invoke-Gw @("tools", "call", $processKillTool, "--json-file", $missingKillProcessIDArgsPath)
    Assert-True `
        (Test-IsValidationError $missingKillProcessID) `
        "server reports a validation error when process_kill processID is missing" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='invalid_argument' or 'missing_argument', path='processID', expected='string'." `
        (Format-JsonCompact $missingKillProcessID.Json)
    Assert-True `
        ($missingKillProcessID.Json.ok -ne $false -or $missingKillProcessID.Json.httpStatus -ne 500) `
        "missing processID validation error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level validation error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $missingKillProcessID.Json)

    $numberKillProcessIDArgsPath = Join-Path $tempDir "process-kill-number-process-id.json"
    Write-Utf8NoBom $numberKillProcessIDArgsPath (@{
        processID = 123
    } | ConvertTo-Json -Depth 5 -Compress)

    Write-Host ""
    Write-Host "CASE: process_kill rejects numeric processID"
    $numberKillProcessID = Invoke-Gw @("tools", "call", $processKillTool, "--json-file", $numberKillProcessIDArgsPath)
    Assert-True `
        (Test-IsValidationError $numberKillProcessID) `
        "server reports a validation error for numeric process_kill processID" `
        "MCP tool result with isError:true or structuredContent.success:false, with error.code='invalid_argument', path='processID', expected='string', actual='number'." `
        (Format-JsonCompact $numberKillProcessID.Json)
    Assert-True `
        ($numberKillProcessID.Json.ok -ne $false -or $numberKillProcessID.Json.httpStatus -ne 500) `
        "numeric processID validation error is not returned as HTTP 500" `
        "HTTP 200 / JSON-RPC success carrying a tool-level validation error, or JSON-RPC -32602 Invalid params. Never HTTP 500." `
        (Format-JsonCompact $numberKillProcessID.Json)

    Write-Host ""
    Write-Host "CASE: process_kill terminates a waiting process"
    $killStart = Invoke-Gw @("process", "start", $helloWorldTool, "--json-file", $interactiveArgsPath)
    $killProcessID = $killStart.Json.result.processID
    $killView = Invoke-Gw @("process", "view", $killProcessID, "--seq", "0")
    Assert-True `
        ($killStart.ExitCode -eq 0 -and $killView.ExitCode -eq 0 -and $killView.Json.result.status -eq "waitingForInput") `
        "test process is waiting for input before kill" `
        "process start succeeds and initial view has result.status='waitingForInput'." `
        (Format-JsonCompact $killView.Json)
    $kill = Invoke-Gw @("process", "kill", $killProcessID)
    Assert-True `
        ($kill.ExitCode -eq 0 -and $null -ne (Get-McpStructuredContent $kill.Json) -and (Get-McpStructuredContent $kill.Json).success -eq $true) `
        "process_kill reports success" `
        "structuredContent.success=true." `
        (Format-JsonCompact $kill.Json)
    $afterKillView = Invoke-Gw @("process", "view", $killProcessID, "--seq", "0")
    Assert-True `
        ($afterKillView.ExitCode -eq 0 -and $afterKillView.Json.result.status -eq "completed") `
        "killed process views as completed" `
        "result.status='completed'." `
        (Format-JsonCompact $afterKillView.Json)

    if ($KeepProgram) {
        Write-Host "Kept validation program: $programID"
    }
} finally {
    Remove-Item -LiteralPath $tempDir -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:Failures -gt 0) {
    Write-Host ""
    Write-Host "$script:Failures validation test(s) failed." -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "All server validation tests passed."
exit 0
