using System;
using System.Diagnostics;
using System.Text;
using System.Windows.Forms;

namespace TunaEditor
{
    /// <summary>
    /// Owns an out-of-process engine instance and hosts its window inside a WinForms control.
    ///
    /// Design note (why out-of-process): the engine keeps the window it creates itself, so its
    /// own WndProc, message pump, input handling and GL context all keep working untouched --
    /// zero engine changes. We only restyle that HWND to WS_CHILD and SetParent it into our
    /// panel, which is legal across process boundaries.
    ///
    /// The in-process alternative (GODOT_WINDOWID, os_windows.cpp:1503) is NOT usable here:
    /// it subclasses the host HWND via SetWindowLongPtr(GWLP_WNDPROC), which fails with
    /// ERROR_ACCESS_DENIED when the window belongs to another process.
    /// </summary>
    internal sealed class EngineHost : IDisposable
    {
        // Window class Godot registers for every window it creates (see os_windows.cpp).
        private const string EngineWindowClass = "Engine";

        private readonly Control host;
        private Process process;
        private IntPtr engineWindow = IntPtr.Zero;
        private Timer discoveryTimer;
        private Timer layoutWatchdog;
        private DateTime discoveryDeadline;

        public event EventHandler<string> StatusChanged;
        public event EventHandler Attached;
        public event EventHandler Detached;

        public bool IsAttached
        {
            get { return engineWindow != IntPtr.Zero && NativeMethods.IsWindow(engineWindow); }
        }

        public IntPtr EngineWindow { get { return engineWindow; } }

        public bool IsRunning { get { return process != null && !process.HasExited; } }

        public EngineHost(Control hostControl)
        {
            if (hostControl == null) throw new ArgumentNullException("hostControl");
            host = hostControl;
            host.Resize += delegate { LayoutEngineWindow(); };
        }

        public void Start(string enginePath, string projectPath, string scenePath, bool editMode,
            float speedMin, float speedMax, TimeSpan timeout)
        {
            if (process != null) throw new InvalidOperationException("Engine already running.");

            ProcessStartInfo psi = new ProcessStartInfo(enginePath);
            psi.UseShellExecute = false;
            psi.WorkingDirectory = System.IO.Path.GetDirectoryName(enginePath);
            // The engine polls stdin for editor commands (see TunaEditBridge), and answers on
            // stdout with TUNA_-prefixed lines. Both halves must be drained: a redirected pipe
            // that nobody reads fills up and blocks the engine.
            psi.RedirectStandardInput = true;
            psi.RedirectStandardOutput = true;

            // We host the ENGINE, not Godot's editor -- no -e flag. The engine runs the project
            // (or a specific scene) and this shell supplies all the editor UI around it.
            //
            // --tuna-edit puts the engine in edit mode: the scene loads and renders, but its
            // scripts and autoloads never run and physics is frozen, with a free-fly camera
            // added. Without it the scene simply plays, which is what Play wants.
            if (!string.IsNullOrEmpty(projectPath))
            {
                psi.Arguments = "--path \"" + projectPath + "\"";
                if (editMode)
                {
                    psi.Arguments += " --tuna-edit";
                    psi.Arguments += " --tuna-edit-speed-min " + Invariant(speedMin);
                    psi.Arguments += " --tuna-edit-speed-max " + Invariant(speedMax);
                }
                if (!string.IsNullOrEmpty(scenePath))
                {
                    psi.Arguments += " \"" + scenePath + "\"";
                }
            }

            process = Process.Start(psi);
            process.EnableRaisingEvents = true;
            process.Exited += OnProcessExited;
            process.OutputDataReceived += OnEngineOutput;
            process.BeginOutputReadLine();

            Report("Engine started (pid " + process.Id + "), waiting for its window...");

            // Godot creates its window early but not instantly, so poll rather than guess a
            // delay. WaitForInputIdle is unreliable against a custom message pump.
            discoveryDeadline = DateTime.UtcNow + timeout;
            discoveryTimer = new Timer();
            discoveryTimer.Interval = 50;
            discoveryTimer.Tick += OnDiscoveryTick;
            discoveryTimer.Start();
        }

        /// <summary>Raised on the UI thread for each TUNA_-prefixed line the engine emits.</summary>
        public event EventHandler<string> EngineMessage;

        /// <summary>
        /// Godot logs a great deal to stdout; only our own protocol lines are surfaced. Fires on
        /// a pool thread, so hop to the UI thread before anyone touches a control.
        /// </summary>
        private void OnEngineOutput(object sender, DataReceivedEventArgs e)
        {
            if (e.Data == null || !e.Data.StartsWith("TUNA_")) return;

            string line = e.Data;
            EventHandler<string> handler = EngineMessage;
            if (handler == null) return;

            if (host.IsHandleCreated && host.InvokeRequired)
            {
                try { host.BeginInvoke(new Action(delegate { handler(this, line); })); }
                catch (InvalidOperationException) { }
            }
            else
            {
                handler(this, line);
            }
        }

        /// <summary>
        /// Sends one command line to the running engine. Returns false when there is nothing to
        /// send to, so callers can report rather than assume it landed.
        /// </summary>
        public bool SendCommand(string command)
        {
            if (process == null || process.HasExited) return false;

            try
            {
                process.StandardInput.WriteLine(command);
                process.StandardInput.Flush();
                return true;
            }
            catch (System.IO.IOException)
            {
                // Engine went away between the check and the write.
                return false;
            }
            catch (ObjectDisposedException)
            {
                return false;
            }
        }

        /// <summary>The engine parses with to_double(), so the decimal point must not be a comma.</summary>
        private static string Invariant(float value)
        {
            return value.ToString("0.####", System.Globalization.CultureInfo.InvariantCulture);
        }

        private void OnDiscoveryTick(object sender, EventArgs e)
        {
            if (process == null || process.HasExited)
            {
                StopDiscovery();
                Report("Engine exited before a window appeared.");
                return;
            }

            IntPtr found = FindEngineWindow((uint)process.Id);
            if (found != IntPtr.Zero)
            {
                StopDiscovery();
                Attach(found);
                return;
            }

            if (DateTime.UtcNow > discoveryDeadline)
            {
                StopDiscovery();
                Report("Timed out waiting for the engine window.");
            }
        }

        private void StopDiscovery()
        {
            if (discoveryTimer != null)
            {
                discoveryTimer.Stop();
                discoveryTimer.Dispose();
                discoveryTimer = null;
            }
        }

        private void StopLayoutWatchdog()
        {
            if (layoutWatchdog != null)
            {
                layoutWatchdog.Stop();
                layoutWatchdog.Dispose();
                layoutWatchdog = null;
            }
        }

        /// <summary>
        /// Finds the engine main top-level window: owned by our process, class "Engine", visible,
        /// and not owned by another window (which would make it a dialog or a secondary window
        /// from OS::window_create).
        /// </summary>
        private static IntPtr FindEngineWindow(uint pid)
        {
            IntPtr result = IntPtr.Zero;
            StringBuilder buffer = new StringBuilder(64);

            NativeMethods.EnumWindows(delegate(IntPtr hWnd, IntPtr lParam)
            {
                uint windowPid;
                NativeMethods.GetWindowThreadProcessId(hWnd, out windowPid);
                if (windowPid != pid) return true;
                if (!NativeMethods.IsWindowVisible(hWnd)) return true;
                if (NativeMethods.GetWindow(hWnd, NativeMethods.GW_OWNER) != IntPtr.Zero) return true;

                buffer.Length = 0;
                NativeMethods.GetClassNameW(hWnd, buffer, buffer.Capacity);
                if (buffer.ToString() != EngineWindowClass) return true;

                NativeMethods.RECT rect;
                if (!NativeMethods.GetClientRect(hWnd, out rect)) return true;
                if (rect.Width <= 0 || rect.Height <= 0) return true;

                result = hWnd;
                return false; // stop enumerating
            }, IntPtr.Zero);

            return result;
        }

        private void Attach(IntPtr hWnd)
        {
            long style = NativeMethods.GetWindowLongPtr(hWnd, NativeMethods.GWL_STYLE).ToInt64();
            style &= ~(NativeMethods.WS_OVERLAPPEDWINDOW | NativeMethods.WS_POPUP);
            style |= NativeMethods.WS_CHILD | NativeMethods.WS_VISIBLE
                   | NativeMethods.WS_CLIPSIBLINGS | NativeMethods.WS_CLIPCHILDREN;
            NativeMethods.SetWindowLongPtr(hWnd, NativeMethods.GWL_STYLE, new IntPtr(style));

            // Keep the embedded engine out of the taskbar and Alt-Tab.
            long exStyle = NativeMethods.GetWindowLongPtr(hWnd, NativeMethods.GWL_EXSTYLE).ToInt64();
            exStyle &= ~NativeMethods.WS_EX_APPWINDOW;
            NativeMethods.SetWindowLongPtr(hWnd, NativeMethods.GWL_EXSTYLE, new IntPtr(exStyle));

            NativeMethods.SetParent(hWnd, host.Handle);
            engineWindow = hWnd;

            LayoutEngineWindow();
            Report("Attached engine window 0x" + hWnd.ToInt64().ToString("X") + " (pid " + process.Id + ").");

            // The engine drives its own window size from project settings during boot, which
            // lands seconds after the reparent and overrides our first layout. Because the
            // window lives in another process we cannot subclass it to intercept WM_SIZE, so a
            // watchdog reconciles the two. It only issues SetWindowPos when the rects actually
            // disagree, so the steady-state cost is one GetClientRect per tick.
            layoutWatchdog = new Timer();
            layoutWatchdog.Interval = 250;
            layoutWatchdog.Tick += delegate { ReconcileSize(); };
            layoutWatchdog.Start();

            EventHandler handler = Attached;
            if (handler != null) handler(this, EventArgs.Empty);
        }

        /// <summary>
        /// Sizes the engine window to fill the host control.
        /// </summary>
        public void LayoutEngineWindow()
        {
            if (!IsAttached) return;
            NativeMethods.SetWindowPos(engineWindow, IntPtr.Zero, 0, 0,
                host.ClientSize.Width, host.ClientSize.Height,
                NativeMethods.SWP_NOZORDER | NativeMethods.SWP_FRAMECHANGED | NativeMethods.SWP_SHOWWINDOW);
        }

        /// <summary>
        /// Corrects the engine window whenever it drifts from the host panel size.
        /// </summary>
        private void ReconcileSize()
        {
            if (!IsAttached) return;

            NativeMethods.RECT rect;
            if (!NativeMethods.GetClientRect(engineWindow, out rect)) return;

            if (rect.Width != host.ClientSize.Width || rect.Height != host.ClientSize.Height)
            {
                LayoutEngineWindow();
            }
        }

        /// <summary>
        /// Hands keyboard focus to the engine. Cross-process SetFocus only takes effect while the
        /// two input queues are attached, hence AttachThreadInput around it.
        /// </summary>
        public void FocusEngine()
        {
            if (!IsAttached) return;

            uint self = NativeMethods.GetCurrentThreadId();
            uint ignored;
            uint target = NativeMethods.GetWindowThreadProcessId(engineWindow, out ignored);

            if (target == 0 || target == self)
            {
                NativeMethods.SetFocus(engineWindow);
                return;
            }

            NativeMethods.AttachThreadInput(self, target, true);
            try { NativeMethods.SetFocus(engineWindow); }
            finally { NativeMethods.AttachThreadInput(self, target, false); }
        }

        private void OnProcessExited(object sender, EventArgs e)
        {
            // Raised on a pool thread; hop back to the UI thread before touching controls.
            if (host.IsHandleCreated && host.InvokeRequired)
            {
                try { host.BeginInvoke(new Action(HandleExit)); }
                catch (InvalidOperationException) { }
            }
            else
            {
                HandleExit();
            }
        }

        private void HandleExit()
        {
            StopLayoutWatchdog();
            engineWindow = IntPtr.Zero;
            Report("Engine process exited.");
            EventHandler handler = Detached;
            if (handler != null) handler(this, EventArgs.Empty);
        }

        /// <summary>
        /// Shuts the engine down politely first so it can save and release its GL context.
        /// The force path only ever targets the process this host started.
        /// </summary>
        public void Stop()
        {
            StopDiscovery();
            StopLayoutWatchdog();
            if (process == null) return;

            try
            {
                if (!process.HasExited)
                {
                    if (engineWindow != IntPtr.Zero && NativeMethods.IsWindow(engineWindow))
                    {
                        NativeMethods.PostMessage(engineWindow, NativeMethods.WM_CLOSE, IntPtr.Zero, IntPtr.Zero);
                        process.WaitForExit(5000);
                    }

                    if (!process.HasExited)
                    {
                        Report("Engine did not close on request; terminating our child process.");
                        process.Kill();
                        process.WaitForExit(2000);
                    }
                }
            }
            catch (InvalidOperationException) { /* already gone */ }
            finally
            {
                process.Exited -= OnProcessExited;
                process.OutputDataReceived -= OnEngineOutput;
                process.Dispose();
                process = null;
                engineWindow = IntPtr.Zero;
            }
        }

        private void Report(string message)
        {
            EventHandler<string> handler = StatusChanged;
            if (handler != null) handler(this, message);
        }

        public void Dispose()
        {
            Stop();
        }
    }
}
