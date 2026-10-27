# Publishes the Rad Overhaul release through the Forgejo API.
#
# The CI runner has no gh CLI, and an inline PowerShell block in the workflow is at the mercy of
# the runner's expression handling, so the publishing logic lives in this file and is invoked with
# simple arguments. Create or update the release for -Tag, then replace that release's asset.
#
# Usage (from a workflow step):
#   pwsh -NoProfile -File tools/publish_release.ps1 -ApiBase $env:GITHUB_SERVER_URL -Repo $env:GITHUB_REPOSITORY `
#        -Tag $env:TAG -AssetPath $asset -NotesPath Rad-Overhaul-Fallout-4/RELEASE_NOTES.md
#
# -DryRun prints the requests it would make and never touches the network or the token.

[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ApiBase,
    [Parameter(Mandatory)][string]$Repo,
    [Parameter(Mandatory)][string]$Tag,
    [Parameter(Mandatory)][string]$AssetPath,
    [string]$NotesPath,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

# Constraint: Invoke-RestMethod -Form (the multipart asset upload) exists only in PowerShell 7+.
if (-not $DryRun -and $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'publishing needs PowerShell 7 (pwsh); this ran on PowerShell ' + $PSVersionTable.PSVersion
}

if (-not (Test-Path -LiteralPath $AssetPath)) { throw "asset not found: $AssetPath" }
$assetName = Split-Path -Leaf $AssetPath
$assetSize = (Get-Item -LiteralPath $AssetPath).Length
if ($assetSize -le 0) { throw "asset is empty: $AssetPath" }

$body = ''
if ($NotesPath -and (Test-Path -LiteralPath $NotesPath)) { $body = Get-Content -LiteralPath $NotesPath -Raw }

$base = $ApiBase.TrimEnd('/') + "/api/v1/repos/$Repo"
$token = $env:FORGEJO_TOKEN
if (-not $DryRun -and -not $token) { throw 'FORGEJO_TOKEN is not set' }

$headers = @{ Authorization = "token $token" }
$payload = @{
    tag_name   = $Tag
    name       = "Rad Overhaul $Tag"
    body       = $body
    draft      = $false
    prerelease = $false
} | ConvertTo-Json -Depth 3

if ($DryRun) {
    Write-Host "[dry-run] PUT release for $Tag at $base/releases"
    Write-Host "[dry-run] upload $assetName ($assetSize bytes) when the release exists"
    Write-Host "[dry-run] notes bytes: $($body.Length)"
    return
}

$release = $null
try {
    $release = Invoke-RestMethod -Uri "$base/releases/tags/$Tag" -Headers $headers -Method Get
} catch {
    $release = $null
}

if ($release) {
    Write-Host "updating existing release $($release.id) for $Tag"
    Invoke-RestMethod -Uri "$base/releases/$($release.id)" -Headers $headers -Method Patch `
        -ContentType 'application/json' -Body $payload | Out-Null
    if ($release.assets) {
        foreach ($existing in $release.assets) {
            if ($existing.name -eq $assetName) {
                Write-Host "removing existing asset $($existing.id) named $assetName"
                Invoke-RestMethod -Uri "$base/releases/$($release.id)/assets/$($existing.id)" -Headers $headers -Method Delete | Out-Null
            }
        }
    }
} else {
    Write-Host "creating release $Tag"
    $release = Invoke-RestMethod -Uri "$base/releases" -Headers $headers -Method Post `
        -ContentType 'application/json' -Body $payload
}

if (-not $release.id) { throw "release $Tag could not be created or read" }

# Constraint: Forgejo installs a release asset from the multipart field named attachment.
Invoke-RestMethod -Uri "$base/releases/$($release.id)/assets?name=$assetName" -Headers $headers -Method Post `
    -Form @{ attachment = Get-Item -LiteralPath $AssetPath } | Out-Null

Write-Host "published $Tag with $assetName ($assetSize bytes)"
