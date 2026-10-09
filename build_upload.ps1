param(
  [ValidateSet("build", "upload", "list", "monitor")] [string]$Action = "build",
  [string]$Port = "",
  [switch]$Uart
)
$ErrorActionPreference = "Stop"
$cli = Get-Command arduino-cli -ErrorAction Stop
$sketchPath = Join-Path $PSScriptRoot "DJ_Audio_Console_V6"
$configPath = Join-Path $PSScriptRoot $(if ($Uart) { "board-fqbn-uart.txt" } else { "board-fqbn.txt" })
$fqbn = (Get-Content -LiteralPath $configPath -Raw).Trim()
if ($Action -eq "list") {
  & $cli.Source board list
  exit $LASTEXITCODE
}
if (($Action -eq "upload" -or $Action -eq "monitor") -and [string]::IsNullOrWhiteSpace($Port)) {
  throw "Use -Port COMx from the 'list' command. No port is selected automatically."
}
if ($Action -eq "monitor") {
  & $cli.Source monitor --port $Port --config baudrate=115200
  exit $LASTEXITCODE
}
# Verify the saved menu options against the locally installed ESP32 core.
& $cli.Source board details --fqbn $fqbn | Out-Null
if ($LASTEXITCODE -ne 0) { throw "ESP32 core/board options are unavailable. Keep your working 3.x core and dependencies installed." }
& $cli.Source compile --fqbn $fqbn --output-dir (Join-Path $PSScriptRoot "build") $sketchPath
if ($LASTEXITCODE -ne 0) { throw "Compilation failed; upload was skipped." }
if ($Action -eq "upload") {
  & $cli.Source upload --fqbn $fqbn --port $Port --input-dir (Join-Path $PSScriptRoot "build") $sketchPath
  if ($LASTEXITCODE -ne 0) { throw "Upload failed." }
}
