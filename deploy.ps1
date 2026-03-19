Stop-Process -Name SmoothZoom -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Copy-Item "C:\Dev\SmoothZoom\build\Debug\SmoothZoom.exe" "C:\Program Files\SmoothZoom\SmoothZoom.exe" -Force
$signtool = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe"
& $signtool sign /a /s My /n "SmoothZoom Dev" /fd SHA256 "C:\Program Files\SmoothZoom\SmoothZoom.exe"
Write-Output "Done. File size: $((Get-Item 'C:\Program Files\SmoothZoom\SmoothZoom.exe').Length)"
