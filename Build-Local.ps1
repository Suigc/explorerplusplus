param(
    [ValidateRange(0, 65535)][int]$BuildNumber = 60003,
    [string]$VCToolsVersion = '14.44.35207',
    [string]$MSBuildPath,
    [string]$SourceRoot = $PSScriptRoot,
    [switch]$SkipDependencyInstall
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $SourceRoot).Path
if (!$MSBuildPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'vswhere.exe is required to locate Visual Studio.' }
    $MSBuildPath = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\amd64\MSBuild.exe' | Select-Object -First 1
}
if (!$MSBuildPath -or !(Test-Path -LiteralPath $MSBuildPath)) { throw 'MSBuild was not found.' }

Push-Location $root
try {
    if (!$SkipDependencyInstall) {
        & '.\Explorer++\ThirdParty\vcpkg\vcpkg.exe' install --triplet x64-windows-static --disable-metrics
        if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed.' }
    }
    $properties = @(
        '/p:Configuration=Release', '/p:Platform=x64',
        "/p:SolutionDir=$root\Explorer++\", '/p:VisualStudioVersion=17.0',
        "/p:VCToolsVersion=$VCToolsVersion", "/p:VcpkgInstalledDir=$root\vcpkg_installed\",
        '/p:VcpkgTriplet=x64-windows-static', '/p:VcpkgManifestInstall=false',
        "/p:EXPLORERPLUSPLUS_BUILD_NUMBER=$BuildNumber", '/p:CL_MPCount=4',
        '/m:4', '/nologo', '/v:minimal'
    )
    & $MSBuildPath 'Explorer++\Explorer++\Explorer++.vcxproj' @properties
    if ($LASTEXITCODE -ne 0) { throw 'Application build failed.' }
    # The resource-only translation project uses Win32 for all application architectures.
    $translationProperties = @($properties | Where-Object { $_ -ne '/p:Platform=x64' }) + '/p:Platform=Win32'
    & $MSBuildPath 'Translations\Explorer++ZH_CN\Explorer++ZH_CN.vcxproj' @translationProperties
    if ($LASTEXITCODE -ne 0) { throw 'Simplified Chinese translation build failed.' }
    $applicationDirectory = Join-Path $root 'Explorer++\Explorer++\x64\Release'
    $translationDll = Join-Path $root 'Explorer++\Win32\Release\Explorer++ZH_CN.dll'
    $applicationVersion = (Get-Item -LiteralPath (Join-Path $applicationDirectory 'Explorer++.exe')).VersionInfo.ProductVersion
    if ((Get-Item -LiteralPath $translationDll).VersionInfo.ProductVersion -ne $applicationVersion) {
        throw 'The translation DLL version must match the application version.'
    }
    Copy-Item -LiteralPath $translationDll -Destination $applicationDirectory -Force
    & $MSBuildPath 'Explorer++\TestExplorer++\TestExplorer++.vcxproj' @properties
    if ($LASTEXITCODE -ne 0) { throw 'Test build failed.' }
    & '.\Explorer++\TestExplorer++\x64\Release\TestExplorer++.exe' '--gtest_filter=AddressBar*:*MainToolbar*:*MainRebar*:ProcessManager*:*SessionRestorer*'
    if ($LASTEXITCODE -ne 0) { throw 'Regression tests failed.' }
} finally {
    Pop-Location
}
