# Copyright 2024 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [string]$test_type,
    [string]$binaries_dir
)

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; 

try {
    switch ($test_type.ToLower()) {
        "unit" {
            $proc = Start-Process -PassThru -Wait -NoNewWindow "$binaries_dir\tests.exe" "--log-level=debug"
            if ($proc.ExitCode -ne 0) { throw "" }
        }
        "vst3val" {
            $proc = Start-Process -PassThru -Wait -NoNewWindow "$binaries_dir\VST3-Validator.exe" "$binaries_dir\Floe.vst3"
            if ($proc.ExitCode -ne 0) { throw "" }
        }
        "pluginval" {
            Invoke-WebRequest https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Windows.zip -OutFile pluginval.zip
            if (Test-Path pluginval.exe) { Remove-Item pluginval.exe }
            Expand-Archive pluginval.zip -DestinationPath .
            $proc = Start-Process -PassThru -Wait -NoNewWindow "pluginval.exe" "--verbose","--validate","$binaries_dir\Floe.vst3"
            if ($proc.ExitCode -ne 0) { throw "" }
        }
        "clapval" {
            Invoke-WebRequest https://github.com/free-audio/clap-validator/releases/download/0.3.2/clap-validator-0.3.2-windows.zip -OutFile clapval.zip
            if (Test-Path clap-validator.exe) { Remove-Item clap-validator.exe }
            Expand-Archive clapval.zip -DestinationPath .
            $proc = Start-Process -PassThru -Wait -NoNewWindow "clap-validator.exe"  "validate","$binaries_dir\Floe.clap"
            if ($proc.ExitCode -ne 0) { throw "" }
        }
        default {
            throw "Invalid test_type"
        }
    }

    Write-Host "SUCEEDED $test_type" -ForegroundColor Green
    exit 0
} catch {
    Write-Host "FAILED ${test_type} $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
