# =============================================================================
# SmoothZoom dev tools: bench environment check. Dot-source:
#     . "$PSScriptRoot\_environment.ps1"
#     $env = Test-SzBenchEnvironment      # -> @{ Ok; Reason; Locked; Display; RefreshPeriodMs }
#
# A bench on a locked session, with the display off, or while DWM reports no
# refresh timing measures the environment, not the code. gate.ps1 and
# run-bench.ps1 call this before every run and fail with an ENVIRONMENT error
# (gate exit 3) instead of reporting metric failures.
# =============================================================================

if (-not ("SzBenchEnv" -as [type])) {
Add-Type -ReferencedAssemblies System.Windows.Forms @"
using System;
using System.Runtime.InteropServices;
using System.Windows.Forms;

public static class SzBenchEnv
{
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct DWM_TIMING_INFO
    {
        public uint cbSize;
        public uint rateRefreshNum, rateRefreshDen;
        public ulong qpcRefreshPeriod;
        public uint rateComposeNum, rateComposeDen;
        public ulong qpcVBlank, cRefresh; public uint cDXRefresh;
        public ulong qpcCompose, cFrame; public uint cDXPresent;
        public ulong cRefreshFrame, cFrameSubmitted; public uint cDXPresentSubmitted;
        public ulong cFrameConfirmed; public uint cDXPresentConfirmed;
        public ulong cRefreshConfirmed; public uint cDXRefreshConfirmed;
        public ulong cFramesLate; public uint cFramesOutstanding;
        public ulong cFrameDisplayed, qpcFrameDisplayed, cRefreshFrameDisplayed;
        public ulong cFrameComplete, qpcFrameComplete;
        public ulong cFramePending, qpcFramePending;
        public ulong cFramesDisplayed, cFramesComplete, cFramesPending, cFramesAvailable;
        public ulong cFramesDropped, cFramesMissed, cRefreshNextDisplayed, cRefreshNextPresented;
        public ulong cRefreshesDisplayed, cRefreshesPresented, cRefreshStarted;
        public ulong cPixelsReceived, cPixelsDrawn, cBuffersEmpty;
    }

    [DllImport("dwmapi.dll")] static extern int DwmGetCompositionTimingInfo(IntPtr hwnd, ref DWM_TIMING_INFO info);
    [DllImport("kernel32.dll")] static extern bool QueryPerformanceFrequency(out long f);
    [DllImport("user32.dll", SetLastError = true)] static extern IntPtr OpenInputDesktop(int flags, bool inherit, uint access);
    [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern bool GetUserObjectInformationW(IntPtr h, int index, System.Text.StringBuilder info, int len, out int needed);
    [DllImport("user32.dll", SetLastError = true)]
    static extern IntPtr RegisterPowerSettingNotification(IntPtr hRecipient, ref Guid powerSettingGuid, int flags);
    [DllImport("user32.dll")] static extern bool UnregisterPowerSettingNotification(IntPtr handle);

    // Refresh period in ms from DWM, or 0 when DWM reports no timing.
    public static double RefreshPeriodMs()
    {
        var ti = new DWM_TIMING_INFO();
        ti.cbSize = (uint)Marshal.SizeOf(typeof(DWM_TIMING_INFO));
        if (DwmGetCompositionTimingInfo(IntPtr.Zero, ref ti) != 0) return 0;
        long f; QueryPerformanceFrequency(out f);
        if (ti.qpcRefreshPeriod == 0 || f == 0) return 0;
        return ti.qpcRefreshPeriod * 1000.0 / f;
    }

    // 1 = locked (or a secure desktop such as UAC is up), 0 = unlocked. The input
    // desktop is "Default" only while the user's desktop takes input; on the lock
    // screen it is Winlogon, which a user process cannot open. (WTSSessionInfoEx
    // SessionFlags was not used: it reads inverted on build 26200.)
    public static int SessionLocked()
    {
        IntPtr h = OpenInputDesktop(0, false, 0x0100 /* DESKTOP_SWITCHDESKTOP */);
        if (h == IntPtr.Zero) return 1;
        try
        {
            var sb = new System.Text.StringBuilder(256); int needed;
            if (!GetUserObjectInformationW(h, 2 /* UOI_NAME */, sb, sb.Capacity * 2, out needed)) return 1;
            return string.Equals(sb.ToString(), "Default", StringComparison.OrdinalIgnoreCase) ? 0 : 1;
        }
        finally { CloseDesktop(h); }
    }

    class PowerWindow : NativeWindow
    {
        public int State = -1;
        public PowerWindow() { CreateHandle(new CreateParams()); }
        protected override void WndProc(ref Message m)
        {
            // WM_POWERBROADCAST / PBT_POWERSETTINGCHANGE: POWERBROADCAST_SETTING { GUID; DWORD len; DWORD data }
            if (m.Msg == 0x218 && m.WParam.ToInt64() == 0x8013)
                State = Marshal.ReadInt32(m.LParam, 20);
            base.WndProc(ref m);
        }
    }

    // GUID_CONSOLE_DISPLAY_STATE: 0 = off, 1 = on, 2 = dimmed, -1 = unknown.
    // Windows sends the current value as soon as the notification is registered.
    public static int DisplayState()
    {
        var w = new PowerWindow();
        Guid g = new Guid("6FE69556-704A-47A0-8F24-C28D936FDA47");
        IntPtr h = RegisterPowerSettingNotification(w.Handle, ref g, 0 /* DEVICE_NOTIFY_WINDOW_HANDLE */);
        try
        {
            var sw = System.Diagnostics.Stopwatch.StartNew();
            while (w.State < 0 && sw.ElapsedMilliseconds < 1000) { Application.DoEvents(); System.Threading.Thread.Sleep(10); }
            return w.State;
        }
        finally { if (h != IntPtr.Zero) UnregisterPowerSettingNotification(h); w.DestroyHandle(); }
    }
}
"@
}

function Test-SzBenchEnvironment {
    $locked = [SzBenchEnv]::SessionLocked()
    $display = [SzBenchEnv]::DisplayState()
    $period = [SzBenchEnv]::RefreshPeriodMs()
    $reasons = @()
    if ($locked -eq 1) { $reasons += "session is locked" }
    if ($display -eq 0) { $reasons += "display is off" }
    if ($period -le 0) { $reasons += "DWM reports no refresh timing" }
    return [pscustomobject]@{ Ok = ($reasons.Count -eq 0); Reason = ($reasons -join "; ");
                              Locked = $locked; Display = $display; RefreshPeriodMs = [math]::Round($period, 4) }
}
