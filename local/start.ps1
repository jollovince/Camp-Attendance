$ErrorActionPreference = 'Stop'
$projectDir = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $projectDir
python -c "import pymysql, serial"
if ($LASTEXITCODE -ne 0) { throw 'Install local dependencies once: python -m pip install -r local/requirements.txt' }
if (-not (Test-Path -LiteralPath 'web/dist/index.html')) {
    npm.cmd --prefix web run build
    if ($LASTEXITCODE -ne 0) { throw 'Website build failed.' }
}
python local/server.py
