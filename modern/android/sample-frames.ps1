param([string]$Serial='',[string]$Sdk="$env:LOCALAPPDATA/Android/Sdk")
$ErrorActionPreference='Stop'
$taskAdb="$Sdk/platform-tools/adb.exe"
$taskLayers=& $taskAdb -s $Serial shell dumpsys SurfaceFlinger --list
if($LASTEXITCODE) { throw 'Cannot list compositor layers' }
$taskLayer=$null
foreach($taskLine in $taskLayers) {
    if($taskLine -match '^RequestedLayerState\{(.+SurfaceView\[org\.d2next\.game/.+\]\(BLAST\)#\d+) parentId=') { $taskLayer=$Matches[1];break }
    if($taskLine -match '^.+SurfaceView.*org\.d2next\.game.*\(BLAST\)#\d+$') { $taskLayer=$taskLine;break }
}
if(!$taskLayer -or $taskLayer.Contains('"')) { throw 'Game compositor surface not found' }
$taskLatency=& $taskAdb -s $Serial shell ('dumpsys SurfaceFlinger --latency "'+$taskLayer+'"')
if($LASTEXITCODE) { throw 'Cannot read compositor timestamps' }
[long[]]$taskTimes=@($taskLatency | ForEach-Object {
    if($_ -match '^\s*\d+\s+(\d+)\s+\d+\s*$') {
        $taskStamp=[long]$Matches[1]
        if($taskStamp -gt 0 -and $taskStamp -lt [long]::MaxValue) { $taskStamp }
    }
} | Sort-Object -Unique)
if($taskTimes.Count -lt 3) { throw 'Not enough valid presented frames' }
[double[]]$taskIntervals=@(for($taskIndex=1;$taskIndex -lt $taskTimes.Count;$taskIndex++) { ($taskTimes[$taskIndex]-$taskTimes[$taskIndex-1])/1000000.0 })
[double[]]$taskSorted=@($taskIntervals | Sort-Object)
[pscustomobject]@{utc=[datetime]::UtcNow.ToString('o');serial=$Serial;layer=$taskLayer;frames=$taskTimes.Count;windowSeconds=($taskTimes[-1]-$taskTimes[0])/1e9;fps=1e9*($taskTimes.Count-1)/($taskTimes[-1]-$taskTimes[0]);p95ms=$taskSorted[[math]::Floor(($taskSorted.Count-1)*0.95)];maxMs=$taskSorted[-1];intervalsOver50ms=@($taskIntervals | Where-Object {$_ -gt 50}).Count;note='Short compositor presentation window, not a full-session frame trace.'} | ConvertTo-Json
