param(
    [string]$BuildDirectory = 'D:/wocc/build/win-forest-o2/arp-engine',
    [string]$Output = 'D:/wocc/output/forest-reconstruction/hilly-forest-20261020/runtime-v19/slit-validation',
    [string]$Prefix = 'recheck'
)
$ErrorActionPreference = 'Stop'
$engineRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$exe = (Resolve-Path (Join-Path $BuildDirectory 'arp_volumetric_fog.exe')).Path
New-Item -ItemType Directory -Force $Output | Out-Null
$cases = @(
    @{name='toward'; camera='toward'; pixels=12; slices=112; extra=@('--no-temporal')},
    @{name='side'; camera='side'; pixels=12; slices=112; extra=@('--no-temporal')},
    @{name='away'; camera='away'; pixels=12; slices=112; extra=@('--no-temporal')},
    @{name='fine'; camera='toward'; pixels=4; slices=128; extra=@('--no-temporal')},
    @{name='point'; camera='side'; pixels=12; slices=112; extra=@('--no-temporal','--point-shadow')},
    @{name='motion'; camera='side'; pixels=12; slices=112; extra=@('--motion','0.12')}
)
python (Join-Path $PSScriptRoot 'validate_fog_shafts.py') --self-test
if ($LASTEXITCODE -ne 0) { throw 'Reference self-test failed' }
foreach ($case in $cases) {
    $base = Join-Path $Output "$Prefix-$($case.name)"
    if (Test-Path "${base}_capture.rdc") { throw "Preserve existing evidence: $base" }
    $arguments = @('--backend','vulkan','--slits','--pixels',"$($case.pixels)",
        '--slices',"$($case.slices)",'--camera',$case.camera,'--frames','75',
        '--capture-frame','70','--capture',$base,'--report',"$base.yaml") + $case.extra
    & 'C:/Program Files/RenderDoc/renderdoccmd.exe' capture -w -d $engineRoot $exe @arguments
    if ($LASTEXITCODE -ne 0 -or !(Test-Path "${base}_capture.rdc")) { throw "Capture failed: $base" }
    $previousCapture = $env:ARP_FOG_AUDIT_CAPTURE
    $previousOutput = $env:ARP_FOG_AUDIT_OUTPUT
    try {
        $env:ARP_FOG_AUDIT_CAPTURE = "${base}_capture.rdc"
        $env:ARP_FOG_AUDIT_OUTPUT = $base
        $exporter = Join-Path $PSScriptRoot 'export_fog_shaft_renderdoc.py'
        $process = Start-Process 'C:/Program Files/RenderDoc/qrenderdoc.exe' -ArgumentList @('--python',"`"$exporter`"") -WindowStyle Hidden -PassThru
        if (!$process.WaitForExit(30000)) { throw "Exporter pending for $base" }
        if (!(Test-Path "$base/report.json") -or (Test-Path "$base/error.txt")) { throw "Export failed: $base" }
    } finally {
        $env:ARP_FOG_AUDIT_CAPTURE = $previousCapture
        $env:ARP_FOG_AUDIT_OUTPUT = $previousOutput
    }
    python (Join-Path $PSScriptRoot 'validate_fog_shafts.py') $base
    if ($LASTEXITCODE -ne 0) { throw "Reference validation failed: $base" }
    python (Join-Path $PSScriptRoot 'inspect_fog_phase_history.py') $base
    if ($LASTEXITCODE -ne 0) { throw "History validation failed: $base" }
}
