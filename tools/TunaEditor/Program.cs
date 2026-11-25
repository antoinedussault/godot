using System;
using System.IO;
using System.Windows.Forms;

namespace TunaEditor
{
    internal static class Program
    {
        private const string EngineBinaryName = "godot.windows.opt.tools.64.exe";

        // Boot straight into this project. Override with --project.
        private const string DefaultProjectPath = @"C:\Users\Antoine Dussault\Documents\solander";

        // The editor opens with no world loaded. Nothing starts until a world is opened, so
        // the project's run/main_scene -- the game -- never runs by accident. Pass --scene to
        // open one at startup.
        private const string DefaultScenePath = null;

        [STAThread]
        private static void Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            string enginePath = null;
            string projectPath = null;
            string scenePath = null;
            string selfTestLog = null;
            // Nothing launches on open. --autostart only does anything alongside --scene.
            bool autoStart = false;

            for (int i = 0; i < args.Length; i++)
            {
                if (args[i] == "--engine" && i + 1 < args.Length)
                {
                    enginePath = args[++i];
                }
                else if (args[i] == "--project" && i + 1 < args.Length)
                {
                    projectPath = args[++i];
                }
                else if (args[i] == "--scene" && i + 1 < args.Length)
                {
                    // res:// path of a scene for the embedded engine to run instead of the
                    // project's configured main scene.
                    scenePath = args[++i];
                }
                else if (args[i] == "--autostart")
                {
                    autoStart = true;
                }
                else if (args[i] == "--selftest" && i + 1 < args.Length)
                {
                    selfTestLog = args[++i];
                }
                else if (args[i] == "--no-autostart")
                {
                    autoStart = false;
                }
            }

            if (string.IsNullOrEmpty(enginePath))
            {
                enginePath = LocateEngine();
            }

            if (projectPath == null)
            {
                projectPath = DefaultProjectPath;
            }

            if (scenePath == null)
            {
                scenePath = DefaultScenePath;
            }
            else
            {
                // An explicit --scene implies the user wants it opened.
                autoStart = true;
            }

            MainForm form = new MainForm(enginePath, projectPath, scenePath, autoStart);
            if (selfTestLog != null) form.SelfTestLogPath = selfTestLog;
            Application.Run(form);
        }

        private static System.Drawing.Icon appIcon;

        /// <summary>
        /// The application icon, pulled from our own executable so every window matches the
        /// icon embedded at build time. Null if it cannot be read, which no window should
        /// treat as fatal.
        /// </summary>
        public static System.Drawing.Icon AppIcon
        {
            get
            {
                if (appIcon == null)
                {
                    try { appIcon = System.Drawing.Icon.ExtractAssociatedIcon(Application.ExecutablePath); }
                    catch (Exception) { }
                }
                return appIcon;
            }
        }

        /// <summary>
        /// Walks up from the executable looking for the repo bin folder, so the spike runs
        /// straight out of its build output without any configuration.
        /// </summary>
        private static string LocateEngine()
        {
            DirectoryInfo dir = new DirectoryInfo(AppDomain.CurrentDomain.BaseDirectory);

            while (dir != null)
            {
                string candidate = Path.Combine(Path.Combine(dir.FullName, "bin"), EngineBinaryName);
                if (File.Exists(candidate))
                {
                    return candidate;
                }
                dir = dir.Parent;
            }

            // Fall back to a path that at least names what was expected in the error dialog.
            return Path.Combine("bin", EngineBinaryName);
        }
    }
}
