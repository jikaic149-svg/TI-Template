Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$script:WorkspaceRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$script:ManifestPath = Join-Path $script:WorkspaceRoot "config\verification\components.json"
$script:CacheDirectory = Join-Path $script:WorkspaceRoot "build\.verify-cache"
$script:CacheSchemaVersion = 2

function Get-Sha256Text {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Text)

    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
        return ([System.BitConverter]::ToString($sha256.ComputeHash($bytes))).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $sha256.Dispose()
    }
}

function Get-RelativePath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $prefix = $script:WorkspaceRoot + [System.IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside the template root: $fullPath"
    }
    return $fullPath.Substring($prefix.Length).Replace("\", "/")
}

function Read-VerificationManifest {
    if (-not (Test-Path -LiteralPath $script:ManifestPath -PathType Leaf)) {
        throw "Verification manifest is missing: $script:ManifestPath"
    }

    $manifest = Get-Content -LiteralPath $script:ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($manifest.schemaVersion -ne $script:CacheSchemaVersion) {
        throw "Unsupported verification manifest schema: $($manifest.schemaVersion)"
    }
    if (@($manifest.PSObject.Properties.Name) -notcontains "coreConfig" -or
        [string]::IsNullOrWhiteSpace([string]$manifest.coreConfig)) {
        throw "Verification manifest must define coreConfig"
    }

    $known = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($component in $manifest.components) {
        if (-not $known.Add([string]$component.id)) {
            throw "Duplicate verification component: $($component.id)"
        }
        if (@($component.PSObject.Properties.Name) -notcontains "configPrefixes") {
            throw "Component '$($component.id)' must define configPrefixes"
        }
        foreach ($dependency in $component.dependencies) {
            if (-not $known.Contains([string]$dependency)) {
                throw "Component '$($component.id)' must appear after dependency '$dependency'"
            }
        }
    }
    return $manifest
}

function Get-InputSnapshot {
    param([Parameter(Mandatory = $true)][object[]]$Entries)

    $files = [System.Collections.Generic.Dictionary[string, string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    $hashLines = [System.Collections.Generic.List[string]]::new()

    foreach ($entryValue in $Entries) {
        $entry = ([string]$entryValue).Replace("/", "\")
        $fullPath = Join-Path $script:WorkspaceRoot $entry
        $hashLines.Add("entry:$(([string]$entryValue).Replace('\', '/'))")
        if (Test-Path -LiteralPath $fullPath -PathType Leaf) {
            $relative = Get-RelativePath -Path $fullPath
            $files[$relative] = $fullPath
            continue
        }
        if (Test-Path -LiteralPath $fullPath -PathType Container) {
            foreach ($file in Get-ChildItem -LiteralPath $fullPath -File -Recurse) {
                $relative = Get-RelativePath -Path $file.FullName
                $files[$relative] = $file.FullName
            }
            continue
        }
        $hashLines.Add("missing:$(([string]$entryValue).Replace('\', '/'))")
    }

    foreach ($relative in @($files.Keys) | Sort-Object) {
        # Keep Markdown discoverable in Files without invalidating compile-oriented caches.
        if ([string]::Equals([System.IO.Path]::GetExtension($relative), ".md",
                [System.StringComparison]::OrdinalIgnoreCase)) {
            continue
        }
        $fileHash = (Get-FileHash -LiteralPath $files[$relative] -Algorithm SHA256).Hash.ToLowerInvariant()
        $hashLines.Add("file:$relative`:$fileHash")
    }

    return [pscustomobject]@{
        Hash = Get-Sha256Text -Text ($hashLines -join "`n")
        Files = @($files.Keys | Sort-Object)
    }
}

function Get-CoreConfigSnapshot {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [object[]]$Prefixes = @()
    )

    $fullPath = Join-Path $script:WorkspaceRoot $Path
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "Core configuration is missing: $fullPath"
    }

    $relative = Get-RelativePath -Path $fullPath
    $prefixSet = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::Ordinal)
    foreach ($prefixValue in $Prefixes) {
        $prefix = ([string]$prefixValue).Trim()
        if (-not [string]::IsNullOrWhiteSpace($prefix)) {
            [void]$prefixSet.Add($prefix)
        }
    }

    $hashLines = [System.Collections.Generic.List[string]]::new()
    $hashLines.Add("core-config:$relative")
    foreach ($prefix in @($prefixSet) | Sort-Object) {
        $hashLines.Add("prefix:$prefix")
    }

    $lines = [System.IO.File]::ReadAllLines($fullPath, [System.Text.Encoding]::UTF8)
    for ($index = 0; $index -lt $lines.Length; $index++) {
        $line = $lines[$index]
        if ($line -notmatch '^\s*#\s*([A-Za-z]+)\b(.*)$') {
            continue
        }

        $directive = $Matches[1].ToLowerInvariant()
        $remainder = $Matches[2]
        $directiveLines = [System.Collections.Generic.List[string]]::new()
        $directiveLines.Add($line.Trim())
        while ($directiveLines[$directiveLines.Count - 1] -match '\\\s*$' -and
            ($index + 1) -lt $lines.Length) {
            $index++
            $directiveLines.Add($lines[$index].Trim())
        }

        # Non-define directives are shared structure; parameter defines are owner-specific.
        $includeDirective = ($directive -ne "define")
        if ($directive -eq "define") {
            if ($remainder -notmatch '^\s*([A-Za-z_][A-Za-z0-9_]*)(.*)$') {
                $includeDirective = $true
            }
            else {
                $macroName = $Matches[1]
                $replacement = $Matches[2]
                $includeDirective = [string]::IsNullOrWhiteSpace($replacement) -or
                    $replacement.TrimStart().StartsWith("//") -or
                    $replacement.TrimStart().StartsWith("/*")
                if (-not $includeDirective) {
                    foreach ($prefix in $prefixSet) {
                        if ($macroName.StartsWith($prefix, [System.StringComparison]::Ordinal)) {
                            $includeDirective = $true
                            break
                        }
                    }
                }
            }
        }

        if ($includeDirective) {
            $hashLines.Add("directive:" + ($directiveLines -join "`n"))
        }
    }

    return [pscustomobject]@{
        Hash = Get-Sha256Text -Text ($hashLines -join "`n")
        Files = @($relative)
    }
}

function Get-CommandFingerprint {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [string]$Fallback,
        [string[]]$VersionArguments = @("--version")
    )

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    $path = if ($null -ne $command) { $command.Source } else { $Fallback }
    if ([string]::IsNullOrWhiteSpace($path) -or -not (Test-Path -LiteralPath $path -PathType Leaf)) {
        return "$Name=MISSING"
    }

    $item = Get-Item -LiteralPath $path
    $versionText = (& $path @VersionArguments 2>&1 | Out-String).Trim()
    return ("{0}={1}|{2}|{3}|{4}" -f $Name, $item.FullName, $item.Length,
        $item.LastWriteTimeUtc.Ticks, $versionText)
}

function Get-EnvironmentSnapshot {
    $knownHostGcc = "C:\msys64\ucrt64\bin\gcc.exe"
    $knownArmGcc = "D:\GCC\ARMGCC\10 2021.10\bin\arm-none-eabi-gcc.exe"
    $knownCMake = "D:\CMAKE\bin\cmake.exe"
    $knownNinja = "D:\Ninja\ninja.exe"
    $lines = @(
        "PowerShell=$($PSVersionTable.PSVersion.ToString())",
        (Get-CommandFingerprint -Name "gcc" -Fallback $knownHostGcc),
        (Get-CommandFingerprint -Name "arm-none-eabi-gcc" -Fallback $knownArmGcc),
        (Get-CommandFingerprint -Name "cmake" -Fallback $knownCMake),
        (Get-CommandFingerprint -Name "ninja" -Fallback $knownNinja)
    )
    return [pscustomobject]@{
        Hash = Get-Sha256Text -Text ($lines -join "`n")
        Lines = $lines
    }
}

function Get-ComponentRecords {
    param([Parameter(Mandatory = $true)]$Manifest)

    $globalSnapshot = Get-InputSnapshot -Entries @($Manifest.globalInputs)
    $environment = Get-EnvironmentSnapshot
    $records = [System.Collections.Generic.List[object]]::new()
    $byId = @{}

    foreach ($component in $Manifest.components) {
        $ownSnapshot = Get-InputSnapshot -Entries @($component.inputs)
        $configSnapshot = Get-CoreConfigSnapshot -Path ([string]$Manifest.coreConfig) `
            -Prefixes @($component.configPrefixes)
        $dependencyHashes = [ordered]@{}
        foreach ($dependency in $component.dependencies) {
            $dependencyHashes[[string]$dependency] = $byId[[string]$dependency].EffectiveHash
        }
        $effectiveLines = @(
            "schema=$($Manifest.schemaVersion)",
            "component=$($component.id)",
            "global=$($globalSnapshot.Hash)",
            "environment=$($environment.Hash)",
            "config=$($configSnapshot.Hash)",
            "own=$($ownSnapshot.Hash)"
        )
        foreach ($dependency in $dependencyHashes.Keys) {
            $effectiveLines += "dependency:$dependency=$($dependencyHashes[$dependency])"
        }

        $recordFiles = @($ownSnapshot.Files) + @($configSnapshot.Files)
        $record = [pscustomobject]@{
            Definition = $component
            OwnInputHash = $ownSnapshot.Hash
            GlobalInputHash = $globalSnapshot.Hash
            ConfigInputHash = $configSnapshot.Hash
            EnvironmentHash = $environment.Hash
            EnvironmentLines = $environment.Lines
            DependencyHashes = $dependencyHashes
            EffectiveHash = Get-Sha256Text -Text ($effectiveLines -join "`n")
            Files = @($recordFiles | Sort-Object -Unique)
            State = $null
        }
        $records.Add($record)
        $byId[[string]$component.id] = $record
    }
    return @($records)
}

function Get-CachePath {
    param([Parameter(Mandatory = $true)][string]$ComponentId)
    return Join-Path $script:CacheDirectory ($ComponentId.ToLowerInvariant() + ".json")
}

function Get-RecordState {
    param([Parameter(Mandatory = $true)]$Record)

    $cachePath = Get-CachePath -ComponentId ([string]$Record.Definition.id)
    if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
        return [pscustomobject]@{ Fresh = $false; Reason = "NO CACHE"; Cache = $null }
    }

    try {
        $cache = Get-Content -LiteralPath $cachePath -Raw -Encoding UTF8 | ConvertFrom-Json
    }
    catch {
        return [pscustomobject]@{ Fresh = $false; Reason = "CACHE INVALID"; Cache = $null }
    }

    $propertyNames = @($cache.PSObject.Properties.Name)
    if ($propertyNames -notcontains "schemaVersion" -or
        $propertyNames -notcontains "status" -or
        $propertyNames -notcontains "configInputHash" -or
        $propertyNames -notcontains "effectiveHash") {
        return [pscustomobject]@{ Fresh = $false; Reason = "CACHE SCHEMA"; Cache = $cache }
    }
    if ($cache.schemaVersion -ne $script:CacheSchemaVersion -or $cache.status -ne "PASS") {
        return [pscustomobject]@{ Fresh = $false; Reason = "CACHE STATUS"; Cache = $cache }
    }
    if ([string]$cache.effectiveHash -ne [string]$Record.EffectiveHash) {
        $reason = "INPUT CHANGED"
        if ($propertyNames -contains "environmentHash" -and
            [string]$cache.environmentHash -ne [string]$Record.EnvironmentHash) {
            $reason = "TOOLCHAIN CHANGED"
        }
        elseif ($propertyNames -contains "globalInputHash" -and
            [string]$cache.globalInputHash -ne [string]$Record.GlobalInputHash) {
            $reason = "GLOBAL INPUT"
        }
        elseif ([string]$cache.configInputHash -ne [string]$Record.ConfigInputHash) {
            $reason = "CONFIG CHANGED"
        }
        elseif ($propertyNames -contains "ownInputHash" -and
            [string]$cache.ownInputHash -eq [string]$Record.OwnInputHash) {
            $reason = "DEPENDENCY CHANGED"
        }
        return [pscustomobject]@{ Fresh = $false; Reason = $reason; Cache = $cache }
    }
    return [pscustomobject]@{ Fresh = $true; Reason = "CACHED PASS"; Cache = $cache }
}

function Update-RecordStates {
    param([Parameter(Mandatory = $true)][object[]]$Records)
    foreach ($record in $Records) {
        $record.State = Get-RecordState -Record $record
    }
}

function Resolve-Scope {
    param(
        [Parameter(Mandatory = $true)][string]$Scope,
        [Parameter(Mandatory = $true)][object[]]$Records
    )

    $selected = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    $forced = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    if ($Scope.Equals("Auto", [System.StringComparison]::OrdinalIgnoreCase)) {
        foreach ($record in $Records) {
            if (-not $record.State.Fresh) {
                [void]$selected.Add([string]$record.Definition.id)
            }
        }
        return [pscustomobject]@{ Selected = $selected; Forced = $forced; Name = "Auto" }
    }
    if ($Scope.Equals("All", [System.StringComparison]::OrdinalIgnoreCase)) {
        foreach ($record in $Records) {
            [void]$selected.Add([string]$record.Definition.id)
            [void]$forced.Add([string]$record.Definition.id)
        }
        return [pscustomobject]@{ Selected = $selected; Forced = $forced; Name = "All" }
    }

    $requested = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($name in $Scope.Split(",", [System.StringSplitOptions]::RemoveEmptyEntries)) {
        $trimmed = $name.Trim()
        $match = @($Records | Where-Object { $_.Definition.id -eq $trimmed })
        if ($match.Count -ne 1) {
            $valid = ($Records | ForEach-Object { $_.Definition.id }) -join ", "
            throw "Unknown scope '$trimmed'. Valid scopes: Auto, All, $valid"
        }
        [void]$requested.Add([string]$match[0].Definition.id)
        [void]$selected.Add([string]$match[0].Definition.id)
        [void]$forced.Add([string]$match[0].Definition.id)
    }

    $changed = $true
    while ($changed) {
        $changed = $false
        foreach ($record in $Records) {
            $id = [string]$record.Definition.id
            if ($selected.Contains($id)) {
                foreach ($dependency in $record.Definition.dependencies) {
                    if (-not $record.State.Fresh -and $selected.Add([string]$dependency)) {
                        $changed = $true
                    }
                }
            }
            if (-not $record.State.Fresh) {
                foreach ($dependency in $record.Definition.dependencies) {
                    if ($selected.Contains([string]$dependency) -and $selected.Add($id)) {
                        $changed = $true
                    }
                }
            }
        }
    }
    return [pscustomobject]@{ Selected = $selected; Forced = $forced; Name = $Scope }
}

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$Command,
        [string[]]$Arguments = @(),
        [hashtable]$Parameters
    )

    Write-Host "[RUN]  $Label" -ForegroundColor Cyan
    $global:LASTEXITCODE = 0
    if ($null -ne $Parameters) {
        & $Command @Parameters
    }
    else {
        & $Command @Arguments
    }
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

function Get-CMakePath {
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }
    $fallback = "D:\CMAKE\bin\cmake.exe"
    if (Test-Path -LiteralPath $fallback -PathType Leaf) {
        return $fallback
    }
    throw "CMake was not found in PATH or at $fallback"
}

function Write-ComponentCache {
    param(
        [Parameter(Mandatory = $true)]$Record,
        [Parameter(Mandatory = $true)][string[]]$Checks,
        [Parameter(Mandatory = $true)][bool]$CleanBuild
    )

    New-Item -ItemType Directory -Force -Path $script:CacheDirectory | Out-Null
    $head = (& git -C $script:WorkspaceRoot rev-parse HEAD 2>$null | Out-String).Trim()
    $cache = [ordered]@{
        schemaVersion = $script:CacheSchemaVersion
        component = [string]$Record.Definition.id
        status = "PASS"
        ownInputHash = $Record.OwnInputHash
        globalInputHash = $Record.GlobalInputHash
        configInputHash = $Record.ConfigInputHash
        environmentHash = $Record.EnvironmentHash
        dependencyHashes = $Record.DependencyHashes
        effectiveHash = $Record.EffectiveHash
        checks = $Checks
        cleanBuild = $CleanBuild
        gitHead = $head
        verifiedAtUtc = [DateTime]::UtcNow.ToString("o")
    }
    $json = $cache | ConvertTo-Json -Depth 8
    $cachePath = Get-CachePath -ComponentId ([string]$Record.Definition.id)
    $tempPath = $cachePath + ".tmp"
    [System.IO.File]::WriteAllText($tempPath, $json + "`n", [System.Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $tempPath -Destination $cachePath -Force
}

function Invoke-ComponentVerification {
    param(
        [Parameter(Mandatory = $true)]$Record,
        [Parameter(Mandatory = $true)][bool]$CleanBuild,
        [Parameter(Mandatory = $true)]$CompletedBuilds
    )

    $id = [string]$Record.Definition.id
    $cachePath = Get-CachePath -ComponentId $id
    if (Test-Path -LiteralPath $cachePath -PathType Leaf) {
        Remove-Item -LiteralPath $cachePath -Force
    }
    $checks = [System.Collections.Generic.List[string]]::new()
    try {
        foreach ($scope in @($Record.Definition.hostScopes)) {
            Invoke-CheckedCommand -Label "$id host tests ($scope)" `
                -Command (Join-Path $script:WorkspaceRoot "tests\run_gcc_tests.ps1") `
                -Parameters @{ Scope = [string]$scope }
            $checks.Add("host:$scope")
        }
        foreach ($scope in @($Record.Definition.armScopes)) {
            Invoke-CheckedCommand -Label "$id ARM strict compile ($scope)" `
                -Command (Join-Path $script:WorkspaceRoot "tests\run_arm_compile.ps1") `
                -Parameters @{ Scope = [string]$scope }
            $checks.Add("arm:$scope")
        }
        foreach ($presetValue in @($Record.Definition.buildPresets)) {
            $preset = [string]$presetValue
            if ($CompletedBuilds.Contains($preset)) {
                Write-Host "[REUSE] build preset $preset" -ForegroundColor DarkGreen
                $checks.Add("build:${preset}:reused")
                continue
            }
            $configurePreset = if ($preset.StartsWith("build-")) { $preset.Substring(6) } else { $preset }
            $cmake = Get-CMakePath
            Invoke-CheckedCommand -Label "$id configure ($configurePreset)" -Command $cmake `
                -Arguments @("--preset", $configurePreset)
            $buildArguments = @("--build", "--preset", $preset)
            if ($CleanBuild) {
                $buildArguments += "--clean-first"
            }
            Invoke-CheckedCommand -Label "$id build ($preset)" -Command $cmake -Arguments $buildArguments
            [void]$CompletedBuilds.Add($preset)
            $checks.Add("build:$preset")
        }
        Write-ComponentCache -Record $Record -Checks @($checks) -CleanBuild $CleanBuild
        Write-Host "[PASS] $id" -ForegroundColor Green
    }
    catch {
        if (Test-Path -LiteralPath $cachePath -PathType Leaf) {
            Remove-Item -LiteralPath $cachePath -Force
        }
        throw
    }
}

function Show-CacheStatus {
    param([Parameter(Mandatory = $true)][object[]]$Records)

    Write-Host ("{0,-16} {1,-20} {2,-12} {3}" -f "COMPONENT", "STATE", "HASH", "VERIFIED (UTC)")
    foreach ($record in $Records) {
        $verified = "-"
        if ($record.State.Fresh -and
            @($record.State.Cache.PSObject.Properties.Name) -contains "verifiedAtUtc") {
            $verified = [string]$record.State.Cache.verifiedAtUtc
        }
        Write-Host ("{0,-16} {1,-20} {2,-12} {3}" -f $record.Definition.id,
            $record.State.Reason, $record.EffectiveHash.Substring(0, 12), $verified)
    }
}

function New-AiContext {
    param(
        [Parameter(Mandatory = $true)]$Manifest,
        [Parameter(Mandatory = $true)][object[]]$Records,
        [Parameter(Mandatory = $true)]$ScopeInfo,
        [string]$OutputPath
    )

    if ([string]::IsNullOrWhiteSpace($OutputPath)) {
        $OutputPath = Join-Path $script:WorkspaceRoot "build\ai-context.md"
    }
    elseif (-not [System.IO.Path]::IsPathRooted($OutputPath)) {
        $OutputPath = Join-Path $script:WorkspaceRoot $OutputPath
    }
    $OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
    $parent = Split-Path -Parent $OutputPath
    New-Item -ItemType Directory -Force -Path $parent | Out-Null

    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $status = (& git -C $script:WorkspaceRoot status --short --branch --untracked-files=all 2>$null |
            Out-String).TrimEnd()
        $statusOk = ($LASTEXITCODE -eq 0)
        $diffStat = (& git -C $script:WorkspaceRoot diff HEAD --stat 2>$null | Out-String).TrimEnd()
        $diffStatOk = ($LASTEXITCODE -eq 0)
        $head = (& git -C $script:WorkspaceRoot rev-parse HEAD 2>$null | Out-String).Trim()
        $headOk = ($LASTEXITCODE -eq 0)
        $trackedChangedPaths = @(& git -C $script:WorkspaceRoot -c core.quotepath=false `
                diff HEAD --name-only --diff-filter=ACMRTUXB 2>$null)
        $trackedChangedPathsOk = ($LASTEXITCODE -eq 0)
        $untrackedPaths = @(& git -C $script:WorkspaceRoot -c core.quotepath=false `
                ls-files --others --exclude-standard 2>$null)
        $untrackedPathsOk = ($LASTEXITCODE -eq 0)
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if (-not $statusOk -or -not $diffStatOk -or -not $headOk -or
        -not $trackedChangedPathsOk -or -not $untrackedPathsOk) {
        throw "Failed to read Git context"
    }
    $selectedRecords = @($Records | Where-Object { $ScopeInfo.Selected.Contains([string]$_.Definition.id) })
    $focusFiles = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($record in $selectedRecords) {
        foreach ($file in $record.Files) {
            [void]$focusFiles.Add([string]$file)
        }
    }
    foreach ($changedPathValue in @($trackedChangedPaths) + @($untrackedPaths)) {
        $changedPath = [string]$changedPathValue
        if (-not [string]::Equals([System.IO.Path]::GetExtension($changedPath), ".md",
                [System.StringComparison]::OrdinalIgnoreCase)) {
            continue
        }
        $fullPath = Join-Path $script:WorkspaceRoot $changedPath
        if (Test-Path -LiteralPath $fullPath -PathType Leaf) {
            [void]$focusFiles.Add((Get-RelativePath -Path $fullPath))
        }
    }
    foreach ($entry in $Manifest.contextInputs) {
        $path = Join-Path $script:WorkspaceRoot ([string]$entry)
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            [void]$focusFiles.Add((Get-RelativePath -Path $path))
        }
    }

    $builder = [System.Text.StringBuilder]::new()
    [void]$builder.AppendLine("# TI MSPM0 Template：AI 增量上下文")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("生成时间：$([DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss zzz'))")
    [void]$builder.AppendLine("Git HEAD：``$head``")
    [void]$builder.AppendLine("请求范围：``$($ScopeInfo.Name)``。本文件和增量验证共用 ``config/verification/components.json``。")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("## 当前结论")
    [void]$builder.AppendLine()
    if ($selectedRecords.Count -eq 0) {
        [void]$builder.AppendLine("所有组件输入、依赖和工具链均命中通过缓存；仍应按任务读取目标源码，不把缓存当作实物验证。")
    }
    else {
        [void]$builder.AppendLine("需要关注的组件：$((@($selectedRecords | ForEach-Object { $_.Definition.id })) -join ', ')。")
    }
    [void]$builder.AppendLine("建议验证命令：``powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\project.ps1 -Action Verify -Scope Auto``")
    [void]$builder.AppendLine("该系统不会 commit、push、flash，也不会把源码缓存冒充硬件验证。")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("## 组件状态")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("| 组件 | 状态 | 有效哈希 | 说明 |")
    [void]$builder.AppendLine("|---|---|---|---|")
    foreach ($record in $Records) {
        [void]$builder.AppendLine("| $($record.Definition.id) | $($record.State.Reason) | ``$($record.EffectiveHash.Substring(0, 12))`` | $($record.Definition.description) |")
    }
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("## Git 工作树")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("``````text")
    [void]$builder.AppendLine($status)
    [void]$builder.AppendLine("``````")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("### 已跟踪差异统计")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("``````text")
    [void]$builder.AppendLine($diffStat)
    [void]$builder.AppendLine("``````")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("## 本次应优先读取的文件")
    [void]$builder.AppendLine()
    foreach ($file in @($focusFiles) | Sort-Object) {
        [void]$builder.AppendLine("- ``$file``")
    }
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("## AI 强制规则（AGENTS.md）")
    [void]$builder.AppendLine()
    $agentsPath = Join-Path $script:WorkspaceRoot "AGENTS.md"
    if (Test-Path -LiteralPath $agentsPath -PathType Leaf) {
        [void]$builder.AppendLine((Get-Content -LiteralPath $agentsPath -Raw -Encoding UTF8).TrimEnd())
    }

    [System.IO.File]::WriteAllText($OutputPath, $builder.ToString(), [System.Text.UTF8Encoding]::new($false))
    Write-Host "AI context: $OutputPath" -ForegroundColor Green
}

function Show-BaselineStatus {
    param([Parameter(Mandatory = $true)][object[]]$Records)

    $stale = @($Records | Where-Object { -not $_.State.Fresh })
    $cleanEvidenceIssues = [System.Collections.Generic.List[string]]::new()
    $buildComponentCount = 0
    foreach ($record in $Records) {
        $presets = @($record.Definition.buildPresets | ForEach-Object { [string]$_ })
        if ($presets.Count -eq 0) {
            continue
        }
        $buildComponentCount++
        if (-not $record.State.Fresh) {
            continue
        }

        $cache = $record.State.Cache
        $properties = @($cache.PSObject.Properties.Name)
        $cleanBuildProperty = $cache.PSObject.Properties["cleanBuild"]
        if ($null -eq $cleanBuildProperty -or
            $cleanBuildProperty.Value -isnot [bool] -or
            -not [bool]$cleanBuildProperty.Value) {
            $cleanEvidenceIssues.Add("$($record.Definition.id) (cleanBuild is not true)")
        }

        if ($properties -notcontains "checks") {
            $cleanEvidenceIssues.Add("$($record.Definition.id) (checks are missing)")
            continue
        }
        $checks = @($cache.checks | ForEach-Object { [string]$_ })
        $checkSet = [System.Collections.Generic.HashSet[string]]::new(
            [System.StringComparer]::OrdinalIgnoreCase)
        foreach ($check in $checks) {
            [void]$checkSet.Add($check)
        }
        $allowedBuildChecks = [System.Collections.Generic.HashSet[string]]::new(
            [System.StringComparer]::OrdinalIgnoreCase)
        foreach ($preset in $presets) {
            $builtCheck = "build:$preset"
            $reusedCheck = "build:${preset}:reused"
            [void]$allowedBuildChecks.Add($builtCheck)
            [void]$allowedBuildChecks.Add($reusedCheck)
            if (-not $checkSet.Contains($builtCheck) -and
                -not $checkSet.Contains($reusedCheck)) {
                $cleanEvidenceIssues.Add(
                    "$($record.Definition.id) (missing build evidence for $preset)")
            }
        }
        foreach ($check in $checks) {
            if ($check.StartsWith("build:", [System.StringComparison]::OrdinalIgnoreCase) -and
                -not $allowedBuildChecks.Contains($check)) {
                $cleanEvidenceIssues.Add(
                    "$($record.Definition.id) (unexpected build evidence '$check')")
            }
        }
    }
    $cleanEvidenceOk = ($stale.Count -eq 0 -and $cleanEvidenceIssues.Count -eq 0)
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $gitStatus = @(& git -C $script:WorkspaceRoot status --short --untracked-files=all 2>$null)
        $gitStatusOk = ($LASTEXITCODE -eq 0)
        $diffOutput = (& git -C $script:WorkspaceRoot diff HEAD --check 2>&1 | Out-String).TrimEnd()
        $diffOk = ($LASTEXITCODE -eq 0)
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if (-not $gitStatusOk) {
        throw "git status failed"
    }
    Show-CacheStatus -Records $Records
    Write-Host ""
    Write-Host ("Git worktree changes: {0}" -f $gitStatus.Count)
    Write-Host ("Current verification cache: {0}" -f $(if ($stale.Count -eq 0) { "PASS" } else { "STALE" }))
    if ($cleanEvidenceOk) {
        Write-Host ("Clean verification evidence: PASS ({0}/{0} build components; shared presets may be reused)" -f `
            $buildComponentCount)
    }
    else {
        Write-Host "Clean verification evidence: FAIL" -ForegroundColor Yellow
        if ($cleanEvidenceIssues.Count -ne 0) {
            Write-Host ("       {0}" -f ($cleanEvidenceIssues -join "; ")) -ForegroundColor Yellow
        }
    }
    Write-Host ("git diff HEAD --check: {0}" -f $(if ($diffOk) { "PASS" } else { "FAIL" }))
    if (-not $diffOk -and -not [string]::IsNullOrWhiteSpace($diffOutput)) {
        Write-Host $diffOutput -ForegroundColor Red
    }
    if ($cleanEvidenceOk -and $diffOk) {
        Write-Host "Baseline candidate is ready for human diff review." -ForegroundColor Green
    }
    else {
        Write-Host "Baseline candidate is not ready. Run Verify -Scope All -Clean and resolve failures first." -ForegroundColor Yellow
    }
    Write-Host "No commit, push, tag, or upload was performed."
}

function Invoke-TemplateProjectTool {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet("Context", "Verify", "CacheStatus", "BaselineStatus")]
        [string]$Action,
        [string]$Scope = "Auto",
        [string]$OutputPath,
        [switch]$Clean
    )

    Push-Location $script:WorkspaceRoot
    try {
        $manifest = Read-VerificationManifest
        $records = @(Get-ComponentRecords -Manifest $manifest)
        Update-RecordStates -Records $records
        $scopeInfo = Resolve-Scope -Scope $Scope -Records $records

        switch ($Action) {
            "Context" {
                New-AiContext -Manifest $manifest -Records $records -ScopeInfo $scopeInfo -OutputPath $OutputPath
            }
            "CacheStatus" {
                Show-CacheStatus -Records $records
            }
            "BaselineStatus" {
                Show-BaselineStatus -Records $records
            }
            "Verify" {
                Invoke-CheckedCommand -Label "template architecture" `
                    -Command (Join-Path $script:WorkspaceRoot "tools\check_template.ps1")
                Invoke-CheckedCommand -Label "git diff HEAD --check" -Command "git" `
                    -Arguments @("-C", $script:WorkspaceRoot, "diff", "HEAD", "--check")

                $completedBuilds = [System.Collections.Generic.HashSet[string]]::new(
                    [System.StringComparer]::OrdinalIgnoreCase)
                $runCount = 0
                foreach ($record in $records) {
                    $id = [string]$record.Definition.id
                    if (-not $scopeInfo.Selected.Contains($id)) {
                        continue
                    }
                    if ($record.State.Fresh -and -not $scopeInfo.Forced.Contains($id)) {
                        Write-Host "[CACHE] $id" -ForegroundColor DarkGreen
                        continue
                    }
                    Invoke-ComponentVerification -Record $record -CleanBuild ([bool]$Clean) `
                        -CompletedBuilds $completedBuilds
                    $runCount++
                }
                if ($runCount -eq 0) {
                    Write-Host "All component checks are cached; global checks passed." -ForegroundColor Green
                }
                else {
                    Write-Host "Incremental verification: PASS ($runCount component(s) executed)." -ForegroundColor Green
                }
            }
        }
    }
    finally {
        Pop-Location
    }
}

Export-ModuleMember -Function Invoke-TemplateProjectTool
