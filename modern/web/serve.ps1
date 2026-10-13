# D2Next in the browser: a small static web server that needs nothing installed (Windows PowerShell 5.1 or 7).
#
#   powershell -ExecutionPolicy Bypass -File modern\web\serve.ps1            # serves out\web, opens the browser
#   powershell -ExecutionPolicy Bypass -File modern\web\serve.ps1 -Port 9000 -NoBrowser
#
# It serves the built page (out\web-dist, or the folder this script sits in when it was copied next to D2Next.html) on
# http://localhost only. When the project's MPQ\ folder is present it is also served under /MPQ/, so a developer can
# load the archives without picking them in the page; players pick their own Diablo II folder instead.
param(
    [int]$Port = 8060,
    [string]$Root = "",
    [switch]$NoBrowser
)
$ErrorActionPreference = "Stop"

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$project = Resolve-Path (Join-Path $here "..\..") -ErrorAction SilentlyContinue
if (-not $Root) {
    if (Test-Path (Join-Path $here "D2Next.html")) { $Root = $here }
    elseif ($project -and (Test-Path (Join-Path $project "out\web-dist\D2Next.html"))) { $Root = Join-Path $project "out\web-dist" }
    elseif ($project -and (Test-Path (Join-Path $project "out\web\D2Next.html"))) { $Root = Join-Path $project "out\web" }
}
if (-not $Root -or -not (Test-Path (Join-Path $Root "D2Next.html"))) {
    Write-Host "D2Next.html was not found. Build the web version first with Build-Web.cmd." -ForegroundColor Red
    exit 1
}
$Root = (Resolve-Path $Root).Path
$mpqRoot = $null
if ($project -and (Test-Path (Join-Path $project "MPQ\d2data.mpq"))) { $mpqRoot = (Resolve-Path (Join-Path $project "MPQ")).Path }

$types = @{
    ".html" = "text/html; charset=utf-8"; ".js" = "text/javascript; charset=utf-8"; ".wasm" = "application/wasm";
    ".json" = "application/json"; ".css" = "text/css"; ".png" = "image/png"; ".ico" = "image/x-icon";
    ".mpq" = "application/octet-stream"; ".d2s" = "application/octet-stream"; ".data" = "application/octet-stream"
}

# The first free port from the one asked for: a second copy of the server, or another program, may hold it.
$listener = $null
for ($p = $Port; $p -lt $Port + 20; $p++) {
    $candidate = New-Object System.Net.HttpListener
    $candidate.Prefixes.Add("http://localhost:$p/")
    try { $candidate.Start(); $listener = $candidate; $Port = $p; break } catch { $candidate.Close() }
}
if (-not $listener) { Write-Host "No free port between $Port and $($Port + 19)." -ForegroundColor Red; exit 1 }

$url = "http://localhost:$Port/D2Next.html"
Write-Host ""
Write-Host "  D2Next is running at $url" -ForegroundColor Yellow
Write-Host "  Keep this window open while you play; close it (or press Ctrl+C) to stop."
if ($mpqRoot) { Write-Host "  Developer MPQs: $mpqRoot" -ForegroundColor DarkGray }
Write-Host ""
if (-not $NoBrowser) { Start-Process $url }

function Send-File($context, $path) {
    $response = $context.Response
    $info = Get-Item -LiteralPath $path
    $type = $types[$info.Extension.ToLowerInvariant()]
    if (-not $type) { $type = "application/octet-stream" }
    $response.ContentType = $type
    $response.Headers.Add("Cache-Control", "no-cache")
    # The page is served with these so a later threaded build (SharedArrayBuffer) works without changing the server.
    $response.Headers.Add("Cross-Origin-Opener-Policy", "same-origin")
    $response.Headers.Add("Cross-Origin-Embedder-Policy", "require-corp")
    $response.ContentLength64 = $info.Length
    if ($context.Request.HttpMethod -ne "HEAD") {
        $stream = [System.IO.File]::OpenRead($path)
        try { $stream.CopyTo($response.OutputStream, 1MB) } finally { $stream.Close() }
    }
}

try {
    while ($listener.IsListening) {
        $context = $listener.GetContext()
        try {
            $relative = [Uri]::UnescapeDataString($context.Request.Url.AbsolutePath).TrimStart("/")
            if (-not $relative) { $relative = "D2Next.html" }
            $base = $Root
            if ($mpqRoot -and $relative -match "^(?i)mpq/(.+)$") { $base = $mpqRoot; $relative = $Matches[1] }
            $file = [System.IO.Path]::GetFullPath((Join-Path $base $relative))
            if (-not $file.StartsWith($base, [StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $file -PathType Leaf)) {
                $context.Response.StatusCode = 404
            } else {
                Send-File $context $file
            }
        } catch {
            try { $context.Response.StatusCode = 500 } catch {}
        } finally {
            try { $context.Response.Close() } catch {}
        }
    }
} finally {
    $listener.Stop()
}
