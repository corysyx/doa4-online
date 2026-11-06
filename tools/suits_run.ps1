param(
  [Parameter(Mandatory = $true)][string]$Name,
  [string]$Import = "",            # a folder from suits_runs\imports, copied into the run's user data
  [string]$EnvSpec = "",           # "KEY=VALUE;KEY=VALUE"
  [int]$Seconds = 178,
  [switch]$RealPads,                # let real controllers drive the run (default: SDL ignores every pad)
  # The build folder to run (e.g. ...\out\build\win-amd64-release\suits_bin\t9f1). No default: a run
  # overwrites that folder's dead_or_alive_4.toml, so it must never be a folder nobody named.
  [Parameter(Mandatory = $true)][string]$Bin
)
# One attract-mode run of the swimsuits build (plan 1 task 9). Waits (60 s steps) while another
# agent's netplay run or a game of the user's is running; starts the build with a fresh user-data
# folder (the import copied in), the real game folder read-only, photos into the run folder;
# stops only the process it started; copies the log. Never touches C:\Users\fsop1\doa4\userdata.
$ErrorActionPreference = "Stop"
$runs = "C:\Users\fsop1\doa4\suits_runs"
# Validate everything BEFORE the wait loop and before anything is created. The run name becomes a
# folder under $runs\runs, and that folder's userdata subfolder is where the game writes its saves:
# a name that resolves anywhere else (..\..\userdata\x, a slash, a drive) would point the saves there.
if ($Name -notmatch '^[A-Za-z0-9][A-Za-z0-9_.-]*$' -or $Name.Contains('..') -or $Name.EndsWith('.') -or
    $Name.Length -gt 64 -or $Name -match '^(con|prn|aux|nul|com[1-9]|lpt[1-9])(\..*)?$') {
  throw "bad run name '$Name': letters, digits, _ . - only, starting with a letter or digit, no '..', not a device name, at most 64 characters"
}
$run = "$runs\runs\$Name"
$runsRoot = [System.IO.Path]::GetFullPath("$runs\runs") + [System.IO.Path]::DirectorySeparatorChar
if (-not [System.IO.Path]::GetFullPath($run).StartsWith($runsRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "run folder $run is not inside $runsRoot"
}
if (Test-Path $run) { throw "run folder $run exists - pick a new name" }
if (-not (Test-Path -LiteralPath "$Bin\dead_or_alive_4.exe" -PathType Leaf)) { throw "-Bin $Bin holds no dead_or_alive_4.exe" }
if ($Import -and -not (Test-Path -LiteralPath $Import -PathType Container)) { throw "-Import $Import is not a folder" }
$waited = 0
while ($true) {
  $procs = Get-CimInstance Win32_Process | Where-Object { $_.Name -in 'dead_or_alive_4.exe', 'python.exe', 'py.exe', 'DOA2U.exe' }
  $busy = $procs | Where-Object {
    $_.Name -eq 'DOA2U.exe' -or $_.Name -eq 'dead_or_alive_4.exe' -or
    $_.CommandLine -match 'wt-np0|np_runs|np_run\.py|np_gate\.py' }
  if (-not $busy) { break }
  "busy: $(($busy | ForEach-Object { "$($_.Name) $($_.ProcessId)" }) -join ', ') - waiting 60 s"
  Start-Sleep -Seconds 60
  $waited += 60
  if ($waited -ge 7200) { throw "waited two hours - giving up" }
}
New-Item -ItemType Directory -Force "$run\userdata", "$run\photos" | Out-Null
if ($Import) { Copy-Item -Recurse $Import "$run\userdata\doax2_suits" }
$toml = @"
fullscreen = false
resolution_scale = 2
doa4_photo_format = "png_full"
doa4_photo_auto = false
doa4_photo_dir = "$("$run\photos" -replace '\\','/')"
doa4_user_data = "$("$run\userdata" -replace '\\','/')"
"@
Set-Content -Path "$Bin\dead_or_alive_4.toml" -Value $toml -Encoding ascii
$before = @()
if (Test-Path "$Bin\logs") { $before = Get-ChildItem "$Bin\logs" -Filter *.log | ForEach-Object Name }
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = "$Bin\dead_or_alive_4.exe"
$psi.Arguments = "--game_data_root=`"C:\Users\fsop1\doa4\xex\DOA4`" --user_data_root=`"$run\userdata`""
$psi.WorkingDirectory = $Bin
$psi.UseShellExecute = $false
foreach ($kv in ($EnvSpec -split ';')) {
  if ($kv -match '^([^=]+)=(.*)$') { $psi.EnvironmentVariables[$Matches[1]] = $Matches[2] }
}
# Real controllers off unless -RealPads. The runtime reads an attached SDL pad even while its window is in the
# background (SDL filters background joystick input only for windows SDL created), and no toml/cvar turns pads off
# (input_backend allows only sdl|xinput). SDL skips every gamepad whose VID/PID is not on this allow-list, and
# 0x0000/0x0000 matches none. An EnvSpec value for the same name wins.
$pads = if ($RealPads) { "real" } else { "off" }
if (-not $RealPads -and -not $psi.EnvironmentVariables.ContainsKey('SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT')) {
  $psi.EnvironmentVariables['SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT'] = '0x0000/0x0000'
}
$t0 = Get-Date
$p = [System.Diagnostics.Process]::Start($psi)
"started pid $($p.Id) name=$Name import=$Import env=$EnvSpec pads=$pads" | Tee-Object -FilePath "$run\run.txt" -Append
if (-not $p.WaitForExit($Seconds * 1000)) {
  Stop-Process -Id $p.Id -Force
  $p.WaitForExit(15000) | Out-Null
  "alive at $Seconds s (stopped)" | Tee-Object -FilePath "$run\run.txt" -Append
} else {
  "EXITED by itself after $([int]((Get-Date) - $t0).TotalSeconds) s, code $($p.ExitCode)" | Tee-Object -FilePath "$run\run.txt" -Append
}
Start-Sleep -Milliseconds 500
$new = Get-ChildItem "$Bin\logs" -Filter *.log | Where-Object { $before -notcontains $_.Name } | Sort-Object LastWriteTime | Select-Object -Last 1
if ($new) { Copy-Item $new.FullName "$run\game.log"; "log -> $run\game.log" } else { "no new log found" }
if ($new -and $pads -eq "off" -and (Select-String -LiteralPath "$run\game.log" -SimpleMatch "OnControllerDeviceAdded: Added" -Quiet)) {
  "WARNING: a real controller was attached although pads were off" | Tee-Object -FilePath "$run\run.txt" -Append
}
"photos: $((Get-ChildItem "$run\photos").Count)"
