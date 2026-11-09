param([Parameter(Mandatory=$true)][string]$Run)
$ErrorActionPreference='Stop'
$taskRun=(Resolve-Path -LiteralPath $Run).Path
$taskTerminal=Get-Content -LiteralPath "$taskRun/terminal.json" -Raw | ConvertFrom-Json
if($taskTerminal.status -ne 'sampling-complete') { throw 'Measurement has not completed successfully; preserve the live process or inspect its terminal failure.' }
$taskSamples=@(Get-Content -LiteralPath "$taskRun/samples.jsonl" | ForEach-Object {$_ | ConvertFrom-Json})
if($taskSamples.Count -ne $taskTerminal.minutes+1) { throw 'Sample count does not match the requested duration' }
for($taskIndex=0;$taskIndex -lt $taskSamples.Count;$taskIndex++) {
    $taskSample=$taskSamples[$taskIndex]
    if($taskSample.sample -ne $taskIndex -or $taskSample.pid -ne $taskTerminal.pid -or $null -eq $taskSample.pssKiB) { throw 'Missing, out-of-order or mismatched-PID sample' }
    if($taskIndex -gt 0 -and $taskSample.elapsedSeconds -le $taskSamples[$taskIndex-1].elapsedSeconds) { throw 'Sample time did not advance' }
}
$taskFirst=$taskSamples[0];$taskLast=$taskSamples[-1]
if($taskLast.elapsedSeconds-$taskFirst.elapsedSeconds -lt $taskTerminal.minutes*60) { throw 'Observed duration is shorter than requested' }
$taskTail=@($taskSamples | Where-Object {$_.elapsedSeconds -ge $taskLast.elapsedSeconds-1200})
$taskPss=$taskSamples | Measure-Object pssKiB -Minimum -Maximum
$taskTailPss=$taskTail | Measure-Object pssKiB -Minimum -Maximum
$taskTemperatures=@($taskSamples | Where-Object {$null -ne $_.batteryC})
$taskFrames=@($taskSamples | Where-Object {$null -ne $_.fps})
$taskSortedFps=@($taskFrames | ForEach-Object {[double]$_.fps} | Sort-Object)
$taskSummary=[ordered]@{
    status=$taskTerminal.status;pid=$taskTerminal.pid;samples=$taskSamples.Count
    durationSeconds=$taskLast.elapsedSeconds-$taskFirst.elapsedSeconds
    firstPssKiB=$taskFirst.pssKiB;lastPssKiB=$taskLast.pssKiB
    minPssKiB=$taskPss.Minimum;maxPssKiB=$taskPss.Maximum
    last20MinutesMinPssKiB=$taskTailPss.Minimum;last20MinutesMaxPssKiB=$taskTailPss.Maximum
    maxBatteryC=if($taskTemperatures.Count){($taskTemperatures | Measure-Object batteryC -Maximum).Maximum}else{$null}
    sampledFrameWindows=$taskFrames.Count
    minWindowFps=if($taskFrames.Count){$taskSortedFps[0]}else{$null}
    medianWindowFps=if($taskFrames.Count){($taskSortedFps[[int][math]::Floor(($taskFrames.Count-1)/2)]+$taskSortedFps[[int][math]::Floor($taskFrames.Count/2)])/2}else{$null}
    maxSampledFrameIntervalMs=if($taskFrames.Count){($taskFrames | Measure-Object frameMaxMs -Maximum).Maximum}else{$null}
    windowsContainingIntervalOver50ms=if($taskFrames.Count){@($taskFrames | Where-Object {$_.frameMaxMs -gt 50}).Count}else{$null}
    firstBotFrame=$taskFirst.lastBotFrame;lastBotFrame=$taskLast.lastBotFrame
    limits='PSS is Android-reported, not a leak verdict. Compositor windows are short samples, not a continuous frame trace. Same PID does not prove sanitizer, campaign or visual correctness. Account for concurrent tests before comparing runs.'
}
$taskSummary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$taskRun/measurement-summary.json"
$taskSummary | ConvertTo-Json -Depth 5
