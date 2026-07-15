$ErrorActionPreference = "Continue"
$testDir = "e:\Soft\Proje\LXCLUA-NCore\lua\test"
$exe = "e:\Soft\Proje\LXCLUA-NCore\lua\lxclua.exe"

$passed = @()
$failed = @()

$testFiles = Get-ChildItem -Path $testDir -Recurse -Filter "*.lua" | Sort-Object FullName
$total = $testFiles.Count
$i = 0

foreach ($f in $testFiles) {
    $i++
    $relPath = $f.FullName.Substring($testDir.Length + 1)
    Write-Host -NoNewline "[$i/$total] $relPath ... "

    try {
        $proc = Start-Process -FilePath $exe -ArgumentList $f.FullName -NoNewWindow -Wait -PassThru -RedirectStandardOutput "$env:TEMP\lua_test_out.txt" -RedirectStandardError "$env:TEMP\lua_test_err.txt"
        $exitCode = $proc.ExitCode
        
        $stdout = Get-Content "$env:TEMP\lua_test_out.txt" -ErrorAction SilentlyContinue | Out-String
        $stderr = Get-Content "$env:TEMP\lua_test_err.txt" -ErrorAction SilentlyContinue | Out-String
        $combined = $stdout + $stderr

        $hasError = $false
        $errorMsg = ""

        if ($exitCode -ne 0) {
            $hasError = $true
            $errorMsg = "exit code=$exitCode"
        }

        if ($combined -match "(?i)(FAIL|error|too many registers|assertion failed|stack traceback|runtime error|syntax error|PANIC|abort|segmentation fault)") {
            if (-not $hasError) {
                $hasError = $true
            }
            if ($errorMsg) { $errorMsg += "; " }
            $errLines = ($combined -split "`n" | Where-Object { $_ -match "(?i)(FAIL|error|too many registers|assertion|traceback|PANIC|abort)" } | Select-Object -First 3) -join " | "
            if ($errLines.Length -gt 200) { $errLines = $errLines.Substring(0, 200) }
            $errorMsg = $errorMsg + "output: " + $errLines
        }

        if ($hasError) {
            Write-Host "FAIL" -ForegroundColor Red
            $obj = New-Object PSObject
            $obj | Add-Member -Name File -MemberType NoteProperty -Value $relPath
            $obj | Add-Member -Name ExitCode -MemberType NoteProperty -Value $exitCode
            $obj | Add-Member -Name Error -MemberType NoteProperty -Value $errorMsg
            $failed += $obj
        } else {
            Write-Host "OK" -ForegroundColor Green
            $obj = New-Object PSObject
            $obj | Add-Member -Name File -MemberType NoteProperty -Value $relPath
            $obj | Add-Member -Name ExitCode -MemberType NoteProperty -Value $exitCode
            $passed += $obj
        }
    } catch {
        Write-Host "CRASH" -ForegroundColor Red
        $obj = New-Object PSObject
        $obj | Add-Member -Name File -MemberType NoteProperty -Value $relPath
        $obj | Add-Member -Name ExitCode -MemberType NoteProperty -Value (-1)
        $obj | Add-Member -Name Error -MemberType NoteProperty -Value $_.Exception.Message
        $failed += $obj
    }
}

Write-Host ""
Write-Host "========== Test Summary ==========" -ForegroundColor Cyan
Write-Host "Total: $total" -ForegroundColor Cyan
Write-Host "Passed: $($passed.Count)" -ForegroundColor Green
Write-Host "Failed: $($failed.Count)" -ForegroundColor Red
Write-Host ""

if ($failed.Count -gt 0) {
    Write-Host "========== Failed Details ==========" -ForegroundColor Red
    foreach ($f in $failed) {
        $line = "  [" + $f.File + "] exit=" + $f.ExitCode + " | " + $f.Error
        Write-Host $line -ForegroundColor Red
    }
}

Remove-Item "$env:TEMP\lua_test_out.txt" -ErrorAction SilentlyContinue
Remove-Item "$env:TEMP\lua_test_err.txt" -ErrorAction SilentlyContinue