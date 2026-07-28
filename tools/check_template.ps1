[CmdletBinding()]
param(
    [string]$Root
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
    $Root = Split-Path -Parent $PSScriptRoot
}
$Root = (Resolve-Path -LiteralPath $Root).Path
$findings = [System.Collections.Generic.List[string]]::new()

function Get-RelativeTemplatePath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if ($fullPath.StartsWith(
            ($Root + [System.IO.Path]::DirectorySeparatorChar),
            [System.StringComparison]::OrdinalIgnoreCase)) {
        return $fullPath.Substring($Root.Length + 1)
    }
    return $fullPath
}

function Add-Finding {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][int]$Line,
        [Parameter(Mandatory = $true)][string]$Reason
    )

    $relative = Get-RelativeTemplatePath -Path $Path
    $findings.Add(("{0}:{1}: {2}" -f $relative, $Line, $Reason))
}

function Test-TemplateEntryUnderDirectory {
    param(
        [Parameter(Mandatory = $true)][System.IO.FileSystemInfo]$Entry,
        [Parameter(Mandatory = $true)][string]$RelativeDirectory
    )

    $directoryPath = [System.IO.Path]::GetFullPath(
        (Join-Path $Root $RelativeDirectory)).TrimEnd(
            [System.IO.Path]::DirectorySeparatorChar,
            [System.IO.Path]::AltDirectorySeparatorChar)
    $prefix = $directoryPath + [System.IO.Path]::DirectorySeparatorChar
    return $Entry.FullName.StartsWith(
        $prefix,
        [System.StringComparison]::OrdinalIgnoreCase)
}

function Test-ManifestInputPath {
    param(
        [Parameter(Mandatory = $true)][string]$ManifestPath,
        [Parameter(Mandatory = $true)][string]$Field,
        [AllowNull()]$Entry
    )

    if ($Entry -isnot [string] -or [string]::IsNullOrWhiteSpace($Entry)) {
        Add-Finding -Path $ManifestPath -Line 1 -Reason (
            "{0} contains an empty or non-string input path" -f $Field)
        return $null
    }
    if ([System.IO.Path]::IsPathRooted($Entry)) {
        Add-Finding -Path $ManifestPath -Line 1 -Reason (
            "{0} input must be repository-relative: {1}" -f $Field, $Entry)
        return $null
    }

    try {
        $candidate = [System.IO.Path]::GetFullPath((Join-Path $Root $Entry))
    }
    catch {
        Add-Finding -Path $ManifestPath -Line 1 -Reason (
            "{0} contains an invalid input path: {1}" -f $Field, $Entry)
        return $null
    }

    $rootPrefix = $Root.TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar) +
        [System.IO.Path]::DirectorySeparatorChar
    if (-not $candidate.StartsWith(
            $rootPrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        Add-Finding -Path $ManifestPath -Line 1 -Reason (
            "{0} input escapes the repository: {1}" -f $Field, $Entry)
        return $null
    }
    if (-not (Test-Path -LiteralPath $candidate)) {
        Add-Finding -Path $ManifestPath -Line 1 -Reason (
            "{0} input does not exist: {1}" -f $Field, $Entry)
        return $null
    }
    return $candidate
}

function Get-ScriptParameterValidateSet {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$ParameterName
    )

    $tokens = $null
    $parseErrors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile(
        $Path,
        [ref]$tokens,
        [ref]$parseErrors)
    if ($parseErrors.Count -ne 0) {
        throw ("cannot parse script: {0}" -f
            (($parseErrors | ForEach-Object { $_.Message }) -join "; "))
    }

    $parameter = $ast.ParamBlock.Parameters |
        Where-Object { $_.Name.VariablePath.UserPath -ceq $ParameterName } |
        Select-Object -First 1
    if ($null -eq $parameter) {
        throw ("parameter '{0}' is missing" -f $ParameterName)
    }
    $validateSet = $parameter.Attributes |
        Where-Object { $_.TypeName.Name -ceq "ValidateSet" } |
        Select-Object -First 1
    if ($null -eq $validateSet) {
        throw ("parameter '{0}' has no ValidateSet" -f $ParameterName)
    }

    return @($validateSet.PositionalArguments | ForEach-Object {
            [string]$_.SafeGetValue()
        })
}

# Keep generated output, executable configuration, and project records in
# separate roots. This prevents old root-level Config/cmake/debug directories
# from silently returning and becoming competing configuration sources.
$rootEntries = @(Get-ChildItem -LiteralPath $Root -Force -ErrorAction Stop)
$rootDirectories = @($rootEntries | Where-Object { $_.PSIsContainer })
$excludedScanRoots = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
foreach ($excludedName in @(".git", ".agents", ".codex", "build")) {
    [void]$excludedScanRoots.Add($excludedName)
}

# Enumerate each allowed root branch once. In particular, do not enter build
# or AI/Git metadata and attempt to filter their descendants afterward.
$repositoryEntries = @(
    $rootEntries
    foreach ($directory in $rootDirectories) {
        if ($excludedScanRoots.Contains($directory.Name)) {
            continue
        }
        Get-ChildItem -LiteralPath $directory.FullName -Recurse -Force `
            -ErrorAction Stop
    }
)
$repositoryFiles = @($repositoryEntries |
    Where-Object { -not $_.PSIsContainer })
$repositoryDirectories = @($repositoryEntries |
    Where-Object { $_.PSIsContainer })
foreach ($legacyName in @("Config", "cmake", "debug", "Profile", "System")) {
    $legacyDirectory = $rootDirectories |
        Where-Object { $_.Name -ceq $legacyName } |
        Select-Object -First 1
    if ($null -ne $legacyDirectory) {
        Add-Finding -Path $legacyDirectory.FullName -Line 1 -Reason (
            "legacy root directory must be migrated to config/ or docs/")
    }
}

$requiredFiles = @(
    "config\README.md",
    "config\core_config.h",
    "config\cmake\arm-gcc-toolchain.cmake",
    "config\cmake\DriverLibModules.cmake",
    "config\openocd\README.md",
    "config\openocd\daplink.local.cfg.example",
    "config\openocd\xds110.local.cfg.example",
    "config\openocd\mspm0_daplink_custom.cfg",
    "config\openocd\mspm0_xds110.cfg",
    "config\verification\components.json",
    "tools\project.ps1",
    "tools\lib\ProjectTools.psm1"
)
foreach ($relative in $requiredFiles) {
    $path = Join-Path $Root $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Add-Finding -Path $path -Line 1 -Reason "required template file is missing"
    }
}

$configRoot = Join-Path $Root "config"
if (Test-Path -LiteralPath $configRoot -PathType Container) {
    $allowedConfigDirectories = @(
        "config\cmake",
        "config\openocd",
        "config\verification"
    )
    $configDirectories = @($repositoryDirectories |
        Where-Object {
            Test-TemplateEntryUnderDirectory -Entry $_ `
                -RelativeDirectory "config"
        })
    foreach ($directory in $configDirectories) {
        $relative = (Get-RelativeTemplatePath -Path $directory.FullName).Replace("/", "\")
        if ($relative -notin $allowedConfigDirectories) {
            Add-Finding -Path $directory.FullName -Line 1 -Reason (
                "unexpected configuration directory under config")
        }
    }

    $configFiles = @($repositoryFiles |
        Where-Object {
            Test-TemplateEntryUnderDirectory -Entry $_ `
                -RelativeDirectory "config"
        })
    foreach ($file in $configFiles) {
        $relative = (Get-RelativeTemplatePath -Path $file.FullName).Replace("/", "\")
        $allowed = (
            $relative -ceq "config\README.md" -or
            $relative -cmatch '^config\\[^\\]+_config\.h$' -or
            $relative -cmatch '^config\\cmake\\[^\\]+\.cmake$' -or
            $relative -ceq "config\openocd\README.md" -or
            $relative -cmatch '^config\\openocd\\[^\\]+\.cfg(?:\.example)?$' -or
            $relative -ceq "config\verification\components.json"
        )
        if (-not $allowed) {
            Add-Finding -Path $file.FullName -Line 1 -Reason (
                "non-declarative file is not allowed under config")
        }
    }

    $openOcdRoot = Join-Path $configRoot "openocd"
    if (Test-Path -LiteralPath $openOcdRoot -PathType Container) {
        $openOcdCfgFiles = $repositoryFiles |
            Where-Object {
                (Test-TemplateEntryUnderDirectory -Entry $_ `
                    -RelativeDirectory "config\openocd") -and
                $_.Name -match '\.cfg(?:\.example)?$'
            }
        foreach ($file in $openOcdCfgFiles) {
            $serialMatches = @(Select-String -LiteralPath $file.FullName -Pattern (
                '^\s*adapter\s+serial\b'))
            foreach ($serialMatch in $serialMatches) {
                Add-Finding -Path $file.FullName -Line $serialMatch.LineNumber -Reason (
                    "real probe selection must stay in sibling TI ENV/local/openocd")
            }
        }
    }
}

$docsRoot = Join-Path $Root "docs"
if (Test-Path -LiteralPath $docsRoot -PathType Container) {
    $nonMarkdownDocs = $repositoryFiles |
        Where-Object {
            Test-TemplateEntryUnderDirectory -Entry $_ `
                -RelativeDirectory "docs"
        } |
        Where-Object { $_.Extension -cne ".md" }
    foreach ($file in $nonMarkdownDocs) {
        Add-Finding -Path $file.FullName -Line 1 -Reason (
            "only project records in Markdown form are allowed under docs")
    }
}

$gitIgnorePath = Join-Path $Root ".gitignore"
$gitIgnoreLines = @(
    Get-Content -LiteralPath $gitIgnorePath -Encoding UTF8 |
        ForEach-Object { $_.Trim() }
)
if ("/build/" -notin $gitIgnoreLines) {
    Add-Finding -Path $gitIgnorePath -Line 1 -Reason (
        "generated build root must remain explicitly ignored")
}

$upperLayerRoots = @(
    "APP",
    "BSP\include",
    "Device",
    "Mode",
    "Task"
)
$sourceFiles = @($repositoryFiles |
    Where-Object { $_.Extension -in @(".c", ".h") })
$forbiddenPatterns = @(
    @{
        Pattern = '#\s*include\s*[<"](?:ti_msp_dl_config|ti/driverlib|driverlib)'
        Reason  = "upper layer includes MCU/DriverLib headers"
    },
    @{
        Pattern = '\bDL_[A-Za-z0-9_]+'
        Reason  = "upper layer calls DriverLib directly"
    },
    @{
        Pattern = '\b(?:PINCM\d+|GPIO[ABC]|TIMA\d+|TIMG\d+|UART\d+|DMA_CH\d+)\b'
        Reason  = "upper layer contains a fixed peripheral or pin token"
    },
    @{
        Pattern = '\b__MSPM0[A-Za-z0-9_]*__\b'
        Reason  = "upper layer branches on an MCU macro"
    }
)

foreach ($relativeRoot in $upperLayerRoots) {
    $directory = Join-Path $Root $relativeRoot
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
        continue
    }

    $files = @($sourceFiles |
        Where-Object {
            Test-TemplateEntryUnderDirectory -Entry $_ `
                -RelativeDirectory $relativeRoot
        })
    foreach ($file in $files) {
        foreach ($rule in $forbiddenPatterns) {
            $matches = @(Select-String -LiteralPath $file.FullName -Pattern $rule.Pattern -AllMatches)
            foreach ($match in $matches) {
                Add-Finding -Path $file.FullName -Line $match.LineNumber -Reason $rule.Reason
            }
        }
    }
}

# APP is the pure algorithm layer. Product modes and execution concerns must
# not leak back into reusable math and protocol components.
$appDirectory = Join-Path $Root "APP"
if (Test-Path -LiteralPath $appDirectory -PathType Container) {
    $appFiles = @($sourceFiles |
        Where-Object {
            Test-TemplateEntryUnderDirectory -Entry $_ -RelativeDirectory "APP"
        })
    $appDependencyPatterns = @(
        @{
            Pattern = '#\s*include\s*[<"](?:bsp_|device_|mode_|FreeRTOS|task\.h)'
            Reason = "APP algorithm includes BSP, Device, Mode, or FreeRTOS"
        },
        @{
            Pattern = '\b(?:xTask|vTask|osThread|TaskHandle|xQueue|xSemaphore)[A-Za-z0-9_]*\b'
            Reason = "APP algorithm contains task/RTOS scheduling code"
        }
    )
    foreach ($file in $appFiles) {
        foreach ($rule in $appDependencyPatterns) {
            $matches = @(Select-String -LiteralPath $file.FullName -Pattern $rule.Pattern -AllMatches)
            foreach ($match in $matches) {
                Add-Finding -Path $file.FullName -Line $match.LineNumber -Reason $rule.Reason
            }
        }
    }
}

# Mode owns business state and safety decisions. It may depend on APP
# algorithms, but not on hardware ownership or FreeRTOS scheduling.
$modeDirectory = Join-Path $Root "Mode"
if (Test-Path -LiteralPath $modeDirectory -PathType Container) {
    $modeFiles = @($sourceFiles |
        Where-Object {
            Test-TemplateEntryUnderDirectory -Entry $_ -RelativeDirectory "Mode"
        })
    $modeDependencyPatterns = @(
        @{
            Pattern = '#\s*include\s*[<"](?:bsp_|device_|app_|profile_)'
            Reason = "Mode includes BSP, Device, APP, or Product wrapper"
        },
        @{
            Pattern = '#\s*include\s*[<"](?:FreeRTOS|task|queue|semphr|timers)\.h[>"]'
            Reason = "Mode includes an RTOS/task header"
        },
        @{
            Pattern = '\b(?:xTask|vTask|osThread|TaskHandle|xQueue|xSemaphore)[A-Za-z0-9_]*\b'
            Reason = "Mode contains task/RTOS scheduling code"
        }
    )
    foreach ($file in $modeFiles) {
        foreach ($rule in $modeDependencyPatterns) {
            $matches = @(Select-String -LiteralPath $file.FullName -Pattern $rule.Pattern -AllMatches)
            foreach ($match in $matches) {
                Add-Finding -Path $file.FullName -Line $match.LineNumber -Reason $rule.Reason
            }
        }
    }
}

# Task owns FreeRTOS execution and Device/Mode data movement, but never MCU
# registers or BSP backends directly.
$taskDirectory = Join-Path $Root "Task"
if (Test-Path -LiteralPath $taskDirectory -PathType Container) {
    $taskSources = @($sourceFiles |
        Where-Object {
            Test-TemplateEntryUnderDirectory -Entry $_ -RelativeDirectory "Task"
        })
    $taskHardwarePatterns = @(
        @{
            Pattern = '#\s*include\s*[<"](?:bsp_|ti_msp_dl_config|ti/driverlib|driverlib)'
            Reason = "Task bypasses Device and accesses BSP/MCU directly"
        },
        @{
            Pattern = '\bDL_[A-Za-z0-9_]+'
            Reason = "Task calls DriverLib directly"
        }
    )
    foreach ($file in $taskSources) {
        foreach ($rule in $taskHardwarePatterns) {
            $matches = @(Select-String -LiteralPath $file.FullName -Pattern $rule.Pattern -AllMatches)
            foreach ($match in $matches) {
                Add-Finding -Path $file.FullName -Line $match.LineNumber -Reason $rule.Reason
            }
        }
    }

    $taskEntryPath = Join-Path $taskDirectory "task_app.c"
    if (-not (Test-Path -LiteralPath $taskEntryPath -PathType Leaf)) {
        Add-Finding -Path $taskEntryPath -Line 1 -Reason "FreeRTOS task entry is missing"
    }
    else {
        $taskEntry = Get-Content -LiteralPath $taskEntryPath -Raw -Encoding UTF8
        foreach ($requiredToken in @(
                '#include "FreeRTOS.h"',
                '#include "task.h"',
                'xTaskCreateStatic',
                'vTaskDelayUntil')) {
            if (-not $taskEntry.Contains($requiredToken)) {
                Add-Finding -Path $taskEntryPath -Line 1 -Reason (
                    "FreeRTOS task entry is missing '{0}'" -f $requiredToken)
            }
        }
    }
}

# A healthy non-critical caller must never mask a dead control Task. Keep both
# watchdog feed APIs single-owner even though their headers are public.
$watchdogFeedRules = @(
    @{
        Pattern = '\bDevice_Watchdog_Feed\s*\('
        Allowed = @(
            "Device\System\Watchdog\device_watchdog.c",
            "Task\task_app.c"
        )
        Reason = "Device watchdog feed must be owned only by Task/task_app.c"
    },
    @{
        Pattern = '\bBSP_Watchdog_Feed\s*\('
        Allowed = @(
            "BSP\MSPM0G3507\bsp_watchdog.c",
            "BSP\MSPM0G3519\bsp_watchdog.c",
            "Device\System\Watchdog\device_watchdog.c"
        )
        Reason = "BSP watchdog feed must be reached only through Device_Watchdog"
    }
)
$watchdogSources = @($sourceFiles |
    Where-Object { $_.Extension -ceq ".c" })
foreach ($rule in $watchdogFeedRules) {
    foreach ($file in $watchdogSources) {
        $relative = (Get-RelativeTemplatePath -Path $file.FullName).Replace("/", "\")
        if ($relative -in $rule.Allowed) {
            continue
        }
        $matches = @(Select-String -LiteralPath $file.FullName -Pattern $rule.Pattern -AllMatches)
        foreach ($match in $matches) {
            Add-Finding -Path $file.FullName -Line $match.LineNumber -Reason $rule.Reason
        }
    }
}

$sourceLikeFiles = @($repositoryFiles |
    Where-Object { $_.Extension -in @(".c", ".h", ".a", ".lib") })
foreach ($file in $sourceLikeFiles) {
    $relative = Get-RelativeTemplatePath -Path $file.FullName
    if ($file.Extension -in @(".a", ".lib")) {
        Add-Finding -Path $file.FullName -Line 1 -Reason "precompiled library is not allowed outside build"
    }
    elseif ($relative -match '(?i)(?:^|[\\/])SeekFree(?:[\\/]|$)' -or
        $file.Name -match '(?i)^zf_') {
        Add-Finding -Path $file.FullName -Line 1 -Reason "forbidden SeekFree/zf source name"
    }
}

$cmakeFiles = @($repositoryFiles |
    Where-Object {
        $_.Name -eq "CMakeLists.txt" -or $_.Extension -eq ".cmake"
    })
foreach ($file in $cmakeFiles) {
    $matches = @(Select-String -LiteralPath $file.FullName -Pattern '^\s*file\s*\(\s*GLOB(?:_RECURSE)?\b')
    foreach ($match in $matches) {
        Add-Finding -Path $file.FullName -Line $match.LineNumber -Reason "source glob would compile files implicitly"
    }
}

$jsonFiles = @(
    "CMakePresets.json",
    ".vscode\c_cpp_properties.json",
    ".vscode\extensions.json",
    ".vscode\launch.json",
    ".vscode\settings.json",
    ".vscode\tasks.json"
)
foreach ($relative in $jsonFiles) {
    $path = Join-Path $Root $relative
    try {
        Get-Content -LiteralPath $path -Raw -Encoding UTF8 |
            ConvertFrom-Json | Out-Null
    }
    catch {
        Add-Finding -Path $path -Line 1 -Reason "invalid JSON"
    }
}

# The incremental verifier manifest is executable project metadata. Validate
# references here so a typo cannot silently skip tests or firmware presets.
$manifestPath = Join-Path $Root "config\verification\components.json"
try {
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 |
        ConvertFrom-Json
    $schemaProperty = $manifest.PSObject.Properties["schemaVersion"]
    if ($null -eq $schemaProperty -or $schemaProperty.Value -ne 2) {
        Add-Finding -Path $manifestPath -Line 1 -Reason (
            "verification manifest schemaVersion must be 2")
    }

    foreach ($field in @("globalInputs", "contextInputs")) {
        $property = $manifest.PSObject.Properties[$field]
        if ($null -eq $property -or $property.Value -isnot [System.Array]) {
            Add-Finding -Path $manifestPath -Line 1 -Reason (
                "verification manifest '{0}' must be an array" -f $field)
            continue
        }
        foreach ($entry in @($property.Value)) {
            [void](Test-ManifestInputPath -ManifestPath $manifestPath `
                    -Field $field -Entry $entry)
        }
    }

    $coreConfigFullPath = $null
    $coreProperty = $manifest.PSObject.Properties["coreConfig"]
    if ($null -eq $coreProperty) {
        Add-Finding -Path $manifestPath -Line 1 -Reason (
            "verification manifest is missing 'coreConfig'")
    }
    else {
        $coreConfigFullPath = Test-ManifestInputPath `
            -ManifestPath $manifestPath -Field "coreConfig" `
            -Entry $coreProperty.Value
        if ($null -ne $coreConfigFullPath -and
            -not (Test-Path -LiteralPath $coreConfigFullPath -PathType Leaf)) {
            Add-Finding -Path $manifestPath -Line 1 -Reason (
                "coreConfig must reference a file")
            $coreConfigFullPath = $null
        }
    }

    $hostScopes = @(Get-ScriptParameterValidateSet `
            -Path (Join-Path $Root "tests\run_gcc_tests.ps1") `
            -ParameterName "Scope")
    $armScopes = @(Get-ScriptParameterValidateSet `
            -Path (Join-Path $Root "tests\run_arm_compile.ps1") `
            -ParameterName "Scope")
    $presetDocument = Get-Content -LiteralPath (
        Join-Path $Root "CMakePresets.json") -Raw -Encoding UTF8 |
        ConvertFrom-Json
    $buildPresets = @($presetDocument.buildPresets |
        ForEach-Object { [string]$_.name })

    $componentsProperty = $manifest.PSObject.Properties["components"]
    if ($null -eq $componentsProperty -or
        $componentsProperty.Value -isnot [System.Array] -or
        @($componentsProperty.Value).Count -eq 0) {
        throw "verification manifest 'components' must be a non-empty array"
    }

    $seenIds = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    $configPrefixes = [System.Collections.Generic.List[string]]::new()
    foreach ($component in @($componentsProperty.Value)) {
        if ($null -eq $component) {
            Add-Finding -Path $manifestPath -Line 1 -Reason (
                "verification manifest contains an empty component")
            continue
        }

        $idProperty = $component.PSObject.Properties["id"]
        $componentId = if ($null -eq $idProperty) {
            ""
        }
        else {
            [string]$idProperty.Value
        }
        $validId = $componentId -cmatch '^[A-Za-z][A-Za-z0-9_-]*$'
        $duplicateId = $validId -and $seenIds.Contains($componentId)
        if (-not $validId) {
            Add-Finding -Path $manifestPath -Line 1 -Reason (
                "invalid component id '{0}'" -f $componentId)
        }
        elseif ($duplicateId) {
            Add-Finding -Path $manifestPath -Line 1 -Reason (
                "duplicate component id '{0}'" -f $componentId)
        }

        $fieldValues = @{}
        foreach ($field in @(
                "inputs",
                "dependencies",
                "configPrefixes",
                "hostScopes",
                "armScopes",
                "buildPresets")) {
            $property = $component.PSObject.Properties[$field]
            if ($null -eq $property -or
                $property.Value -isnot [System.Array]) {
                Add-Finding -Path $manifestPath -Line 1 -Reason (
                    "component '{0}' field '{1}' must be an array" -f
                    $componentId, $field)
                $fieldValues[$field] = @()
            }
            else {
                $fieldValues[$field] = @($property.Value)
            }
        }

        foreach ($dependency in @($fieldValues["dependencies"])) {
            if ($dependency -isnot [string] -or
                -not $seenIds.Contains($dependency)) {
                Add-Finding -Path $manifestPath -Line 1 -Reason (
                    "component '{0}' dependency '{1}' must reference an earlier component id" -f
                    $componentId, $dependency)
            }
        }
        if ($validId -and -not $duplicateId) {
            [void]$seenIds.Add($componentId)
        }

        foreach ($entry in @($fieldValues["inputs"])) {
            [void](Test-ManifestInputPath -ManifestPath $manifestPath `
                    -Field ("component '{0}' inputs" -f $componentId) `
                    -Entry $entry)
        }
        foreach ($scopeRule in @(
                @{
                    Field = "hostScopes"
                    Valid = $hostScopes
                    Kind = "host test"
                },
                @{
                    Field = "armScopes"
                    Valid = $armScopes
                    Kind = "ARM compile"
                })) {
            foreach ($scope in @($fieldValues[$scopeRule.Field])) {
                if ($scope -isnot [string] -or $scope -notin $scopeRule.Valid) {
                    Add-Finding -Path $manifestPath -Line 1 -Reason (
                        "component '{0}' references unknown {1} scope '{2}'" -f
                        $componentId, $scopeRule.Kind, $scope)
                }
            }
        }
        foreach ($preset in @($fieldValues["buildPresets"])) {
            if ($preset -isnot [string] -or $preset -notin $buildPresets) {
                Add-Finding -Path $manifestPath -Line 1 -Reason (
                    "component '{0}' references unknown build preset '{1}'" -f
                    $componentId, $preset)
            }
        }
        foreach ($prefix in @($fieldValues["configPrefixes"])) {
            if ($prefix -isnot [string] -or
                $prefix -cnotmatch '^TEMPLATE_[A-Za-z0-9_]+$') {
                Add-Finding -Path $manifestPath -Line 1 -Reason (
                    "component '{0}' has invalid config prefix '{1}'" -f
                    $componentId, $prefix)
            }
            else {
                $configPrefixes.Add($prefix)
            }
        }
    }

    if ($null -ne $coreConfigFullPath) {
        $macroMatches = @(Select-String -LiteralPath $coreConfigFullPath `
                -Pattern '^\s*#\s*define\s+(TEMPLATE_[A-Za-z0-9_]+)\b' |
            Where-Object {
                $_.Matches[0].Groups[1].Value -cne "TEMPLATE_CORE_CONFIG_H"
            })
        foreach ($macroMatch in $macroMatches) {
            $macroName = $macroMatch.Matches[0].Groups[1].Value
            $covered = @($configPrefixes | Where-Object {
                    $macroName.StartsWith(
                        $_,
                        [System.StringComparison]::Ordinal)
                }).Count -ne 0
            if (-not $covered) {
                Add-Finding -Path $coreConfigFullPath `
                    -Line $macroMatch.LineNumber -Reason (
                    "core configuration macro '{0}' is not covered by any component configPrefixes" -f
                    $macroName)
            }
        }
    }
}
catch {
    Add-Finding -Path $manifestPath -Line 1 -Reason (
        "cannot validate verification manifest: {0}" -f $_.Exception.Message)
}

$tasksPath = Join-Path $Root ".vscode\tasks.json"
$launchPath = Join-Path $Root ".vscode\launch.json"
try {
    $tasksDocument = Get-Content -LiteralPath $tasksPath -Raw -Encoding UTF8 |
        ConvertFrom-Json
    $launchDocument = Get-Content -LiteralPath $launchPath -Raw -Encoding UTF8 |
        ConvertFrom-Json
    $taskLabels = @($tasksDocument.tasks | ForEach-Object { $_.label })

    $duplicateTaskLabels = @($taskLabels |
        Group-Object |
        Where-Object { $_.Count -gt 1 } |
        ForEach-Object { $_.Name })
    foreach ($duplicateTaskLabel in $duplicateTaskLabels) {
        Add-Finding -Path $tasksPath -Line 1 -Reason (
            "duplicate task label '{0}'" -f $duplicateTaskLabel)
    }

    foreach ($task in $tasksDocument.tasks) {
        $dependsOnProperty = $task.PSObject.Properties["dependsOn"]
        if ($null -eq $dependsOnProperty) {
            continue
        }

        foreach ($dependency in @($dependsOnProperty.Value)) {
            if ([string]::IsNullOrWhiteSpace($dependency) -or
                $dependency -notin $taskLabels) {
                Add-Finding -Path $tasksPath -Line 1 -Reason (
                    "unknown dependsOn task '{0}'" -f $dependency)
            }
        }
    }

    foreach ($configuration in $launchDocument.configurations) {
        $configFilesProperty =
            $configuration.PSObject.Properties["configFiles"]
        if ($null -ne $configFilesProperty) {
            foreach ($configReference in @($configFilesProperty.Value)) {
                $openOcdPrefix = '${workspaceFolder}/config/openocd/'
                if (-not $configReference.StartsWith(
                        $openOcdPrefix,
                        [System.StringComparison]::Ordinal)) {
                    Add-Finding -Path $launchPath -Line 1 -Reason (
                        "OpenOCD configFiles must resolve under config/openocd")
                    continue
                }

                $workspacePrefix = '${workspaceFolder}/'
                $relativeConfigPath = $configReference.Substring(
                    $workspacePrefix.Length).Replace("/", "\")
                $configPath = Join-Path $Root $relativeConfigPath
                if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
                    Add-Finding -Path $launchPath -Line 1 -Reason (
                        "OpenOCD configFiles target does not exist: {0}" -f
                        $configReference)
                }
            }
        }

        $preLaunchTaskProperty =
            $configuration.PSObject.Properties["preLaunchTask"]
        if ($null -eq $preLaunchTaskProperty) {
            continue
        }

        $preLaunchTask = $preLaunchTaskProperty.Value
        if (-not [string]::IsNullOrWhiteSpace($preLaunchTask) -and
            $preLaunchTask -notin $taskLabels) {
            Add-Finding -Path $launchPath -Line 1 -Reason (
                "unknown preLaunchTask '{0}'" -f $preLaunchTask)
        }
    }
}
catch {
    Add-Finding -Path $launchPath -Line 1 -Reason (
        "cannot validate VS Code task references: {0}" -f $_.Exception.Message)
}

if ($findings.Count -ne 0) {
    Write-Host "[FAIL] Template architecture check" -ForegroundColor Red
    foreach ($finding in $findings) {
        Write-Host ("  {0}" -f $finding)
    }
    exit 1
}

Write-Host "[OK]   Template architecture check" -ForegroundColor Green
Write-Host "       Upper layers do not own MCU resources; VS Code task references are valid."
Write-Host "       APP/Mode stay pure; Task owns FreeRTOS execution without bypassing Device."
Write-Host "       No source glob, forbidden source name, or precompiled library found."
Write-Host "       Config, docs, and generated build output have separate enforced roots."
Write-Host "       Incremental verification inputs, scopes, dependencies, and presets are valid."
