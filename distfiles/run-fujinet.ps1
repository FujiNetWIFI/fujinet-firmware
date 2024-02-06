# PowerShell Runner run-fujinet.ps1
Write-Host "Starting FujiNet"

do {
    .\fujinet.exe $args[0] $args[1] $args[2] $args[3] $args[4] $args[5] $args[6] $args[7]
    
    if ($LASTEXITCODE -eq 75) {
        Write-Host "Restarting FujiNet"
    }
} while ($LASTEXITCODE -eq 75)

Write-Host "FujiNet ended with exit code $LASTEXITCODE"
