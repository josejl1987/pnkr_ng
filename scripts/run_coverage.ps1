# scripts/run_coverage.ps1
$ErrorActionPreference = "Stop"

# Auto-detect Ninja path if not found
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    $PotentialPaths = @(
        "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja",
        "C:\Program Files\Meson"
    )
    foreach ($path in $PotentialPaths) {
        if (Test-Path "$path\ninja.exe") {
            Write-Host "Found Ninja at: $path"
            $env:PATH = "$path;$env:PATH"
            break
        }
    }
}

Write-Host "=== PNKR Coverage Report Generation ==="

# 1. Configure
Write-Host "`n[1/3] Configuring with 'coverage' preset..."
try {
    cmake --preset coverage
} catch {
    Write-Error "Configuration failed. Please check if the 'coverage' preset dependencies (LLVM, Ninja) are installed."
    exit 1
}

# 2. Add DLL paths to environment for tests
Write-Host "`n[1.5/3] Setting up DLL environment..."
$DllPaths = @(
    "$PSScriptRoot/../build-coverage/bin",
    "$PSScriptRoot/../build-coverage/vcpkg_installed/x64-windows/debug/bin",
    "$PSScriptRoot/../build-coverage/vcpkg_installed/x64-windows/bin"
)
foreach ($path in $DllPaths) {
    if (Test-Path $path) {
        $AbsPath = Resolve-Path $path
        Write-Host "Adding to PATH: $AbsPath"
        $env:PATH = "$AbsPath;$env:PATH"
    }
}

# 3. Build & Run Tests
Write-Host "`n[2/3] Running tests and generating coverage..."

# We build the coverage targets which runs tests + generates reports
cmake --build --preset coverage --target pnkr_tests_coverage
if ($LASTEXITCODE -ne 0) { Write-Warning "pnkr_tests_coverage reported failures or tests failed." }

cmake --build --preset coverage --target pnkr_vulkan_tests_coverage
if ($LASTEXITCODE -ne 0) { Write-Warning "pnkr_vulkan_tests_coverage reported failures or tests failed." }

# 3. Summary
$CoverageDir = "build-coverage/tests/coverage_report"
Write-Host "`n[3/3] Done!"
Write-Host "Reports should be available at: $CoverageDir"
