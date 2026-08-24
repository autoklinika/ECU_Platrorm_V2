[CmdletBinding()]
param(
    [ValidateRange(0,100)]
    [int]$TargetPercent,

    [ValidateRange(1,60)]
    [int]$DurationSec = 5,

    [ValidateSet(250000,500000)]
    [int]$Bitrate,

    [ValidateRange(1,30)]
    [int]$DetectSeconds = 4,

    [string]$DllPath,

    [switch]$Yes,

    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ProtocolCAN = [uint32]5
$Can29BitId = [uint32]0x00000100
$StatusNoError = 0
$ErrTimeout = 0x09
$ErrBufferEmpty = 0x10
$ClearTxBuffer = [uint32]0x07
$ClearRxBuffer = [uint32]0x08
$ClearPeriodicMsgs = [uint32]0x09

$CommandId = [uint32]0x18FF2700
$TargetId  = [uint32]0x1AFFB400
$ActualId  = [uint32]0x1AFFB580
$AuxId     = [uint32]0x1AFFB680
$TxPeriodMs = [uint32]20
$PresenceTimeoutMs = 1500
$FramesRequiredForOnline = 3

function Convert-PercentToRaw {
    param([int]$Percent)
    $clamped = [Math]::Max(0, [Math]::Min(100, $Percent))
    return [uint16][Math]::Min(999, $clamped * 10)
}

function New-EgrPayload {
    param(
        [ValidateSet('Command','Target')]
        [string]$Kind,
        [uint16]$Raw
    )
    $rawClamped = [uint16][Math]::Min(999, [int]$Raw)
    $byte1 = if ($Kind -eq 'Command') { [byte]0x02 } else { [byte]0x01 }
    return [byte[]]@(
        0x01,
        $byte1,
        [byte]($rawClamped -band 0xFF),
        [byte](($rawClamped -shr 8) -band 0xFF),
        0x7D,
        0x00,
        0x00,
        0xFF
    )
}

function Assert-ByteArrayEqual {
    param([byte[]]$Actual, [byte[]]$Expected, [string]$Name)
    if ($Actual.Length -ne $Expected.Length) {
        throw "SELFTEST $Name: length $($Actual.Length), expected $($Expected.Length)"
    }
    for ($i = 0; $i -lt $Actual.Length; $i++) {
        if ($Actual[$i] -ne $Expected[$i]) {
            throw ("SELFTEST {0}: byte {1} = 0x{2:X2}, expected 0x{3:X2}" -f $Name,$i,$Actual[$i],$Expected[$i])
        }
    }
}

function Invoke-ProtocolSelfTest {
    if ((Convert-PercentToRaw 0) -ne 0) { throw 'SELFTEST raw 0 failed' }
    if ((Convert-PercentToRaw 50) -ne 500) { throw 'SELFTEST raw 50 failed' }
    if ((Convert-PercentToRaw 100) -ne 999) { throw 'SELFTEST raw 100 clamp failed' }

    Assert-ByteArrayEqual (New-EgrPayload Command 0) ([byte[]](0x01,0x02,0x00,0x00,0x7D,0x00,0x00,0xFF)) 'command-0'
    Assert-ByteArrayEqual (New-EgrPayload Target 500) ([byte[]](0x01,0x01,0xF4,0x01,0x7D,0x00,0x00,0xFF)) 'target-50'
    Assert-ByteArrayEqual (New-EgrPayload Command 999) ([byte[]](0x01,0x02,0xE7,0x03,0x7D,0x00,0x00,0xFF)) 'command-100'

    Write-Host 'SELFTEST PASS: Sonceboz frame encoding is consistent with legacy protocol.' -ForegroundColor Green
}

if ($SelfTest) {
    Invoke-ProtocolSelfTest
    exit 0
}

function Find-ScanmatikJ2534Dll {
    $roots = @(
        'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\PassThruSupport.04.04',
        'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\PassThruSupport.04.04'
    )

    $candidates = @()
    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        foreach ($key in (Get-ChildItem -Path $root -ErrorAction SilentlyContinue)) {
            try {
                $p = Get-ItemProperty -Path $key.PSPath
                $name = [string]$p.Name
                $vendor = [string]$p.Vendor
                $library = [string]$p.FunctionLibrary
                if (($name -match '(?i)scanmatik|SM2') -or ($vendor -match '(?i)scanmatik') -or ($library -match '(?i)smj2534')) {
                    if ($library -and (Test-Path $library)) {
                        $candidates += [pscustomobject]@{ Name=$name; Vendor=$vendor; Dll=$library; RegistryPath=$key.PSPath }
                    }
                }
            } catch {
                continue
            }
        }
    }

    if ($candidates.Count -eq 0) {
        $fallbacks = @(@(
            "$env:ProgramFiles\Scanmatik\smj2534.dll",
            "${env:ProgramFiles(x86)}\Scanmatik\smj2534.dll"
        ) | Where-Object { $_ -and (Test-Path $_) })
        if ($fallbacks.Count -gt 0) { return [string]$fallbacks[0] }
        throw 'Nie znaleziono sterownika Scanmatik J2534. Zainstaluj aktualny pakiet Scanmatik/J2534-RP1210 i podłącz SM2 przez USB.'
    }

    $usb = $candidates | Where-Object { $_.Name -match '(?i)USB|SM2' } | Select-Object -First 1
    if ($null -ne $usb) { return [string]$usb.Dll }
    return [string]$candidates[0].Dll
}

if (-not $DllPath) {
    $DllPath = Find-ScanmatikJ2534Dll
}
$DllPath = (Resolve-Path $DllPath).Path
Write-Host "J2534 DLL: $DllPath"

if ([Environment]::Is64BitProcess -and $DllPath -match '(?i)Program Files \(x86\)') {
    $x86PowerShell = Join-Path $env:WINDIR 'SysWOW64\WindowsPowerShell\v1.0\powershell.exe'
    if (-not (Test-Path $x86PowerShell)) {
        throw "Sterownik wygląda na 32-bitowy, ale nie znaleziono 32-bit Windows PowerShell: $x86PowerShell"
    }

    Write-Host 'Wykryto 32-bitowy sterownik J2534. Restart testera w 32-bit Windows PowerShell...' -ForegroundColor Yellow
    $forward = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$PSCommandPath,'-DllPath',$DllPath,'-DurationSec',$DurationSec,'-DetectSeconds',$DetectSeconds)
    if ($PSBoundParameters.ContainsKey('TargetPercent')) { $forward += @('-TargetPercent',$TargetPercent) }
    if ($PSBoundParameters.ContainsKey('Bitrate')) { $forward += @('-Bitrate',$Bitrate) }
    if ($Yes) { $forward += '-Yes' }
    & $x86PowerShell @forward
    exit $LASTEXITCODE
}

$NativeSource = @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

namespace EcuPlatform.J2534
{
    [StructLayout(LayoutKind.Sequential)]
    public struct PassThruMsg
    {
        public UInt32 ProtocolID;
        public UInt32 RxStatus;
        public UInt32 TxFlags;
        public UInt32 Timestamp;
        public UInt32 DataSize;
        public UInt32 ExtraDataIndex;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4128)]
        public byte[] Data;
    }

    public static class Native
    {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibrary(string lpFileName);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool FreeLibrary(IntPtr hModule);

        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
        private static extern IntPtr GetProcAddress(IntPtr hModule, string lpProcName);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate Int32 OpenDelegate(IntPtr pName, out UInt32 deviceId);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate Int32 CloseDelegate(UInt32 deviceId);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate Int32 ConnectDelegate(UInt32 deviceId, UInt32 protocolId, UInt32 flags, UInt32 baudRate, out UInt32 channelId);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate Int32 DisconnectDelegate(UInt32 channelId);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate Int32 ReadMsgsDelegate(UInt32 channelId, IntPtr pMsg, ref UInt32 numMsgs, UInt32 timeout);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate Int32 StartPeriodicDelegate(UInt32 channelId, IntPtr pMsg, out UInt32 msgId, UInt32 intervalMs);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate Int32 StopPeriodicDelegate(UInt32 channelId, UInt32 msgId);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate Int32 IoctlDelegate(UInt32 channelId, UInt32 ioctlId, IntPtr pInput, IntPtr pOutput);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate Int32 GetLastErrorDelegate(IntPtr pErrorDescription);

        private static IntPtr module = IntPtr.Zero;
        private static OpenDelegate open;
        private static CloseDelegate close;
        private static ConnectDelegate connect;
        private static DisconnectDelegate disconnect;
        private static ReadMsgsDelegate readMsgs;
        private static StartPeriodicDelegate startPeriodic;
        private static StopPeriodicDelegate stopPeriodic;
        private static IoctlDelegate ioctl;
        private static GetLastErrorDelegate getLastError;

        private static T Bind<T>(string name) where T : class
        {
            IntPtr p = GetProcAddress(module, name);
            if (p == IntPtr.Zero) throw new MissingMethodException("J2534 export not found: " + name);
            return Marshal.GetDelegateForFunctionPointer(p, typeof(T)) as T;
        }

        public static void Load(string path)
        {
            if (module != IntPtr.Zero) return;
            module = LoadLibrary(path);
            if (module == IntPtr.Zero)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "LoadLibrary failed for " + path);

            open = Bind<OpenDelegate>("PassThruOpen");
            close = Bind<CloseDelegate>("PassThruClose");
            connect = Bind<ConnectDelegate>("PassThruConnect");
            disconnect = Bind<DisconnectDelegate>("PassThruDisconnect");
            readMsgs = Bind<ReadMsgsDelegate>("PassThruReadMsgs");
            startPeriodic = Bind<StartPeriodicDelegate>("PassThruStartPeriodicMsg");
            stopPeriodic = Bind<StopPeriodicDelegate>("PassThruStopPeriodicMsg");
            ioctl = Bind<IoctlDelegate>("PassThruIoctl");
            getLastError = Bind<GetLastErrorDelegate>("PassThruGetLastError");
        }

        public static void Unload()
        {
            if (module == IntPtr.Zero) return;
            FreeLibrary(module);
            module = IntPtr.Zero;
        }

        public static Int32 Open(out UInt32 deviceId) { return open(IntPtr.Zero, out deviceId); }
        public static Int32 Close(UInt32 deviceId) { return close(deviceId); }
        public static Int32 Connect(UInt32 deviceId, UInt32 protocolId, UInt32 flags, UInt32 baudRate, out UInt32 channelId)
        { return connect(deviceId, protocolId, flags, baudRate, out channelId); }
        public static Int32 Disconnect(UInt32 channelId) { return disconnect(channelId); }
        public static Int32 StopPeriodic(UInt32 channelId, UInt32 msgId) { return stopPeriodic(channelId, msgId); }
        public static Int32 Ioctl(UInt32 channelId, UInt32 ioctlId) { return ioctl(channelId, ioctlId, IntPtr.Zero, IntPtr.Zero); }

        private static IntPtr MarshalMsg(PassThruMsg msg)
        {
            if (msg.Data == null || msg.Data.Length != 4128)
                throw new ArgumentException("PassThruMsg.Data must contain exactly 4128 bytes");
            IntPtr p = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(PassThruMsg)));
            Marshal.StructureToPtr(msg, p, false);
            return p;
        }

        public static Int32 StartPeriodic(UInt32 channelId, PassThruMsg msg, out UInt32 msgId, UInt32 intervalMs)
        {
            IntPtr p = MarshalMsg(msg);
            try { return startPeriodic(channelId, p, out msgId, intervalMs); }
            finally { Marshal.DestroyStructure(p, typeof(PassThruMsg)); Marshal.FreeHGlobal(p); }
        }

        public static Int32 ReadOne(UInt32 channelId, UInt32 timeout, out PassThruMsg msg, out UInt32 count)
        {
            msg = new PassThruMsg { Data = new byte[4128] };
            IntPtr p = MarshalMsg(msg);
            count = 1;
            try
            {
                Int32 rc = readMsgs(channelId, p, ref count, timeout);
                if (count > 0) msg = (PassThruMsg)Marshal.PtrToStructure(p, typeof(PassThruMsg));
                return rc;
            }
            finally
            {
                Marshal.DestroyStructure(p, typeof(PassThruMsg));
                Marshal.FreeHGlobal(p);
            }
        }

        public static string LastErrorText()
        {
            if (getLastError == null) return String.Empty;
            IntPtr p = Marshal.AllocHGlobal(256);
            try
            {
                for (int i = 0; i < 256; i++) Marshal.WriteByte(p, i, 0);
                getLastError(p);
                return Marshal.PtrToStringAnsi(p) ?? String.Empty;
            }
            finally { Marshal.FreeHGlobal(p); }
        }
    }
}
'@

Add-Type -TypeDefinition $NativeSource -Language CSharp
[EcuPlatform.J2534.Native]::Load($DllPath)

function Get-J2534ErrorText {
    param([int]$Code)
    $text = [EcuPlatform.J2534.Native]::LastErrorText()
    if ([string]::IsNullOrWhiteSpace($text)) { return ('0x{0:X8}' -f ([uint32]$Code)) }
    return ('0x{0:X8}: {1}' -f ([uint32]$Code), $text)
}

function Assert-J2534Ok {
    param([int]$Code, [string]$Operation)
    if ($Code -ne $StatusNoError) {
        throw "$Operation failed: $(Get-J2534ErrorText $Code)"
    }
}

function New-J2534CanMessage {
    param([uint32]$CanId, [byte[]]$Payload)
    if ($Payload.Length -gt 8) { throw 'Classic CAN payload cannot exceed 8 bytes.' }
    $m = New-Object EcuPlatform.J2534.PassThruMsg
    $m.ProtocolID = $ProtocolCAN
    $m.RxStatus = 0
    $m.TxFlags = $Can29BitId
    $m.Timestamp = 0
    $m.DataSize = [uint32](4 + $Payload.Length)
    $m.ExtraDataIndex = 0
    $m.Data = New-Object byte[] 4128
    $m.Data[0] = [byte](($CanId -shr 24) -band 0xFF)
    $m.Data[1] = [byte](($CanId -shr 16) -band 0xFF)
    $m.Data[2] = [byte](($CanId -shr 8) -band 0xFF)
    $m.Data[3] = [byte]($CanId -band 0xFF)
    [Array]::Copy($Payload, 0, $m.Data, 4, $Payload.Length)
    return $m
}

function Convert-J2534Message {
    param([EcuPlatform.J2534.PassThruMsg]$Msg)
    if ($Msg.DataSize -lt 4 -or $null -eq $Msg.Data) { return $null }
    $id = ([uint32]$Msg.Data[0] -shl 24) -bor ([uint32]$Msg.Data[1] -shl 16) -bor ([uint32]$Msg.Data[2] -shl 8) -bor [uint32]$Msg.Data[3]
    $payloadLen = [Math]::Min(8, [int]$Msg.DataSize - 4)
    $payload = New-Object byte[] $payloadLen
    if ($payloadLen -gt 0) { [Array]::Copy($Msg.Data, 4, $payload, 0, $payloadLen) }
    return [pscustomobject]@{
        Id = $id
        Payload = $payload
        Timestamp = [uint32]$Msg.Timestamp
        RxStatus = [uint32]$Msg.RxStatus
    }
}

function Format-Payload {
    param([byte[]]$Payload)
    if ($null -eq $Payload) { return '' }
    return (($Payload | ForEach-Object { '{0:X2}' -f $_ }) -join ' ')
}

$logDir = Join-Path $PSScriptRoot 'logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$logPath = Join-Path $logDir ('sm2_egr_{0}.csv' -f (Get-Date -Format 'yyyyMMdd_HHmmss'))
'host_time,j2534_timestamp_us,direction,can_id,rx_status,payload,actual_raw,actual_percent' | Set-Content -Path $logPath -Encoding UTF8

function Write-FrameLog {
    param(
        [string]$Direction,
        [uint32]$Id,
        [uint32]$Timestamp,
        [uint32]$RxStatus,
        [byte[]]$Payload,
        [object]$ActualRaw,
        [object]$ActualPercent
    )
    $host = (Get-Date).ToString('o')
    $rawText = if ($null -ne $ActualRaw) { ([int]$ActualRaw).ToString([Globalization.CultureInfo]::InvariantCulture) } else { '' }
    $pctText = if ($null -ne $ActualPercent) { ([double]$ActualPercent).ToString('0.0',[Globalization.CultureInfo]::InvariantCulture) } else { '' }
    $line = '"{0}",{1},{2},0x{3:X8},0x{4:X8},"{5}",{6},{7}' -f $host,$Timestamp,$Direction,$Id,$RxStatus,(Format-Payload $Payload),$rawText,$pctText
    Add-Content -Path $logPath -Value $line -Encoding UTF8
}

$deviceId = [uint32]0
$channelId = [uint32]0
$deviceOpen = $false
$channelOpen = $false
$commandPeriodicId = [uint32]0
$targetPeriodicId = [uint32]0
$commandPeriodicActive = $false
$targetPeriodicActive = $false
$selectedBitrate = 0
$lastStatusAt = $null
$statusFrames = 0
$lastActualRaw = $null
$lastActualPercent = $null
$txEchoLastTimestamp = @{}
$txEchoMaxGapMs = 0.0
$txEchoCount = 0

function Stop-PeriodicSafely {
    if ($commandPeriodicActive -and $channelOpen) {
        try { [void][EcuPlatform.J2534.Native]::StopPeriodic($channelId, $commandPeriodicId) } catch {}
        $script:commandPeriodicActive = $false
    }
    if ($targetPeriodicActive -and $channelOpen) {
        try { [void][EcuPlatform.J2534.Native]::StopPeriodic($channelId, $targetPeriodicId) } catch {}
        $script:targetPeriodicActive = $false
    }
    if ($channelOpen) {
        try { [void][EcuPlatform.J2534.Native]::Ioctl($channelId, $ClearPeriodicMsgs) } catch {}
    }
}

function Read-OneFrame {
    param([int]$TimeoutMs = 50)
    $m = New-Object EcuPlatform.J2534.PassThruMsg
    $count = [uint32]0
    $rc = [EcuPlatform.J2534.Native]::ReadOne($channelId, [uint32]$TimeoutMs, [ref]$m, [ref]$count)
    if (($rc -eq $ErrTimeout) -or ($rc -eq $ErrBufferEmpty) -or ($count -eq 0)) { return $null }
    Assert-J2534Ok $rc 'PassThruReadMsgs'
    return (Convert-J2534Message $m)
}

function Handle-Frame {
    param($Frame, [switch]$Quiet)
    if ($null -eq $Frame) { return }

    $actualRawLocal = $null
    $actualPctLocal = $null
    if ($Frame.Id -eq $ActualId -and $Frame.Payload.Length -ge 6) {
        $actualRawLocal = [int]$Frame.Payload[4] -bor ([int]$Frame.Payload[5] -shl 8)
        $actualPctLocal = [Math]::Min(1000, $actualRawLocal) / 10.0
        $script:lastActualRaw = $actualRawLocal
        $script:lastActualPercent = $actualPctLocal
    }

    if (($Frame.Id -eq $ActualId) -or ($Frame.Id -eq $AuxId)) {
        $script:statusFrames++
        $script:lastStatusAt = [DateTime]::UtcNow
        Write-FrameLog 'RX' $Frame.Id $Frame.Timestamp $Frame.RxStatus $Frame.Payload $actualRawLocal $actualPctLocal
        if (-not $Quiet) {
            if ($null -ne $actualPctLocal) {
                Write-Host ('RX 0x{0:X8}  actual={1:0.0}%  [{2}]' -f $Frame.Id,$actualPctLocal,(Format-Payload $Frame.Payload))
            } else {
                Write-Host ('RX 0x{0:X8}  [{1}]' -f $Frame.Id,(Format-Payload $Frame.Payload))
            }
        }
        return
    }

    if (($Frame.Id -eq $CommandId) -or ($Frame.Id -eq $TargetId)) {
        $script:txEchoCount++
        $key = ('{0:X8}' -f $Frame.Id)
        if ($txEchoLastTimestamp.ContainsKey($key)) {
            $deltaUs = [uint32]($Frame.Timestamp - $txEchoLastTimestamp[$key])
            $gapMs = $deltaUs / 1000.0
            if ($gapMs -gt $script:txEchoMaxGapMs) { $script:txEchoMaxGapMs = $gapMs }
        }
        $script:txEchoLastTimestamp[$key] = $Frame.Timestamp
        Write-FrameLog 'TX_ECHO' $Frame.Id $Frame.Timestamp $Frame.RxStatus $Frame.Payload $null $null
    }
}

function Connect-CanAtBitrate {
    param([int]$Speed)
    if ($channelOpen) {
        try { [void][EcuPlatform.J2534.Native]::Disconnect($channelId) } catch {}
        $script:channelOpen = $false
    }
    $newChannel = [uint32]0
    $rc = [EcuPlatform.J2534.Native]::Connect($deviceId, $ProtocolCAN, $Can29BitId, [uint32]$Speed, [ref]$newChannel)
    Assert-J2534Ok $rc "PassThruConnect CAN $Speed"
    $script:channelId = $newChannel
    $script:channelOpen = $true
    try { [void][EcuPlatform.J2534.Native]::Ioctl($channelId, $ClearRxBuffer) } catch {}
    try { [void][EcuPlatform.J2534.Native]::Ioctl($channelId, $ClearTxBuffer) } catch {}
}

function Detect-Egr {
    param([int[]]$Speeds)
    foreach ($speed in $Speeds) {
        Write-Host "`nNasłuch SONCEBOZ EGR: $($speed/1000) kb/s..." -ForegroundColor Cyan
        Connect-CanAtBitrate $speed
        $script:statusFrames = 0
        $script:lastStatusAt = $null
        $deadline = [DateTime]::UtcNow.AddSeconds($DetectSeconds)
        while ([DateTime]::UtcNow -lt $deadline) {
            $f = Read-OneFrame 100
            if ($null -ne $f) { Handle-Frame $f -Quiet }
            if ($statusFrames -ge $FramesRequiredForOnline) {
                Write-Host "EGR ONLINE przy $($speed/1000) kb/s ($statusFrames ramek statusowych)." -ForegroundColor Green
                return $speed
            }
        }
        Write-Host "Brak wymaganych ramek statusowych przy $($speed/1000) kb/s." -ForegroundColor DarkYellow
        if ($channelOpen) {
            [void][EcuPlatform.J2534.Native]::Disconnect($channelId)
            $script:channelOpen = $false
        }
    }
    return 0
}

try {
    Invoke-ProtocolSelfTest

    $rc = [EcuPlatform.J2534.Native]::Open([ref]$deviceId)
    Assert-J2534Ok $rc 'PassThruOpen'
    $deviceOpen = $true
    Write-Host ('Scanmatik J2534 otwarty. DeviceID=0x{0:X8}' -f $deviceId) -ForegroundColor Green

    $speeds = if ($PSBoundParameters.ContainsKey('Bitrate')) { @($Bitrate) } else { @(250000,500000) }
    $selectedBitrate = Detect-Egr $speeds
    if ($selectedBitrate -eq 0) {
        throw 'Nie wykryto ramek SONCEBOZ EGR 0x1AFFB580/0x1AFFB680 przy sprawdzonych bitrate.'
    }

    Write-Host "`n=== STATUS ==="
    Write-Host "Bitrate: $($selectedBitrate/1000) kb/s"
    if ($null -ne $lastActualPercent) {
        Write-Host ('Aktualna pozycja: {0:0.0}% (RAW {1})' -f $lastActualPercent,$lastActualRaw)
    } else {
        Write-Host 'Pozycja aktualna: jeszcze brak pełnej ramki 0x1AFFB580.'
    }
    Write-Host "Log: $logPath"

    if (-not $PSBoundParameters.ContainsKey('TargetPercent')) {
        Write-Host "`nTryb LISTEN-ONLY zakończony. Aby wykonać krótki test ruchu, uruchom ponownie z -TargetPercent <0..100>." -ForegroundColor Cyan
        return
    }

    $raw = Convert-PercentToRaw $TargetPercent
    $commandPayload = New-EgrPayload Command $raw
    $targetPayload = New-EgrPayload Target $raw

    Write-Host "`n=== TEST STEROWANIA ===" -ForegroundColor Yellow
    if ($null -ne $lastActualPercent) {
        $delta = [Math]::Abs([double]$TargetPercent - [double]$lastActualPercent)
        Write-Host ('Aktualna: {0:0.0}%  -> żądana: {1}%  (różnica {2:0.0} pp)' -f $lastActualPercent,$TargetPercent,$delta)
        if ($delta -gt 20) {
            Write-Warning 'Duża zmiana pozycji (>20 pp). Do pierwszego testu zalecane jest ustawienie celu blisko aktualnej pozycji.'
        }
    } else {
        Write-Host "Żądana pozycja: $TargetPercent% (RAW $raw)"
    }
    Write-Host ('Command 0x{0:X8}: {1}' -f $CommandId,(Format-Payload $commandPayload))
    Write-Host ('Target  0x{0:X8}: {1}' -f $TargetId,(Format-Payload $targetPayload))
    Write-Host "Obie ramki będą zlecane Scanmatikowi jako periodic TX co $TxPeriodMs ms przez maksymalnie $DurationSec s."
    Write-Host 'STOP nastąpi po czasie, timeout statusu 1500 ms, błędzie lub przerwaniu skryptu.'

    if (-not $Yes) {
        $answer = Read-Host 'Wpisz dokładnie START aby uruchomić ruch EGR'
        if ($answer -cne 'START') {
            Write-Host 'Anulowano. Nie uruchomiono żadnej transmisji sterującej.' -ForegroundColor Cyan
            return
        }
    }

    $commandMsg = New-J2534CanMessage $CommandId $commandPayload
    $targetMsg = New-J2534CanMessage $TargetId $targetPayload

    $rc = [EcuPlatform.J2534.Native]::StartPeriodic($channelId, $commandMsg, [ref]$commandPeriodicId, $TxPeriodMs)
    Assert-J2534Ok $rc 'PassThruStartPeriodicMsg COMMAND'
    $commandPeriodicActive = $true

    try {
        $rc = [EcuPlatform.J2534.Native]::StartPeriodic($channelId, $targetMsg, [ref]$targetPeriodicId, $TxPeriodMs)
        Assert-J2534Ok $rc 'PassThruStartPeriodicMsg TARGET'
        $targetPeriodicActive = $true
    } catch {
        Stop-PeriodicSafely
        throw
    }

    Write-FrameLog 'TX_PERIODIC_START' $CommandId 0 0 $commandPayload $null $null
    Write-FrameLog 'TX_PERIODIC_START' $TargetId 0 0 $targetPayload $null $null
    Write-Host 'PERIODIC TX STARTED' -ForegroundColor Green

    if ($null -eq $lastStatusAt) { $lastStatusAt = [DateTime]::UtcNow }
    $endAt = [DateTime]::UtcNow.AddSeconds($DurationSec)
    $lastPrintedActual = [double]::NaN

    while ([DateTime]::UtcNow -lt $endAt) {
        $f = Read-OneFrame 50
        if ($null -ne $f) { Handle-Frame $f -Quiet }

        if ($null -ne $lastStatusAt) {
            $ageMs = ([DateTime]::UtcNow - $lastStatusAt).TotalMilliseconds
            if ($ageMs -gt $PresenceTimeoutMs) {
                Stop-PeriodicSafely
                throw ('EGR status timeout: {0:0} ms. Periodic TX zatrzymany.' -f $ageMs)
            }
        }

        if (($null -ne $lastActualPercent) -and ([double]::IsNaN($lastPrintedActual) -or [Math]::Abs($lastActualPercent - $lastPrintedActual) -ge 0.5)) {
            Write-Host ('Actual: {0:0.0}%  target: {1}%' -f $lastActualPercent,$TargetPercent)
            $lastPrintedActual = [double]$lastActualPercent
        }
    }

    Stop-PeriodicSafely
    Write-FrameLog 'TX_PERIODIC_STOP' $CommandId 0 0 ([byte[]]@()) $null $null
    Write-FrameLog 'TX_PERIODIC_STOP' $TargetId 0 0 ([byte[]]@()) $null $null
    Write-Host "`nPERIODIC TX STOPPED" -ForegroundColor Green

    if ($null -ne $lastActualPercent) {
        Write-Host ('Pozycja końcowa: {0:0.0}% (RAW {1})' -f $lastActualPercent,$lastActualRaw)
    }
    if ($txEchoCount -gt 1) {
        Write-Host ('J2534 zwrócił {0} echo TX; maksymalny zaobserwowany odstęp dla jednego ID: {1:0.000} ms.' -f $txEchoCount,$txEchoMaxGapMs)
    } else {
        Write-Host 'Sterownik nie zwrócił wystarczających echo TX do pomiaru jittera. Dokładny jitter zmierzymy drugim analizatorem CAN.' -ForegroundColor DarkYellow
    }

    Write-Host "TEST ZAKOŃCZONY. Log: $logPath" -ForegroundColor Green
}
finally {
    Stop-PeriodicSafely
    if ($channelOpen) {
        try { [void][EcuPlatform.J2534.Native]::Disconnect($channelId) } catch {}
        $channelOpen = $false
    }
    if ($deviceOpen) {
        try { [void][EcuPlatform.J2534.Native]::Close($deviceId) } catch {}
        $deviceOpen = $false
    }
    try { [EcuPlatform.J2534.Native]::Unload() } catch {}
}
