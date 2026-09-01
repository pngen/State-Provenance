# ---------------------------------------------------------------------------
# State Provenance - build the CUDA hardware-backed proof.
# Uses nvcc 13.1 (CUDA 13.1) targeting arch sm_120 (RTX 5090) with the MSVC
# host toolchain.  The Visual Studio generator does not auto-discover the CUDA
# VS toolset, so this script drives nvcc directly with the same include paths
# and warning flags as the rest of the project.
# ---------------------------------------------------------------------------
param(
    [string]$SrcDir = (Split-Path $PSScriptRoot -Parent),
    [string]$BuildDir = "$PSScriptRoot\..\build",
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

$vcvarsCandidates = @(
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)
$vcvars = $vcvarsCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vcvars) { Write-Error "vcvars64.bat not found"; exit 1 }

$nvcc = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1\bin\nvcc.exe"
if (-not (Test-Path $nvcc)) { Write-Error "nvcc 13.1 not found: $nvcc"; exit 1 }

$out = Join-Path $BuildDir (Join-Path $Config "sp_cuda_proof.exe")
New-Item -ItemType Directory -Force -Path (Split-Path $out) | Out-Null

# Build the cmd command line. Backtick-escaped quotes keep the paths quoted.
$cmdLine = "call `"$vcvars`" && `"$nvcc`" -std=c++20 -arch=sm_120 " +
    "-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS " +
    "-D_WINSOCK_DEPRECATED_NO_WARNINGS -I `"$SrcDir\include`" " +
    "`"$SrcDir\cuda\cuda_proof.cu`" -o `"$out`""

Write-Host "compiling CUDA proof (nvcc 13.1 / sm_120)..."
& cmd /c $cmdLine
if ($LASTEXITCODE -ne 0) { Write-Error "nvcc failed with exit $LASTEXITCODE"; exit $LASTEXITCODE }
Write-Host "CUDA proof built: $out"