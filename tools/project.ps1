[CmdletBinding()]
param(
    [ValidateSet("Context", "Verify", "CacheStatus", "BaselineStatus")]
    [string]$Action = "Context",
    [string]$Scope = "Auto",
    [string]$OutputPath,
    [switch]$Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$modulePath = Join-Path $PSScriptRoot "lib\ProjectTools.psm1"
if (-not (Test-Path -LiteralPath $modulePath -PathType Leaf)) {
    throw "Project tool module is missing: $modulePath"
}

Import-Module $modulePath -Force
Invoke-TemplateProjectTool -Action $Action -Scope $Scope -OutputPath $OutputPath -Clean:$Clean
