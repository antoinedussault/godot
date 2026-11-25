using System;
using System.Drawing;
using System.Windows.Forms;

namespace TunaEditor
{
    /// <summary>
    /// The 3D view, in its own window.
    ///
    /// The engine's window is reparented into <see cref="ViewportHost"/>, so this form owns the
    /// viewport while the main window keeps the menus and docks -- the same split the Creation
    /// Kit uses between its main window and its render window.
    ///
    /// Closing this window closes the world: there is nothing left to edit without it.
    /// </summary>
    internal sealed class WorldEditorForm : Form
    {
        private readonly Panel viewportHost;

        /// <summary>The control the engine window is parented into.</summary>
        public Panel ViewportHost { get { return viewportHost; } }

        /// <summary>Raised when the user closes the window, so the owner can stop the engine.</summary>
        public event EventHandler ClosedByUser;

        public WorldEditorForm()
        {
            Text = "World Editor";
            Icon = Program.AppIcon;
            ClientSize = new Size(1280, 800);
            StartPosition = FormStartPosition.CenterScreen;

            viewportHost = new Panel();
            viewportHost.Dock = DockStyle.Fill;
            // The engine child window covers this entirely; skipping the background paint stops
            // a flash of panel colour underneath it while resizing.
            viewportHost.SetStyle_OptOutOfBackgroundPaint();
            Controls.Add(viewportHost);

            FormClosing += OnFormClosing;
        }

        private void OnFormClosing(object sender, FormClosingEventArgs e)
        {
            // The owner decides what closing means; it keeps the window and reuses it rather
            // than disposing the panel the engine is parented into.
            if (e.CloseReason == CloseReason.UserClosing)
            {
                e.Cancel = true;
                Hide();

                EventHandler handler = ClosedByUser;
                if (handler != null)
                {
                    handler(this, EventArgs.Empty);
                }
            }
        }

        private string worldName = string.Empty;
        private string worldKind = string.Empty;
        private string cameraPosition = string.Empty;

        /// <summary>
        /// Title bar as a readout: which world, whether it is exterior or interior, and where
        /// the camera is. Rebuilt from whatever pieces are known so far, so a missing one just
        /// leaves that section out rather than printing a placeholder.
        /// </summary>
        private void RefreshTitle()
        {
            string text = "World Editor";
            if (worldName.Length > 0) text += " - " + worldName;
            if (worldKind.Length > 0) text += " - " + worldKind;
            if (cameraPosition.Length > 0) text += " - " + cameraPosition;

            if (Text != text)
            {
                Text = text;
            }
        }

        public void SetWorld(string name, string kind)
        {
            worldName = name ?? string.Empty;
            worldKind = kind ?? string.Empty;
            RefreshTitle();
        }

        public void SetCameraPosition(string position)
        {
            cameraPosition = position ?? string.Empty;
            RefreshTitle();
        }

        /// <summary>Shows the window and brings it forward without stealing focus repeatedly.</summary>
        public void Present(string title)
        {
            worldName = string.IsNullOrEmpty(title) ? string.Empty : title;
            // A fresh world: the previous reading is not this world's.
            cameraPosition = string.Empty;
            RefreshTitle();

            if (!Visible)
            {
                Show();
            }
            if (WindowState == FormWindowState.Minimized)
            {
                WindowState = FormWindowState.Normal;
            }
            Activate();
        }
    }
}
