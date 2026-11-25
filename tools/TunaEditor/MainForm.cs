using System;
using System.Drawing;
using System.IO;
using System.Windows.Forms;

namespace TunaEditor
{
    /// <summary>
    /// Editor shell. The engine viewport is embedded in the centre; every piece of editor UI
    /// around it is ours. Menu items that still need the engine data bridge report that on the
    /// status bar rather than failing silently.
    /// </summary>
    internal sealed class MainForm : Form
    {
        private const string NeedsBridge = "needs the engine data bridge";

        // The main window is a backdrop now: the 3D view has its own window, and nothing
        // here is interactive.
        private readonly Panel canvas;
        private readonly StatusStrip statusStrip;
        private readonly ToolStripStatusLabel statusLabel;

        // Was the toolbar's text box. The path is state the editor needs, not a widget.
        private string projectPath;

        private AssetBrowserForm assetBrowserWindow;
        private readonly WorldEditorForm worldWindow;

        private const float DefaultSpeedMin = 0.25f;
        private const float DefaultSpeedMax = 4000f;
        private float cameraSpeedMin = DefaultSpeedMin;
        private float cameraSpeedMax = DefaultSpeedMax;
        private ToolStripMenuItem cellBoundsToggle;
        private string selfTestLogPath;   // set by --selftest; drives the editor without a mouse
        private ToolStripMenuItem[] debugViewItems;
        private int debugViewMode;
        private ToolStripMenuItem minSpeedItem;
        private ToolStripMenuItem maxSpeedItem;

        private readonly EngineHost engine;
        private readonly string enginePath;
        private string scenePath;
        private readonly bool autoStart;

        public string SelfTestLogPath { set { selfTestLogPath = value; } }

        public MainForm(string enginePath, string initialProject, string scenePath, bool autoStart)
        {
            this.enginePath = enginePath;
            this.projectPath = initialProject ?? string.Empty;
            this.scenePath = scenePath;
            this.autoStart = autoStart;

            Text = "Tuna Editor";
            Icon = Program.AppIcon;
            ClientSize = new Size(1440, 860);
            StartPosition = FormStartPosition.CenterScreen;

            // --- canvas ----------------------------------------------------------------
            // Deliberately empty and inert: no child controls, no click handling, nothing to
            // drop onto. Just the wordmark.
            canvas = new Panel();
            canvas.Dock = DockStyle.Fill;
            canvas.Paint += OnCanvasPaint;
            canvas.Resize += delegate { canvas.Invalidate(); };

            // --- status ----------------------------------------------------------------
            statusLabel = new ToolStripStatusLabel("Idle.");
            statusLabel.Spring = true;
            statusLabel.TextAlign = ContentAlignment.MiddleLeft;
            statusStrip = new StatusStrip();
            statusStrip.Items.Add(statusLabel);

            MenuStrip menuStrip = BuildMenu();

            // Docking is applied from the highest control index down, so the fill control goes
            // in first and the outermost edge (the menu) goes in last.
            Controls.Add(canvas);
            Controls.Add(statusStrip);
            Controls.Add(menuStrip);
            MainMenuStrip = menuStrip;

            // The 3D view lives in its own window; this one keeps the menus and docks.
            // Created hidden so the engine host has a stable panel to parent into, and
            // shown the moment a world is opened.
            worldWindow = new WorldEditorForm();
            worldWindow.Owner = this;
            worldWindow.ClosedByUser += delegate { CloseWorld(); };

            engine = new EngineHost(worldWindow.ViewportHost);
            engine.StatusChanged += delegate(object s, string message)
            {
                SetStatus(message);
                SelfTestLog("status: " + message);
            };
            engine.EngineMessage += OnEngineMessage;
            // Attach only means the window exists. Overlays and the self test wait for
            // TUNA_READY, which the engine sends once the world is actually loaded.
            engine.Attached += delegate { UpdateRunState(); };
            engine.Detached += delegate { UpdateRunState(); };

            UpdateSpeedMenu();

            FormClosing += OnFormClosing;
            Shown += OnShown;
        }

        /// <summary>
        /// The wordmark, sized to the window and tracked out wide. Drawn rather than placed as
        /// a Label so it stays a painted backdrop -- there is no control here to click, focus
        /// or drop onto.
        /// </summary>
        private void OnCanvasPaint(object sender, PaintEventArgs e)
        {
            const string mark = "TUNA TOOLSET";

            int width = canvas.ClientSize.Width;
            int height = canvas.ClientSize.Height;
            if (width <= 0 || height <= 0) return;

            e.Graphics.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAlias;

            // Scaled off the window so it reads the same at any size, and kept clear of the edges.
            float size = Math.Max(16f, Math.Min(width / 16f, height / 5f));
            float tracking = size * 0.22f;

            using (Font font = new Font(Font.FontFamily, size, FontStyle.Regular, GraphicsUnit.Pixel))
            using (Brush brush = new SolidBrush(Color.FromArgb(26, SystemColors.ControlText)))
            {
                // Measured per character so the letters can be tracked apart; DrawString has no
                // letter-spacing of its own.
                float total = 0f;
                float[] advances = new float[mark.Length];
                for (int i = 0; i < mark.Length; i++)
                {
                    advances[i] = e.Graphics.MeasureString(mark[i].ToString(), font,
                        PointF.Empty, StringFormat.GenericTypographic).Width;
                    total += advances[i] + tracking;
                }
                total -= tracking;

                float x = (width - total) * 0.5f;
                float y = (height - font.GetHeight(e.Graphics)) * 0.5f;

                for (int i = 0; i < mark.Length; i++)
                {
                    e.Graphics.DrawString(mark[i].ToString(), font, brush, x, y,
                        StringFormat.GenericTypographic);
                    x += advances[i] + tracking;
                }
            }
        }

        // -------------------------------------------------------------------------------
        // Menu
        // -------------------------------------------------------------------------------

        private MenuStrip BuildMenu()
        {
            MenuStrip menu = new MenuStrip();

            // --- World -----------------------------------------------------------------
            ToolStripMenuItem world = new ToolStripMenuItem("&World");
            world.DropDownItems.Add(WithIcon(Pending("New World", Keys.Control | Keys.N), MenuIcons.NewWorld));
            world.DropDownItems.Add(WithIcon(Item("Open World", Keys.Control | Keys.O, OnOpenSceneClicked), MenuIcons.OpenWorld));
            world.DropDownItems.Add(new ToolStripSeparator());
            world.DropDownItems.Add(WithIcon(Pending("Save World", Keys.Control | Keys.S), MenuIcons.SaveWorld));
            world.DropDownItems.Add(Pending("Save World As", Keys.None));
            world.DropDownItems.Add(new ToolStripSeparator());
            world.DropDownItems.Add(WithIcon(Pending("World Property", Keys.None), MenuIcons.WorldProperty));
            world.DropDownItems.Add(new ToolStripSeparator());
            world.DropDownItems.Add(Item("Play World", Keys.F5, delegate { PlayCurrentWorld(); }));
            world.DropDownItems.Add(Item("Reload World", Keys.Control | Keys.R, delegate { RestartEngine(); }));
            world.DropDownItems.Add(new ToolStripSeparator());
            world.DropDownItems.Add(Item("Exit Editor", Keys.None, delegate { Close(); }));

            // --- Edit ------------------------------------------------------------------
            ToolStripMenuItem edit = new ToolStripMenuItem("&Edit");
            edit.DropDownItems.Add(Pending("Undo", Keys.Control | Keys.Z));
            edit.DropDownItems.Add(Pending("Redo", Keys.Control | Keys.Y));
            edit.DropDownItems.Add(new ToolStripSeparator());
            edit.DropDownItems.Add(WithIcon(Pending("Cut", Keys.Control | Keys.X), MenuIcons.Cut));
            edit.DropDownItems.Add(WithIcon(Pending("Copy", Keys.Control | Keys.C), MenuIcons.Copy));
            edit.DropDownItems.Add(WithIcon(Pending("Paste", Keys.Control | Keys.V), MenuIcons.Paste));
            edit.DropDownItems.Add(new ToolStripSeparator());
            edit.DropDownItems.Add(Pending("Select All", Keys.Control | Keys.A));

            // --- Tools -----------------------------------------------------------------
            ToolStripMenuItem tools = new ToolStripMenuItem("&Tools");
            tools.DropDownItems.Add(Item("Assets Browser", Keys.F1, delegate { ShowAssetBrowser(); }));
            tools.DropDownItems.Add(new ToolStripSeparator());
            tools.DropDownItems.Add(Item("Open Project...", Keys.None, OnBrowseClicked));
            tools.DropDownItems.Add(new ToolStripSeparator());
            tools.DropDownItems.Add(Pending("Terrain Sculpt", Keys.None));
            tools.DropDownItems.Add(Pending("Terrain Path", Keys.None));
            tools.DropDownItems.Add(Pending("Foliage Paint", Keys.None));
            tools.DropDownItems.Add(Pending("Mesh Packer", Keys.None));

            // --- View ------------------------------------------------------------------
            ToolStripMenuItem view = new ToolStripMenuItem("&View");
            view.DropDownItems.Add(BuildDebugViewMenu());

            cellBoundsToggle = new ToolStripMenuItem("Tile Bound");
            cellBoundsToggle.CheckOnClick = true;
            cellBoundsToggle.ShortcutKeys = Keys.F2;
            cellBoundsToggle.Click += delegate { SendCellBounds(); };
            view.DropDownItems.Add(cellBoundsToggle);

            view.DropDownItems.Add(new ToolStripSeparator());
            view.DropDownItems.Add(Item("Focus Viewport", Keys.F6, delegate { engine.FocusEngine(); }));

            ToolStripMenuItem speed = new ToolStripMenuItem("Camera Speed");
            minSpeedItem = Item("Minimum...", Keys.None, delegate { EditSpeedLimit(true); });
            maxSpeedItem = Item("Maximum...", Keys.None, delegate { EditSpeedLimit(false); });
            speed.DropDownItems.Add(minSpeedItem);
            speed.DropDownItems.Add(maxSpeedItem);
            speed.DropDownItems.Add(new ToolStripSeparator());
            speed.DropDownItems.Add(Item("Reset", Keys.None, delegate
            {
                cameraSpeedMin = DefaultSpeedMin;
                cameraSpeedMax = DefaultSpeedMax;
                ApplySpeedLimits();
            }));
            view.DropDownItems.Add(speed);

            // --- Help ------------------------------------------------------------------
            ToolStripMenuItem help = new ToolStripMenuItem("&Help");
            help.DropDownItems.Add(Item("Shortcuts", Keys.None, delegate { ShowShortcuts(); }));
            help.DropDownItems.Add(new ToolStripSeparator());
            help.DropDownItems.Add(Item("About Tuna Editor", Keys.None, delegate { ShowAbout(); }));

            menu.Items.Add(world);
            menu.Items.Add(edit);
            menu.Items.Add(tools);
            menu.Items.Add(view);
            menu.Items.Add(help);
            return menu;
        }

        /// <summary>
        /// Viewport debug draw. Godot 3 has exactly four modes; the two that no renderer path
        /// backs say so rather than appearing to work.
        /// </summary>
        private ToolStripMenuItem BuildDebugViewMenu()
        {
            ToolStripMenuItem debug = new ToolStripMenuItem("DebugView");

            debugViewItems = new ToolStripMenuItem[4];
            debugViewItems[0] = MakeDebugView("Wireframe", 3);
            debugViewItems[1] = MakeDebugView("Shaded", 0);
            debugViewItems[2] = MakeDebugView("UnShaded", 1);
            debugViewItems[3] = MakeDebugView("OverDraw", 2);

            debug.DropDownItems.Add(debugViewItems[0]);
            debug.DropDownItems.Add(debugViewItems[1]);
            debug.DropDownItems.Add(debugViewItems[2]);
            debug.DropDownItems.Add(MakeUnsupportedDebugView("Shaded Wireframe"));
            debug.DropDownItems.Add(MakeUnsupportedDebugView("Level of Details"));
            debug.DropDownItems.Add(debugViewItems[3]);

            debugViewItems[1].Checked = true; // Shaded is the engine default
            return debug;
        }

        private ToolStripMenuItem MakeDebugView(string text, int mode)
        {
            ToolStripMenuItem item = new ToolStripMenuItem(text);
            item.Tag = mode;
            item.Click += delegate { SetDebugView(item, mode); };
            return item;
        }

        private ToolStripMenuItem MakeUnsupportedDebugView(string text)
        {
            ToolStripMenuItem item = new ToolStripMenuItem(text);
            item.Click += delegate
            {
                SetStatus(text + " - no renderer support in this engine; it needs a new debug draw mode.");
            };
            return item;
        }

        private void SetDebugView(ToolStripMenuItem chosen, int mode)
        {
            foreach (ToolStripMenuItem item in debugViewItems)
            {
                item.Checked = (item == chosen);
            }

            debugViewMode = mode;

            if (!engine.IsRunning)
            {
                SetStatus(chosen.Text + " - applies when a world is open.");
                return;
            }

            engine.SendCommand("debug_draw " + mode);
            SetStatus("Debug view: " + chosen.Text);
        }

        private void ShowShortcuts()
        {
            MessageBox.Show(this,
                "Viewport\r\n" +
                "  Right mouse    look\r\n" +
                "  W A S D        move,  Q / E  down / up\r\n" +
                "  Shift / Alt    faster / slower\r\n" +
                "  Mouse wheel    fly speed\r\n" +
                "  Escape         release the pointer\r\n" +
                "  Left click     select\r\n\r\n" +
                "Editor\r\n" +
                "  F1   assets browser\r\n" +
                "  F2   tile bounds\r\n" +
                "  F5   play world\r\n" +
                "  F6   focus viewport\r\n" +
                "  F11  maximise viewport\r\n" +
                "  Ctrl+O  open world     Ctrl+R  reload world",
                "Shortcuts", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }

        private void ShowAbout()
        {
            MessageBox.Show(this,
                "Tuna Editor\r\n\r\n" +
                "Hosts the Tuna engine in edit mode: the world renders, but its scripts,\r\n" +
                "autoloads and physics do not run.\r\n\r\n" +
                "Engine: " + enginePath,
                "About", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }

        private static ToolStripMenuItem WithIcon(ToolStripMenuItem item, Image image)
        {
            item.Image = image;
            return item;
        }

        private static ToolStripMenuItem Item(string text, Keys shortcut, EventHandler onClick)
        {
            ToolStripMenuItem item = new ToolStripMenuItem(text);
            if (shortcut != Keys.None)
            {
                item.ShortcutKeys = shortcut;
            }
            item.Click += onClick;
            return item;
        }

        /// <summary>
        /// A menu entry whose behaviour lives on the far side of the engine bridge. It stays
        /// enabled so the menu structure is visible, but says plainly that it is not wired.
        /// </summary>
        private ToolStripMenuItem Pending(string text, Keys shortcut)
        {
            ToolStripMenuItem item = new ToolStripMenuItem(text);
            if (shortcut != Keys.None)
            {
                item.ShortcutKeys = shortcut;
            }
            string label = text.Replace("&&", "&").TrimEnd('.');
            item.Click += delegate { SetStatus(label + " -- not wired yet: " + NeedsBridge + "."); };
            return item;
        }

        /// <summary>
        /// Opens the asset browser in its own window, or brings the existing one forward.
        /// It is modeless so you can keep working in the viewport alongside it.
        /// </summary>
        private void ShowAssetBrowser()
        {
            ShowAssetBrowser(null);
        }

        private void ShowAssetBrowser(string category)
        {
            if (assetBrowserWindow != null && !assetBrowserWindow.IsDisposed)
            {
                assetBrowserWindow.Browser.SetRoot(projectPath);
                if (category != null) assetBrowserWindow.Browser.ShowCategory(category);
                assetBrowserWindow.Activate();
                return;
            }

            assetBrowserWindow = new AssetBrowserForm(projectPath);
            assetBrowserWindow.Owner = this;
            assetBrowserWindow.Browser.StatusChanged += delegate(object s, string message) { SetStatus(message); };
            assetBrowserWindow.Browser.SceneOpenRequested += OnSceneOpenRequested;
            assetBrowserWindow.Browser.ScenePlayRequested += OnScenePlayRequested;
            assetBrowserWindow.FormClosed += delegate { assetBrowserWindow = null; };
            assetBrowserWindow.Show(this);
            if (category != null) assetBrowserWindow.Browser.ShowCategory(category);
        }

        /// <summary>World -> Open: load that scene into the embedded viewport.</summary>
        private void OnSceneOpenRequested(object sender, string resPath)
        {
            scenePath = resPath;
            SetStatus("Opening " + resPath + " ...");
            worldWindow.Present(WorldNameOf(resPath));

            // Picking a world is the end of browsing, so get the browser out of the way.
            // Deferred: this runs from the browser's own click handler, and closing a form
            // while its event is still on the stack disposes controls that are about to be
            // touched again on the way out.
            CloseAssetBrowser();

            RestartEngine();
        }

        private void CloseAssetBrowser()
        {
            if (assetBrowserWindow == null || assetBrowserWindow.IsDisposed)
            {
                return;
            }

            AssetBrowserForm window = assetBrowserWindow;
            try
            {
                window.BeginInvoke(new Action(delegate
                {
                    if (!window.IsDisposed)
                    {
                        window.Close();
                    }
                }));
            }
            catch (InvalidOperationException)
            {
                // No handle to post to; nothing left to close.
            }
        }

        /// <summary>
        /// World -> Play: run the scene in its own engine process, outside the editor, so it
        /// behaves like the shipped game rather than something embedded in a panel.
        /// </summary>
        private void OnScenePlayRequested(object sender, string resPath)
        {
            string project = projectPath;
            if (!File.Exists(enginePath))
            {
                MessageBox.Show(this, "Engine binary not found:\n" + enginePath,
                    "Tuna Editor", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }

            try
            {
                System.Diagnostics.ProcessStartInfo psi =
                    new System.Diagnostics.ProcessStartInfo(enginePath);
                psi.UseShellExecute = false;
                psi.WorkingDirectory = Path.GetDirectoryName(enginePath);
                psi.Arguments = "--path \"" + project + "\" \"" + resPath + "\"";
                System.Diagnostics.Process.Start(psi);
                SetStatus("Playing " + resPath + " in a separate window.");
            }
            catch (Exception ex)
            {
                SetStatus("Could not play " + resPath + ": " + ex.Message);
            }
        }

        /// <summary>
        /// Camera speed limits live in the engine and are passed at launch, so a change applies
        /// when the world is next loaded. If one is open, offer to reload it now.
        /// </summary>
        private void EditSpeedLimit(bool minimum)
        {
            string title = minimum ? "Minimum camera speed" : "Maximum camera speed";
            float current = minimum ? cameraSpeedMin : cameraSpeedMax;

            string answer = Microsoft.VisualBasic.Interaction.InputBox(
                title + " (metres per second):", "Camera Speed",
                current.ToString(System.Globalization.CultureInfo.InvariantCulture));

            if (string.IsNullOrEmpty(answer)) return; // cancelled

            float value;
            if (!float.TryParse(answer.Trim(), System.Globalization.NumberStyles.Float,
                    System.Globalization.CultureInfo.InvariantCulture, out value) || value <= 0f)
            {
                MessageBox.Show(this, "Enter a positive number.", "Camera Speed",
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            if (minimum)
            {
                cameraSpeedMin = value;
                if (cameraSpeedMax < cameraSpeedMin) cameraSpeedMax = cameraSpeedMin;
            }
            else
            {
                cameraSpeedMax = value;
                if (cameraSpeedMin > cameraSpeedMax) cameraSpeedMin = cameraSpeedMax;
            }

            ApplySpeedLimits();
        }

        private void ApplySpeedLimits()
        {
            UpdateSpeedMenu();

            if (!engine.IsRunning)
            {
                SetStatus("Camera speed " + FormatSpeed(cameraSpeedMin) + " to " + FormatSpeed(cameraSpeedMax) + ".");
                return;
            }

            DialogResult answer = MessageBox.Show(this,
                "Camera speed is set when the world loads.\n\nReload it now?",
                "Camera Speed", MessageBoxButtons.YesNo, MessageBoxIcon.Question);

            if (answer == DialogResult.Yes)
            {
                RestartEngine();
            }
            else
            {
                SetStatus("Camera speed " + FormatSpeed(cameraSpeedMin) + " to " + FormatSpeed(cameraSpeedMax)
                    + " - applies on next load.");
            }
        }

        private void UpdateSpeedMenu()
        {
            if (minSpeedItem != null) minSpeedItem.Text = "Minimum...   (" + FormatSpeed(cameraSpeedMin) + ")";
            if (maxSpeedItem != null) maxSpeedItem.Text = "Maximum...   (" + FormatSpeed(cameraSpeedMax) + ")";
        }

        private static string FormatSpeed(float value)
        {
            return value.ToString("0.##", System.Globalization.CultureInfo.InvariantCulture) + " m/s";
        }

        /// <summary>
        /// Cell borders are drawn by the engine, so the toggle is a command down the stdin
        /// channel rather than anything this process draws.
        /// </summary>
        private void SendCellBounds()
        {
            bool on = cellBoundsToggle.Checked;

            if (!engine.IsRunning)
            {
                SetStatus("Cell bounds " + (on ? "on" : "off") + " - applies when a world is open.");
                return;
            }

            if (engine.SendCommand("cell_bounds " + (on ? "1" : "0")))
            {
                SetStatus("Cell bounds " + (on ? "on" : "off") + ".");
            }
            else
            {
                SetStatus("Could not reach the engine to set cell bounds.");
            }
        }

        /// <summary>
        /// TUNA_ lines coming back from the engine: selection reports and command acks.
        /// </summary>
        /// <summary>
        /// Test hook. Exercising the real menu path needs a click, which is not something to
        /// synthesise on a live desktop -- so --selftest drives the same handlers directly and
        /// records what came back, letting the editor be verified instead of just the engine.
        /// </summary>
        private void SelfTestLog(string line)
        {
            if (selfTestLogPath == null) return;
            try { File.AppendAllText(selfTestLogPath, line + Environment.NewLine); }
            catch (IOException) { }
        }

        private void RunSelfTest()
        {
            SelfTestLog("editor: engine attached, running cell-bounds check");

            cellBoundsToggle.Checked = true;
            SendCellBounds();
            SelfTestLog("editor: sent cell_bounds 1 -> status: " + statusLabel.Text);

            Timer step = new Timer();
            step.Interval = 2500;
            int phase = 0;
            step.Tick += delegate
            {
                phase++;
                if (phase == 1)
                {
                    cellBoundsToggle.Checked = false;
                    SendCellBounds();
                    SelfTestLog("editor: sent cell_bounds 0 -> status: " + statusLabel.Text);
                }
                else
                {
                    step.Stop();
                    step.Dispose();
                    SelfTestLog("editor: done");
                    Close();
                }
            };
            step.Start();
        }

        private void OnEngineMessage(object sender, string line)
        {
            SelfTestLog(line);

            if (line.StartsWith("TUNA_SEL:"))
            {
                string body = line.Substring("TUNA_SEL:".Length);
                string[] parts = body.Split('|');
                string name = parts.Length > 0 ? parts[0] : string.Empty;
                string type = parts.Length > 1 ? parts[1] : string.Empty;
                string cell = parts.Length > 2 ? parts[2] : string.Empty;

                if (name.Length == 0)
                {
                    SetStatus("Nothing selected.");
                    return;
                }

                SetStatus("Selected: " + name + "  (" + type + ")"
                    + (cell.Length > 0 ? "  in " + cell : string.Empty));
                return;
            }

            if (line.StartsWith("TUNA_READY:"))
            {
                string payload = line.Substring("TUNA_READY:".Length);
                SetStatus("World loaded: " + payload);

                // TUNA_READY carries key=value fields after the scene path.
                string kind = string.Empty;
                foreach (string field in payload.Split('|'))
                {
                    if (field.StartsWith("kind=")) kind = field.Substring("kind=".Length);
                }
                worldWindow.SetWorld(WorldNameOf(scenePath), kind);

                // Overlays are engine state and reset with each load, so re-apply what the
                // menus say once there is something to apply it to.
                if (cellBoundsToggle != null && cellBoundsToggle.Checked)
                {
                    engine.SendCommand("cell_bounds 1");
                }
                if (debugViewMode != 0)
                {
                    engine.SendCommand("debug_draw " + debugViewMode);
                }
                if (selfTestLogPath != null)
                {
                    RunSelfTest();
                }
                return;
            }

            if (line.StartsWith("TUNA_CAM:"))
            {
                string[] xyz = line.Substring("TUNA_CAM:".Length).Split(',');
                if (xyz.Length == 3)
                {
                    worldWindow.SetCameraPosition(FormatCoordinate(xyz[0]) + ", "
                        + FormatCoordinate(xyz[1]) + ", " + FormatCoordinate(xyz[2]));
                }
                return;
            }

            if (line.StartsWith("TUNA_KEY:"))
            {
                OnForwardedKey(line.Substring("TUNA_KEY:".Length).Trim());
                return;
            }

            if (line.StartsWith("TUNA_ACK:cell_bounds "))
            {
                // The engine can toggle this itself (B in the viewport), so the menu follows
                // the engine rather than assuming it is the only thing that changes it.
                string rest = line.Substring("TUNA_ACK:cell_bounds ".Length).Trim();
                if ((rest == "0" || rest == "1") && cellBoundsToggle != null)
                {
                    cellBoundsToggle.Checked = (rest == "1");
                }
                return;
            }

            if (line.StartsWith("TUNA_ERR:"))
            {
                SetStatus("Engine: " + line.Substring("TUNA_ERR:".Length));
            }
        }

        /// <summary>
        /// Runs the world currently open in the editor as the game, in its own engine process --
        /// no --tuna-edit, so scripts, autoloads and physics all run for real.
        /// </summary>
        private void PlayCurrentWorld()
        {
            if (string.IsNullOrEmpty(scenePath))
            {
                SetStatus("No world open - nothing to play.");
                return;
            }

            OnScenePlayRequested(this, scenePath);
        }

        /// <summary>
        /// A shortcut pressed while the engine viewport had focus. WinForms never saw the key,
        /// so the engine forwards it and the same handlers run as if the menu had been used.
        /// </summary>
        private void OnForwardedKey(string key)
        {
            switch (key)
            {
                case "F1":
                    ShowAssetBrowser();
                    break;
                case "F2":
                    cellBoundsToggle.Checked = !cellBoundsToggle.Checked;
                    SendCellBounds();
                    break;
                case "F5":
                    PlayCurrentWorld();
                    break;
                case "F6":
                    engine.FocusEngine();
                    break;
                case "ctrl+O":
                    OnOpenSceneClicked(this, EventArgs.Empty);
                    break;
                case "ctrl+R":
                    RestartEngine();
                    break;
            }
        }

        /// <summary>
        /// Closing the World Editor window means closing the world: without the viewport there
        /// is nothing to edit, so the engine goes down with it.
        /// </summary>
        private void CloseWorld()
        {
            engine.Stop();
            scenePath = null;
            UpdateRunState();
            SetStatus("World closed.");
        }

        /// <summary>The scene's file name, which is what the world is called day to day.</summary>
        private static string WorldNameOf(string resPath)
        {
            if (string.IsNullOrEmpty(resPath)) return string.Empty;
            string name = resPath;
            int slash = name.LastIndexOf('/');
            if (slash >= 0) name = name.Substring(slash + 1);
            int dot = name.LastIndexOf('.');
            if (dot > 0) name = name.Substring(0, dot);
            return name;
        }

        /// <summary>
        /// One decimal is enough to navigate by, and it stops the title flickering through
        /// digits that carry no meaning at walking speed.
        /// </summary>
        private static string FormatCoordinate(string raw)
        {
            float value;
            if (float.TryParse(raw.Trim(), System.Globalization.NumberStyles.Float,
                    System.Globalization.CultureInfo.InvariantCulture, out value))
            {
                return value.ToString("0.#", System.Globalization.CultureInfo.InvariantCulture);
            }
            return raw.Trim();
        }

        private void OnShown(object sender, EventArgs e)
        {
            if (autoStart)
            {
                OnStartClicked(this, EventArgs.Empty);
            }
        }

        private void OnStartClicked(object sender, EventArgs e)
        {
            if (engine.IsRunning) return;

            if (!File.Exists(enginePath))
            {
                MessageBox.Show(this, "Engine binary not found:\n" + enginePath,
                    "Tuna Editor", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }

            string project = projectPath;
            if (project.Length > 0 && !File.Exists(Path.Combine(project, "project.godot")))
            {
                MessageBox.Show(this, "No project.godot in:\n" + project,
                    "Tuna Editor", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            // Never start without a world. Launching the engine with no scene would run the
            // project's run/main_scene -- the game.
            if (string.IsNullOrEmpty(scenePath))
            {
                SetStatus("No world open - use World > Open Scene... (Ctrl+O) to pick one.");
                return;
            }

            if (!SceneExists(project, scenePath))
            {
                MessageBox.Show(this, "Scene not found:\n" + scenePath,
                    "Tuna Editor", MessageBoxButtons.OK, MessageBoxIcon.Error);
                SetStatus("Scene not found: " + scenePath);
                return;
            }

            // The viewport is in the World Editor window, so it has to be up before the
            // engine looks for somewhere to attach.
            worldWindow.Present(WorldNameOf(scenePath));

            try
            {
                // The hosted viewport is always edit mode; Play launches its own process.
                engine.Start(enginePath, project, scenePath, true, cameraSpeedMin, cameraSpeedMax, TimeSpan.FromSeconds(60));
            }
            catch (Exception ex)
            {
                SetStatus("Failed to start engine: " + ex.Message);
                return;
            }
            UpdateRunState();
        }

        private static bool SceneExists(string projectDir, string resPath)
        {
            if (string.IsNullOrEmpty(projectDir)) return false;
            if (!resPath.StartsWith("res://")) return true; // not ours to resolve
            string relative = resPath.Substring("res://".Length).Replace('/', Path.DirectorySeparatorChar);
            return File.Exists(Path.Combine(projectDir, relative));
        }

        private void OnStopClicked(object sender, EventArgs e)
        {
            SetStatus("Stopping engine...");
            engine.Stop();
            UpdateRunState();
        }

        private void RestartEngine()
        {
            if (engine.IsRunning)
            {
                engine.Stop();
            }
            OnStartClicked(this, EventArgs.Empty);
        }

        /// <summary>
        /// Picks a .tscn inside the project and reloads the engine into it.
        /// </summary>
        private void OnOpenSceneClicked(object sender, EventArgs e)
        {
            string project = projectPath;
            if (project.Length == 0 || !Directory.Exists(project))
            {
                MessageBox.Show(this, "Set a project folder first.", "Tuna Editor",
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            using (OpenFileDialog dialog = new OpenFileDialog())
            {
                dialog.Title = "Open scene";
                dialog.Filter = "Godot scenes (*.tscn;*.scn)|*.tscn;*.scn|All files (*.*)|*.*";
                dialog.InitialDirectory = project;

                if (dialog.ShowDialog(this) != DialogResult.OK) return;

                string res = ToResPath(project, dialog.FileName);
                if (res == null)
                {
                    MessageBox.Show(this,
                        "That scene is outside the project folder:\n" + dialog.FileName +
                        "\n\nThe engine can only load scenes under res://.",
                        "Tuna Editor", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }

                scenePath = res;
                SetStatus("Loading " + res + " ...");
                RestartEngine();
            }
        }

        /// <summary>
        /// Converts an absolute path to a res:// path, or null if it is outside the project.
        /// </summary>
        private static string ToResPath(string projectDir, string absolutePath)
        {
            string root = Path.GetFullPath(projectDir).TrimEnd(Path.DirectorySeparatorChar)
                + Path.DirectorySeparatorChar;
            string full = Path.GetFullPath(absolutePath);

            if (!full.StartsWith(root, StringComparison.OrdinalIgnoreCase)) return null;

            return "res://" + full.Substring(root.Length).Replace(Path.DirectorySeparatorChar, '/');
        }

        private void OnBrowseClicked(object sender, EventArgs e)
        {
            using (FolderBrowserDialog dialog = new FolderBrowserDialog())
            {
                dialog.Description = "Select a folder containing project.godot";
                if (projectPath.Length > 0 && Directory.Exists(projectPath))
                {
                    dialog.SelectedPath = projectPath;
                }
                if (dialog.ShowDialog(this) == DialogResult.OK)
                {
                    projectPath = dialog.SelectedPath;
                    SetStatus("Project: " + projectPath);
                }
            }
        }

        private void UpdateRunState()
        {
            Text = string.IsNullOrEmpty(scenePath) ? "Tuna Editor" : "Tuna Editor - " + scenePath;

            bool running = engine.IsRunning;

            if (worldWindow != null && !running && worldWindow.Visible)
            {
                worldWindow.Hide();
            }
        }

        private void SetStatus(string message)
        {
            statusLabel.Text = message;
        }

        private void OnFormClosing(object sender, FormClosingEventArgs e)
        {
            // Never leave an orphaned engine process behind.
            engine.Dispose();
        }
    }

    internal static class PanelStyleExtensions
    {
        /// <summary>
        /// Panel.SetStyle is protected, so reach it through reflection. Opting out of background
        /// painting stops the panel repainting underneath the engine child window during resize,
        /// which otherwise shows as flicker.
        /// </summary>
        public static void SetStyle_OptOutOfBackgroundPaint(this Panel panel)
        {
            typeof(Control)
                .GetMethod("SetStyle", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)
                .Invoke(panel, new object[] { ControlStyles.Opaque, true });
        }
    }
}
