# =============================================================================
# SmoothZoom dev tool: observe how wheel/touchpad scroll reaches a WH_MOUSE_LL hook
# -----------------------------------------------------------------------------
# Observe-only. Installs a low-level mouse hook in THIS PowerShell process (no
# UIAccess, no elevation), never consumes anything, and prints one line per
# WM_MOUSEWHEEL / WM_MOUSEHWHEEL with modifier state. Ends with a summary that
# answers: does Shift + wheel / two-finger scroll arrive as WHEEL, or is it
# converted to HWHEEL before the hook sees it? (R-07, R-08, AC-2.1.05)
#
# Precision-touchpad scroll in Direct-Manipulation apps (Edge, Explorer,
# Settings) is consumed by DManip and usually never reaches a LL hook. Seeing
# NOTHING while scrolling there is itself the finding.
#
# Usage:  powershell -ExecutionPolicy Bypass -File tools\observe-wheel.ps1 [-Seconds 60] [-Out file.csv]
# =============================================================================
param(
    [int]$Seconds = 60,
    [string]$Out = ""
)

$ErrorActionPreference = "Stop"

Add-Type -ReferencedAssemblies System.Windows.Forms @"
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Windows.Forms;

public static class WheelObserver {
    const int WH_MOUSE_LL = 14, WM_MOUSEWHEEL = 0x020A, WM_MOUSEHWHEEL = 0x020E;
    [StructLayout(LayoutKind.Sequential)] struct POINT { public int x, y; }
    [StructLayout(LayoutKind.Sequential)] struct MSLLHOOKSTRUCT { public POINT pt; public uint mouseData, flags, time; public IntPtr extra; }
    delegate IntPtr HookProc(int code, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] static extern IntPtr SetWindowsHookEx(int id, HookProc fn, IntPtr mod, uint tid);
    [DllImport("user32.dll")] static extern bool UnhookWindowsHookEx(IntPtr h);
    [DllImport("user32.dll")] static extern IntPtr CallNextHookEx(IntPtr h, int code, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] static extern short GetAsyncKeyState(int vk);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("kernel32.dll")] static extern IntPtr GetModuleHandle(string n);

    static HookProc s_proc = Proc;   // keep the delegate alive
    static IntPtr s_hook;
    static Stopwatch s_sw = Stopwatch.StartNew();
    public static List<string> Rows = new List<string>();

    static bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

    static IntPtr Proc(int code, IntPtr wParam, IntPtr lParam) {
        if (code >= 0) {
            int msg = wParam.ToInt32();
            if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) {
                var s = (MSLLHOOKSTRUCT)Marshal.PtrToStructure(lParam, typeof(MSLLHOOKSTRUCT));
                short delta = (short)(s.mouseData >> 16);
                uint pid; GetWindowThreadProcessId(GetForegroundWindow(), out pid);
                string fg = "?";
                try { fg = Process.GetProcessById((int)pid).ProcessName; } catch {}
                Rows.Add(string.Format("{0:F1},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10}",
                    s_sw.Elapsed.TotalMilliseconds, msg == WM_MOUSEWHEEL ? "WHEEL" : "HWHEEL", delta,
                    (s.flags & 1) != 0 ? 1 : 0, Down(0x10) ? 1 : 0, Down(0x11) ? 1 : 0, Down(0x12) ? 1 : 0,
                    (Down(0x5B) || Down(0x5C)) ? 1 : 0, s.pt.x, s.pt.y, fg));
                Console.WriteLine(Rows[Rows.Count - 1]);
            }
        }
        return CallNextHookEx(s_hook, code, wParam, lParam);
    }

    public static void Run(int seconds) {
        s_hook = SetWindowsHookEx(WH_MOUSE_LL, s_proc, GetModuleHandle(null), 0);
        if (s_hook == IntPtr.Zero) throw new Exception("SetWindowsHookEx failed: " + Marshal.GetLastWin32Error());
        var t = new Timer(); t.Interval = seconds * 1000; t.Tick += (o, e) => Application.ExitThread(); t.Start();
        Application.Run();   // message pump: LL hooks need one on the installing thread
        UnhookWindowsHookEx(s_hook);
    }
}
"@

Write-Host "Observing WH_MOUSE_LL wheel events for $Seconds s (observe-only; nothing is consumed)."
Write-Host "Suggested gestures, ~3 s each, with Notepad focused, then with Edge focused:"
Write-Host "  1. two-finger scroll up/down       2. Shift + two-finger scroll up/down"
Write-Host "  3. Win + two-finger scroll         4. (if a mouse is attached) wheel, then Shift + wheel"
Write-Host ""
Write-Host "t_ms,kind,delta,injected,shift,ctrl,alt,win,x,y,foreground"
[WheelObserver]::Run($Seconds)

$rows = [WheelObserver]::Rows
if ($Out) {
    @("t_ms,kind,delta,injected,shift,ctrl,alt,win,x,y,foreground") + $rows | Set-Content -Encoding utf8 $Out
    Write-Host "Wrote $($rows.Count) rows to $Out"
}

Write-Host "`n=== Summary (kind x shift x foreground) ==="
$rows | ForEach-Object {
    $f = $_.Split(',')
    [pscustomobject]@{ kind = $f[1]; shift = $f[4]; win = $f[7]; injected = $f[3]; foreground = $f[10]; absDelta = [math]::Abs([int]$f[2]) }
} | Group-Object kind, shift, win, injected, foreground | ForEach-Object {
    $d = $_.Group | Measure-Object absDelta -Minimum -Maximum -Average
    "{0,-40} n={1,-5} |delta| min={2} max={3} avg={4:N1}" -f $_.Name, $_.Count, $d.Minimum, $d.Maximum, $d.Average
}
