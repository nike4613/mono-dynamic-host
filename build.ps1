param (
    [switch][bool] $Debug = $false,
    [switch][bool] $NoOpt = $false,
    [string] $Target = $null,
    [string] $LipoCommand = "lipo"
)

$ErrorActionPreference = "Stop";

function Ensure-DirExists([string]$dir)
{
    if (-not (Test-Path -Type Container $dir))
    {
        mkdir $dir | Out-Null;
    }
}

$zig = Get-Command zig;
$lipo = Get-Command -EA Ignore $LipoCommand;

function Exec
{
    [CmdletBinding()]
    param (
        $cmd,
        [Parameter(ValueFromRemainingArguments)]
        [string[]]$args
    )

    process 
    {
        echo "$cmd $($args | Join-String -Separator ' ')";
        &$cmd @args;
        if (-not $?) { 
            $PSCmdlet.WriteError(
                [System.Management.Automation.ErrorRecord]::new(
                    [exception]::new(), # the underlying exception
                    'dummy',            # the error ID
                    'NotSpecified',     # the error category
                    $null)              # the object the error relates to
            );
        }
    }
}

echo "Using zig $(&$zig version)";
if ($lipo -ne $null)
{
    echo "Using lipo $(&$lipo -version)";
}

$builddir = Join-Path $PSScriptRoot "build";
Ensure-DirExists $builddir;

$source = Join-Path $PSScriptRoot "mono_dynamic_host.c" | Resolve-Path;

$targets = @(
    # Windows
    "x86_64-windows",
    "x86-windows",
    "aarch64-windows",

    # Linux glibc
    "x86-linux-gnu.2.10",
    "x86_64-linux-gnu.2.10",
    "aarch64-linux-gnu.2.10",

    # Linux musl
    "x86-linux-musl",
    "x86_64-linux-musl",
    "aarch64-linux-musl",

    # MacOS
    "x86_64-macos",
    "aarch64-macos"
);

$cflags = @("-g","-Wall","-Wpedantic");
$cflagsNonWin = @("-lc","-ldl","-Wl,--build-id");

$anyFailed = $false;
foreach ($target in $targets)
{
    if ($Target -ne $null -and $Target -ne $target) { continue; }

    echo "--------- Compiling for $target ---------";

    $iswin = $target.EndsWith("-windows");
    $ismac = $target.EndsWith("-macos");
    $ext = if ($iswin) { ".exe" } else { "" }

    $outdir = Join-Path $builddir $target;
    Ensure-DirExists $outdir;

    $outfilebase = Join-Path $outdir "mdh";
    $outexe = "$outfilebase$ext";

    # compile with zig cc
    $flags = $cflags;
    if (-not $iswin) { $flags += $cflagsNonWin; };
    Exec -EA Ignore $zig cc @flags "-target" $target $source "-o" $outexe;
    if (-not $?) { $anyFailed = $true; continue; }

    if (-not $iswin -and -not $ismac)
    {
        Move-Item $outexe "$outexe.tmp";
        # if we're not targeting windows, separate out the debug information
        Exec -EA Ignore $zig objcopy "--only-keep-debug" "--compress-debug-sections" "$outexe.tmp" "$outfilebase.dbg";
        if (-not $?) { $anyFailed = $true; continue; }
        # and add debuglink to the original
        Exec -EA Ignore $zig objcopy "--add-gnu-debuglink=$outfilebase.dbg" "--strip-all" "$outexe.tmp" $outexe
        if (-not $?) { $anyFailed = $true; continue; }

        Remove-Item "$outexe.tmp";
    }
}

if ($lipo -ne $null -and $Target -eq $null -and -not $anyFailed)
{
    # lipo together all the macos targets
    $outdir = Join-Path $builddir "any-macos";
    Ensure-DirExists $outdir;
    $outexe = Join-Path $outdir "mdh";

    $infiles = $targets | Where { $_.EndsWith("-macos") } | % { Join-Path $builddir $_ "mdh" | Resolve-Path };
    Exec -EA Ignore $lipo "-output" $outexe "-create" @infiles;
    if (-not $?) { $anyFailed = $true; }
}

if ($anyFailed)
{
    Write-Error "Some builds failed."
}