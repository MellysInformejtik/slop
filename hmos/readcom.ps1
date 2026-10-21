# Append the hmos USB ACM log (COM3) to a file until the port goes away.
param([string]$Port = 'COM3', [string]$Out = 'C:\Users\paw\hmos\out\com.log')
$p = New-Object System.IO.Ports.SerialPort $Port, 115200
$p.ReadTimeout = 3000
$p.Open()
try {
  while ($p.IsOpen) {
    try { $s = $p.ReadExisting(); if ($s) { [IO.File]::AppendAllText($Out, $s) } } catch [TimeoutException] { }
    Start-Sleep -Milliseconds 200
  }
} catch { [IO.File]::AppendAllText($Out, "`n[readcom: $($_.Exception.Message)]`n") }
finally { if ($p.IsOpen) { $p.Close() } }
