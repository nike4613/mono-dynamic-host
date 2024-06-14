param (
    [switch][bool] $Debug = $false,
    [switch][bool] $NoOpt = $false,
    [string[]] $Targets = $null,
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
if ($lipo -eq $null)
{
    # try to probe for llvm-lipo
    $lipo = Get-Command -EA Ignore "llvm-lipo*" | Select -First 1;
}

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

$allTargets = @(
    # Windows
    "x86_64-windows",
    "x86-windows",
    "aarch64-windows",

    # Linux glibc
    "x86-linux-gnu.2.10",
    "x86_64-linux-gnu.2.10",

    # Linux musl
    "x86-linux-musl",
    "x86_64-linux-musl",
    "aarch64-linux-musl",

    # MacOS
    "x86_64-macos",
    "aarch64-macos"
);

$macTargets = $allTargets | Where { $_.EndsWith("-macos") };

if ($Targets -contains "any-macos") { $Targets += $macTargets; }

$cflags = @("-g","-Wall","-Wpedantic");
$cflagsNonWin = @("-lc","-ldl","-Wl,--build-id");

if (-not $Debug -and -not $NoOpt)
{
    # always optimize for size
    $cflags += @("-Oz");
}
elseif (-not $NoOpt)
{
    # if -debug is specified, use optimization targeted at debugability
    $cflags += @("-Og");
}
else
{
    # if -NoOpt is specified, disable all optimization
    $cflags += @("-O0");
}

$built = @();
$failed = @();
foreach ($target in $allTargets)
{
    if ($Targets -ne $null -and -not $Targets -contains $target) { continue; }

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
    if (-not $?) { $failed += @($target); continue; }

    if (-not $iswin -and -not $ismac)
    {
        Move-Item $outexe "$outexe.tmp";
        # if we're not targeting windows, separate out the debug information
        Exec -EA Ignore $zig objcopy "--only-keep-debug" "--compress-debug-sections" "$outexe.tmp" "$outfilebase.dbg";
        if (-not $?) { $failed += @($target); continue; }
        # and add debuglink to the original
        Exec -EA Ignore $zig objcopy "--add-gnu-debuglink=$outfilebase.dbg" "--strip-all" "$outexe.tmp" $outexe
        if (-not $?) { $failed += @($target); continue; }

        Remove-Item "$outexe.tmp";
    }

    $built += @($target);
}


if ($failed.Length -eq 0)
{
    $target = "any-macos";
    if ($Targets -eq $null -or $Target -contains $target)
    {
        # we want to build a MacOS universal binary
        if ($lipo -ne $null)
        {
            echo "--------- Packing for $target ---------";
            # lipo together all the macos targets
            $outdir = Join-Path $builddir $target;
            Ensure-DirExists $outdir;
            $outexe = Join-Path $outdir "mdh";

            $infiles = $macTargets | % { Join-Path $builddir $_ "mdh" | Resolve-Path };
            Exec -EA Ignore $lipo "-output" $outexe "-create" @infiles;
            if (-not $?) { $failed += @($target); }
            $built += @($target);
        }
        elseif ($Targets -ne $null -and $Targets -contains $target)
        {
            Write-Host "------------------------------------------------";
            Write-Host "any-macos was requested, but no lipo command was found";
            $failed += @($target);
        }
        else
        {
            Write-Host "------------------------------------------------";
            Write-Host "No lipo command was found, not building $target";
        }

        Write-Host "------------------------------------------------";
    }
}

if ($failed.Length -gt 0)
{
    Write-Host "Some builds failed:"
    Write-Host $failed;

    exit 1;
}

Write-Host "Creating archives...";

# create archives of all created binaries
foreach ($target in $built)
{
    $outdir = Join-Path $builddir $target | Resolve-Path;
    $zip = Join-Path $builddir "$target.zip";
    Compress-Archive -LiteralPath (Get-ChildItem $outdir) -DestinationPath $zip -CompressionLevel Optimal -Force;
}

Write-Host "Done!";