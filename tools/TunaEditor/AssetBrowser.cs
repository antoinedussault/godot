using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;

namespace TunaEditor
{
    /// <summary>
    /// Asset browser over the project folder. Everything here is ours -- it reads the project
    /// tree straight off disk rather than going through any Godot editor machinery.
    ///
    /// Scanning, importing, folder creation and delete all work locally. Place and Load act on
    /// the running scene, so they wait on the engine data bridge.
    /// </summary>
    internal sealed class AssetBrowser : UserControl
    {
        private sealed class Category
        {
            public string Name;
            public string[] Extensions;
            public Button Button;

            /// <summary>Project-relative folder this category lives in, e.g. "models".</summary>
            public string Folder;

            /// <summary>Project settings key that can override Folder.</summary>
            public string SettingKey;

            /// <summary>Action buttons this category shows, left to right.</summary>
            public string[] Actions;
        }

        private static readonly string[] AssetActions = { "Place", "Property", "Inspect", "Delete", "Reimport" };
        private static readonly string[] WorldActions = { "Open", "Play", "Property", "Delete" };
        // Quick Play is audio-only.
        private static readonly string[] AudioActions = { "Place", "Property", "Inspect", "Delete", "Reimport", "Quick Play" };

        private sealed class AssetEntry
        {
            public string Name;
            public string Folder;     // path relative to the category folder
            public string Info;
            public string FullPath;
        }

        private static readonly string[] SkipDirectories = { ".git", ".import", ".godot", ".mono", ".vs" };

        // Each category is rooted at its own folder, so Models shows the inside of models/ and
        // never the rest of the project. SettingKey lets project.godot override the folder.
        private readonly Category[] categories = new[]
        {
            new Category { Name = "Models",    Folder = "models",    SettingKey = "assets/models_folder", Actions = AssetActions,
                           Extensions = new[] { ".obj", ".gltf", ".glb", ".dae", ".fbx", ".escn", ".mesh", ".blend" } },
            new Category { Name = "Textures",  Folder = "textures",  SettingKey = "assets/textures_folder", Actions = AssetActions,
                           Extensions = new[] { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".webp", ".exr", ".hdr", ".svg", ".dds" } },
            new Category { Name = "Materials", Folder = "materials", SettingKey = "assets/materials_folder", Actions = AssetActions,
                           Extensions = new[] { ".material", ".tres" } },
            new Category { Name = "Audios",    Folder = "audios",    SettingKey = "assets/audios_folder", Actions = AudioActions,
                           Extensions = new[] { ".wav", ".ogg", ".mp3" } },
            new Category { Name = "Worlds",    Folder = "scenes",    SettingKey = "assets/worlds_folder", Actions = WorldActions,
                           Extensions = new[] { ".tscn", ".scn" } },
        };

        private readonly Label pathLabel;
        private readonly TextBox subfolderBox;
        private readonly TextBox newFolderBox;
        private readonly TreeView folderTree;
        private readonly DataGridView grid;
        private readonly FlowLayoutPanel categoryBar;
        private readonly SplitContainer split;

        private const int TreeWidth = 190;

        private string root;
        private Category active;
        private string folderFilter;   // relative folder selected in the tree, null = everything
        private readonly List<AssetEntry> rows = new List<AssetEntry>();
        private Dictionary<string, List<AssetEntry>> cache = new Dictionary<string, List<AssetEntry>>();

        public event EventHandler<string> StatusChanged;

        /// <summary>Raised for actions that need engine-side resources (Place, Inspect, Reimport).</summary>
        public event EventHandler<string> BridgeActionRequested;

        /// <summary>Raised with a res:// path to load that scene into the editor viewport.</summary>
        public event EventHandler<string> SceneOpenRequested;

        /// <summary>Raised with a res:// path to run that scene as a standalone game window.</summary>
        public event EventHandler<string> ScenePlayRequested;

        public AssetBrowser(string projectRoot)
        {
            root = projectRoot;
            active = categories[0];


            // --- header ----------------------------------------------------------------
            Label header = new Label();
            header.Text = "Assets";
            header.Dock = DockStyle.Top;
            header.Height = 20;
            header.Padding = new Padding(6, 3, 0, 0);

            pathLabel = new Label();
            pathLabel.Dock = DockStyle.Top;
            pathLabel.Height = 18;
            pathLabel.Padding = new Padding(8, 2, 0, 0);

            // --- toolbar ---------------------------------------------------------------
            FlowLayoutPanel toolbar = new FlowLayoutPanel();
            toolbar.Dock = DockStyle.Top;
            toolbar.Height = 30;
            toolbar.Padding = new Padding(6, 2, 0, 0);
            toolbar.WrapContents = false;

            Button importButton = MakeButton("Import...", 78, OnImport);
            Button rescanButton = MakeButton("Rescan", 68, delegate { Rescan(); });

            subfolderBox = MakeHintBox("subfolder", 150);
            newFolderBox = MakeHintBox("new folder", 150);
            Button createButton = MakeButton("Create", 68, OnCreateFolder);

            toolbar.Controls.Add(importButton);
            toolbar.Controls.Add(rescanButton);
            toolbar.Controls.Add(subfolderBox);
            toolbar.Controls.Add(newFolderBox);
            toolbar.Controls.Add(createButton);

            // --- categories ------------------------------------------------------------
            categoryBar = new FlowLayoutPanel();
            categoryBar.Dock = DockStyle.Top;
            categoryBar.Height = 30;
            categoryBar.Padding = new Padding(6, 2, 0, 0);
            categoryBar.WrapContents = false;

            foreach (Category category in categories)
            {
                Category captured = category;
                Button button = MakeButton(category.Name + " (0)", 112, delegate { SelectCategory(captured); });
                category.Button = button;
                categoryBar.Controls.Add(button);
            }

            // --- tree + grid -----------------------------------------------------------
            folderTree = new TreeView();
            folderTree.Dock = DockStyle.Fill;
            folderTree.HideSelection = false;
            folderTree.AfterSelect += OnFolderSelected;

            Label hint = new Label();
            hint.Text = "Ctrl+click to add, Shift+click for a range";
            hint.Dock = DockStyle.Top;
            hint.Height = 18;
            hint.Padding = new Padding(4, 2, 0, 0);

            grid = BuildGrid();

            Panel right = new Panel();
            right.Dock = DockStyle.Fill;
            right.Controls.Add(grid);
            right.Controls.Add(hint);

            split = new SplitContainer();
            split.Dock = DockStyle.Fill;
            // Without this the splitter keeps its *proportion* on resize, so the tree grows to
            // swallow the window. Panel1 stays a fixed width instead; the grid takes the rest.
            //
            // Panel1MinSize/Panel2MinSize are NOT set here: the control is still at its default
            // 150px width during construction, and a min size that cannot fit inside that makes
            // SplitContainer throw while adjusting SplitterDistance. They are applied in OnLoad.
            split.FixedPanel = FixedPanel.Panel1;
            split.Panel1.Controls.Add(folderTree);
            split.Panel2.Controls.Add(right);

            Controls.Add(split);
            Controls.Add(categoryBar);
            Controls.Add(toolbar);
            Controls.Add(pathLabel);
            Controls.Add(header);

            Rescan();
        }

        /// <summary>
        /// Splitter geometry can only be set once the control has its real width -- in the
        /// constructor it is still 150px wide, and anything that cannot fit inside that makes
        /// SplitContainer throw. Everything here is clamped to what actually fits.
        /// </summary>
        protected override void OnLoad(EventArgs e)
        {
            base.OnLoad(e);
            ApplySplitterLayout();
        }

        private void ApplySplitterLayout()
        {
            int width = split.Width;
            if (width <= 0) return;

            int panel1Min = 100;
            int panel2Min = 300;

            // Shrink the minimums until the pair fits, rather than trusting the window is big.
            int needed = panel1Min + panel2Min + split.SplitterWidth;
            if (needed > width)
            {
                panel1Min = Math.Max(25, Math.Min(panel1Min, (width - split.SplitterWidth) / 3));
                panel2Min = Math.Max(25, width - panel1Min - split.SplitterWidth);
            }

            if (panel1Min + panel2Min + split.SplitterWidth > width) return;

            try
            {
                // Order matters: widen the allowance before moving the splitter into it.
                split.Panel1MinSize = panel1Min;
                split.Panel2MinSize = panel2Min;

                int maximum = width - panel2Min - split.SplitterWidth;
                if (maximum >= panel1Min)
                {
                    split.SplitterDistance = Math.Max(panel1Min, Math.Min(TreeWidth, maximum));
                }
            }
            catch (InvalidOperationException)
            {
                // A layout race is not worth taking the editor down for; the splitter just
                // keeps whatever position it already had.
            }
        }

        // -------------------------------------------------------------------------------
        // Construction helpers
        // -------------------------------------------------------------------------------

        private static Button MakeButton(string text, int width, EventHandler onClick)
        {
            Button button = new Button();
            button.Text = text;
            button.Width = width;
            button.Height = 24;
            button.Click += onClick;
            return button;
        }

        /// <summary>A textbox showing grey placeholder text while empty.</summary>
        private static TextBox MakeHintBox(string hint, int width)
        {
            TextBox box = new TextBox();
            box.Width = width;
            box.Text = hint;
            box.ForeColor = SystemColors.GrayText;
            box.Tag = hint;

            box.GotFocus += delegate
            {
                if (box.Text == hint)
                {
                    box.Text = string.Empty;
                    box.ForeColor = SystemColors.WindowText;
                }
            };
            box.LostFocus += delegate
            {
                if (box.Text.Trim().Length == 0)
                {
                    box.Text = hint;
                    box.ForeColor = SystemColors.GrayText;
                }
            };
            return box;
        }

        private static string ValueOf(TextBox box)
        {
            string hint = (string)box.Tag;
            string text = box.Text.Trim();
            return (text == hint) ? string.Empty : text;
        }

        private DataGridView BuildGrid()
        {
            DataGridView view = new DataGridView();
            view.Dock = DockStyle.Fill;
            view.AutoGenerateColumns = false;
            view.AllowUserToAddRows = false;
            view.AllowUserToDeleteRows = false;
            view.AllowUserToResizeRows = false;
            view.RowHeadersVisible = false;
            view.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
            view.MultiSelect = true; // Ctrl+click / Shift+click
            view.EditMode = DataGridViewEditMode.EditProgrammatically;

            // A real project has tens of thousands of assets (this one has 17k textures), and
            // adding rows one at a time is O(n) allocations plus a layout pass each. Virtual
            // mode keeps only the visible rows materialised.
            view.VirtualMode = true;
            view.CellValueNeeded += OnCellValueNeeded;

            view.CellClick += OnCellClick;
            return view;
        }

        /// <summary>
        /// Action buttons differ per category, so the columns are rebuilt whenever the category
        /// changes rather than fixed once at construction.
        /// </summary>
        private void RebuildColumns()
        {
            grid.RowCount = 0;
            grid.Columns.Clear();

            grid.Columns.Add(TextColumn("Name", 260));
            grid.Columns.Add(TextColumn("Folder", 200));
            grid.Columns.Add(TextColumn("Info", 150));

            for (int i = 0; i < active.Actions.Length; i++)
            {
                string action = active.Actions[i];
                grid.Columns.Add(ButtonColumn(i == 0 ? "Actions" : "", action, ActionWidth(action)));
            }
        }

        private static int ActionWidth(string action)
        {
            return Math.Max(58, 18 + action.Length * 8);
        }

        private static DataGridViewTextBoxColumn TextColumn(string header, int width)
        {
            DataGridViewTextBoxColumn column = new DataGridViewTextBoxColumn();
            column.HeaderText = header;
            column.Width = width;
            column.ReadOnly = true;
            column.SortMode = DataGridViewColumnSortMode.NotSortable;
            return column;
        }

        private static DataGridViewButtonColumn ButtonColumn(string header, string text, int width)
        {
            DataGridViewButtonColumn column = new DataGridViewButtonColumn();
            column.HeaderText = header;
            column.Text = text;
            column.UseColumnTextForButtonValue = true;
            column.Width = width;
            column.SortMode = DataGridViewColumnSortMode.NotSortable;
            // Identifies the action on click; DataGridViewColumn itself exposes no Text.
            column.Tag = text;
            return column;
        }

        // -------------------------------------------------------------------------------
        // Scanning
        // -------------------------------------------------------------------------------

        /// <summary>Switches to a category by name, e.g. "Worlds". Unknown names are ignored.</summary>
        public void ShowCategory(string name)
        {
            foreach (Category category in categories)
            {
                if (category.Name == name)
                {
                    SelectCategory(category);
                    return;
                }
            }
        }

        public void SetRoot(string projectRoot)
        {
            root = projectRoot;
            folderFilter = null;
            Rescan();
        }

        public void Rescan()
        {
            if (string.IsNullOrEmpty(root) || !Directory.Exists(root))
            {
                folderTree.Nodes.Clear();
                rows.Clear();
                grid.RowCount = 0;
                ScanAll();
                RefreshCounts();
                Report("Project folder not found: " + root);
                return;
            }

            Cursor previous = Cursor.Current;
            Cursor.Current = Cursors.WaitCursor;
            try
            {
                System.Diagnostics.Stopwatch timer = System.Diagnostics.Stopwatch.StartNew();
                LoadFolderSettings();
                ScanAll();
                folderFilter = null;
                RebuildColumns();
                BuildFolderTree();
                RefreshCounts();
                RefreshGrid();
                UpdatePathLabel();
                timer.Stop();

                int total = cache.Values.Sum(list => list.Count);
                Report("Scanned " + total.ToString("N0") + " assets in " + timer.ElapsedMilliseconds + " ms.");
            }
            finally
            {
                Cursor.Current = previous;
            }
        }

        /// <summary>
        /// The tree shows the inside of the active category's folder only -- models/ for Models,
        /// audios/ for Audios -- never the whole project.
        /// </summary>
        private void BuildFolderTree()
        {
            folderTree.BeginUpdate();
            folderTree.Nodes.Clear();

            string folder = FolderOf(active);
            TreeNode rootNode = new TreeNode(active.Folder);
            rootNode.Tag = string.Empty;

            if (Directory.Exists(folder))
            {
                AddChildFolders(rootNode, folder, string.Empty, 0);
            }

            folderTree.Nodes.Add(rootNode);
            rootNode.Expand();
            folderTree.SelectedNode = rootNode;

            folderTree.EndUpdate();
        }

        private static void AddChildFolders(TreeNode parent, string absolute, string relative, int depth)
        {
            if (depth > 8) return;

            string[] children;
            try { children = Directory.GetDirectories(absolute); }
            catch (UnauthorizedAccessException) { return; }
            catch (IOException) { return; }

            foreach (string child in children.OrderBy(Path.GetFileName))
            {
                string name = Path.GetFileName(child);
                if (SkipDirectories.Contains(name)) continue;
                if (name.StartsWith(".")) continue;

                string childRelative = (relative.Length == 0) ? name : relative + "/" + name;
                TreeNode node = new TreeNode(name);
                node.Tag = childRelative;
                AddChildFolders(node, child, childRelative, depth + 1);
                parent.Nodes.Add(node);
            }
        }

        /// <summary>Absolute path of a category's folder.</summary>
        private string FolderOf(Category category)
        {
            return Path.Combine(root, category.Folder.Replace('/', Path.DirectorySeparatorChar));
        }

        /// <summary>
        /// Scans each category inside its own folder only. The folders are disjoint, so this is
        /// less total work than one walk of the whole project was.
        /// </summary>
        private void ScanAll()
        {
            cache = new Dictionary<string, List<AssetEntry>>();
            foreach (Category category in categories)
            {
                cache[category.Name] = new List<AssetEntry>();
            }

            if (string.IsNullOrEmpty(root) || !Directory.Exists(root)) return;

            foreach (Category category in categories)
            {
                string folder = FolderOf(category);
                if (!Directory.Exists(folder)) continue;

                Walk(new DirectoryInfo(folder), string.Empty, category, 0);
            }
        }

        private void Walk(DirectoryInfo directory, string relative, Category category, int depth)
        {
            if (depth > 12) return;

            FileInfo[] files;
            DirectoryInfo[] children;
            try
            {
                // DirectoryInfo hands back Length from the directory enumeration itself, so this
                // costs no extra stat per file the way new FileInfo(path).Length would.
                files = directory.GetFiles();
                children = directory.GetDirectories();
            }
            catch (UnauthorizedAccessException) { return; }
            catch (IOException) { return; }

            string folder = (relative.Length == 0) ? "/" : relative;

            foreach (FileInfo file in files)
            {
                if (!category.Extensions.Contains(file.Extension.ToLowerInvariant())) continue;

                cache[category.Name].Add(new AssetEntry
                {
                    Name = file.Name,
                    Folder = folder,
                    Info = DescribeSize(file.Length),
                    FullPath = file.FullName,
                });
            }

            foreach (DirectoryInfo child in children)
            {
                if (SkipDirectories.Contains(child.Name)) continue;
                if (child.Name.StartsWith(".")) continue;

                string childRelative = (relative.Length == 0) ? child.Name : relative + "/" + child.Name;
                Walk(child, childRelative, category, depth + 1);
            }
        }

        /// <summary>
        /// Reads folder overrides out of project.godot, e.g. assets/models_folder="res://meshes".
        /// Absent keys leave the conventional folder in place.
        /// </summary>
        private void LoadFolderSettings()
        {
            string settings = Path.Combine(root, "project.godot");
            if (!File.Exists(settings)) return;

            string[] lines;
            try { lines = File.ReadAllLines(settings); }
            catch (IOException) { return; }

            foreach (string line in lines)
            {
                string trimmed = line.Trim();
                int equals = trimmed.IndexOf('=');
                if (equals <= 0 || trimmed.StartsWith(";")) continue;

                string key = trimmed.Substring(0, equals).Trim();
                string value = trimmed.Substring(equals + 1).Trim().Trim('"');

                foreach (Category category in categories)
                {
                    if (key != category.SettingKey) continue;
                    if (value.StartsWith("res://")) value = value.Substring("res://".Length);
                    if (value.Length > 0) category.Folder = value.TrimEnd('/');
                }
            }
        }

        private static string DescribeSize(long bytes)
        {
            if (bytes < 1024) return bytes + " B";
            if (bytes < 1024 * 1024) return (bytes / 1024.0).ToString("0.#") + " KB";
            return (bytes / (1024.0 * 1024.0)).ToString("0.#") + " MB";
        }

        private void RefreshCounts()
        {
            foreach (Category category in categories)
            {
                category.Button.Text = category.Name + " (" + cache[category.Name].Count.ToString("N0") + ")";
            }
        }

        private void SelectCategory(Category category)
        {
            active = category;
            folderFilter = null;
            RebuildColumns();
            BuildFolderTree();
            RefreshGrid();
            UpdatePathLabel();
        }

        private void UpdatePathLabel()
        {
            string folder = FolderOf(active);
            pathLabel.Text = Directory.Exists(folder)
                ? folder
                : folder + "   (missing -- set " + active.SettingKey + " in project.godot to point elsewhere)";
        }

        private void OnFolderSelected(object sender, TreeViewEventArgs e)
        {
            string relative = e.Node.Tag as string;
            folderFilter = string.IsNullOrEmpty(relative) ? null : relative;
            RefreshGrid();
        }

        private void OnCellValueNeeded(object sender, DataGridViewCellValueEventArgs e)
        {
            if (e.RowIndex < 0 || e.RowIndex >= rows.Count) return;

            AssetEntry entry = rows[e.RowIndex];
            switch (e.ColumnIndex)
            {
                case 0: e.Value = entry.Name; break;
                case 1: e.Value = entry.Folder; break;
                case 2: e.Value = entry.Info; break;
                default: e.Value = grid.Columns[e.ColumnIndex].Tag; break;
            }
        }

        private void RefreshGrid()
        {
            rows.Clear();

            foreach (AssetEntry entry in cache[active.Name])
            {
                if (folderFilter != null)
                {
                    // Show the selected folder and everything under it.
                    if (entry.Folder != folderFilter &&
                        !entry.Folder.StartsWith(folderFilter + "/", StringComparison.OrdinalIgnoreCase))
                    {
                        continue;
                    }
                }
                rows.Add(entry);
            }

            // Virtual mode: hand over the count and let CellValueNeeded fill what is on screen.
            grid.RowCount = 0;
            grid.RowCount = rows.Count;
            grid.Invalidate();
        }

        // -------------------------------------------------------------------------------
        // Actions
        // -------------------------------------------------------------------------------

        private void OnCellClick(object sender, DataGridViewCellEventArgs e)
        {
            if (e.RowIndex < 0 || e.RowIndex >= rows.Count) return;

            AssetEntry entry = rows[e.RowIndex];
            string action = grid.Columns[e.ColumnIndex].Tag as string;
            if (action == null) return;

            switch (action)
            {
                case "Delete":
                    DeleteEntry(entry);
                    break;

                case "Property":
                    ShowProperties(entry);
                    break;

                case "Open":
                    RaiseScene(SceneOpenRequested, entry, "Open");
                    break;

                case "Play":
                    RaiseScene(ScenePlayRequested, entry, "Play");
                    break;

                case "Quick Play":
                    QuickPlay(entry);
                    break;

                default:
                    // Place, Inspect and Reimport all act on engine-side resources.
                    EventHandler<string> handler = BridgeActionRequested;
                    if (handler != null) handler(this, action + " " + entry.Name);
                    Report(action + " '" + entry.Name + "' -- not wired yet: needs the engine data bridge.");
                    break;
            }
        }

        private void QuickPlay(AssetEntry entry)
        {
            if (entry.FullPath == null) return;

            string problem = AudioPreview.Play(entry.FullPath);
            if (problem != null)
            {
                Report("Quick Play - " + entry.Name + ": " + problem);
                return;
            }

            TimeSpan? duration = AudioPreview.Duration;
            Report(duration.HasValue
                ? "Playing " + entry.Name + " (" + duration.Value.ToString(@"m\:ss") + ")"
                : "Playing " + entry.Name);
        }

        private void RaiseScene(EventHandler<string> handler, AssetEntry entry, string verb)
        {
            if (entry.FullPath == null) return;

            string res = ToResPath(entry.FullPath);
            if (res == null)
            {
                Report("Cannot " + verb.ToLowerInvariant() + " a scene outside the project: " + entry.Name);
                return;
            }

            if (handler != null) handler(this, res);
            Report(verb + " " + res);
        }

        private string ToResPath(string absolutePath)
        {
            string prefix = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            string full = Path.GetFullPath(absolutePath);
            if (!full.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) return null;
            return "res://" + full.Substring(prefix.Length).Replace(Path.DirectorySeparatorChar, '/');
        }

        /// <summary>
        /// File-level facts we can read off disk. Engine-side resource properties (import flags,
        /// material slots, mesh surfaces) arrive with the bridge.
        /// </summary>
        private void ShowProperties(AssetEntry entry)
        {
            try
            {
                FileInfo info = new FileInfo(entry.FullPath);
                string text =
                    "Name:      " + info.Name + "\n" +
                    "Type:      " + info.Extension.TrimStart('.').ToUpperInvariant() + "\n" +
                    "Size:      " + DescribeSize(info.Length) + " (" + info.Length.ToString("N0") + " bytes)\n" +
                    "Folder:    " + entry.Folder + "\n" +
                    "res://     " + (ToResPath(entry.FullPath) ?? "(outside project)") + "\n" +
                    "Modified:  " + info.LastWriteTime + "\n" +
                    "Created:   " + info.CreationTime + "\n" +
                    "Read-only: " + info.IsReadOnly + "\n\n" +
                    "Path:\n" + info.FullName;

                MessageBox.Show(this, text, "Properties - " + entry.Name,
                    MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            catch (IOException ex)
            {
                MessageBox.Show(this, "Could not read properties:\n" + ex.Message,
                    "Properties", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void DeleteEntry(AssetEntry entry)
        {
            DialogResult answer = MessageBox.Show(this,
                "Send this file to the Recycle Bin?\n\n" + entry.FullPath,
                "Delete asset", MessageBoxButtons.YesNo, MessageBoxIcon.Warning,
                MessageBoxDefaultButton.Button2);

            if (answer != DialogResult.Yes) return;

            try
            {
                // Recycle rather than erase, so a misclick is recoverable.
                Microsoft.VisualBasic.FileIO.FileSystem.DeleteFile(
                    entry.FullPath,
                    Microsoft.VisualBasic.FileIO.UIOption.OnlyErrorDialogs,
                    Microsoft.VisualBasic.FileIO.RecycleOption.SendToRecycleBin);

                Report("Deleted " + entry.Name + " (sent to Recycle Bin).");
                RefreshCounts();
                RefreshGrid();
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, "Could not delete:\n" + ex.Message, "Delete asset",
                    MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void OnImport(object sender, EventArgs e)
        {
            if (!Directory.Exists(root))
            {
                Report("Project folder not found: " + root);
                return;
            }

            using (OpenFileDialog dialog = new OpenFileDialog())
            {
                dialog.Title = "Import assets";
                dialog.Multiselect = true;
                dialog.Filter = BuildImportFilter();

                if (dialog.ShowDialog(this) != DialogResult.OK) return;

                string target = TargetFolder();
                try { Directory.CreateDirectory(target); }
                catch (Exception ex)
                {
                    MessageBox.Show(this, "Could not create:\n" + target + "\n\n" + ex.Message,
                        "Import", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return;
                }

                int copied = 0;
                foreach (string source in dialog.FileNames)
                {
                    string destination = Path.Combine(target, Path.GetFileName(source));
                    if (File.Exists(destination))
                    {
                        DialogResult answer = MessageBox.Show(this,
                            "Overwrite?\n\n" + destination, "Import",
                            MessageBoxButtons.YesNo, MessageBoxIcon.Question,
                            MessageBoxDefaultButton.Button2);
                        if (answer != DialogResult.Yes) continue;
                    }

                    try { File.Copy(source, destination, true); copied++; }
                    catch (Exception ex)
                    {
                        MessageBox.Show(this, "Could not copy " + Path.GetFileName(source) + ":\n" + ex.Message,
                            "Import", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    }
                }

                Report("Imported " + copied + " file(s) into " + target);
                Rescan();
            }
        }

        private string BuildImportFilter()
        {
            string all = string.Join(";", categories.SelectMany(c => c.Extensions).Select(x => "*" + x).ToArray());
            string parts = string.Join("|", categories
                .Select(c => c.Name + " (" + string.Join(";", c.Extensions.Select(x => "*" + x).ToArray()) + ")|"
                           + string.Join(";", c.Extensions.Select(x => "*" + x).ToArray()))
                .ToArray());
            return "All assets (" + all + ")|" + all + "|" + parts + "|All files (*.*)|*.*";
        }

        /// <summary>
        /// Import and Create target the subfolder box when it has a value, otherwise the folder
        /// selected in the tree, otherwise the project root.
        /// </summary>
        private string TargetFolder()
        {
            // Everything is relative to the active category's folder, so importing a model lands
            // in models/ and never at the project root.
            string categoryFolder = FolderOf(active);

            string subfolder = ValueOf(subfolderBox);
            if (subfolder.Length > 0)
            {
                return Path.Combine(categoryFolder, subfolder.Replace('/', Path.DirectorySeparatorChar));
            }
            if (folderFilter != null)
            {
                return Path.Combine(categoryFolder, folderFilter.Replace('/', Path.DirectorySeparatorChar));
            }
            return categoryFolder;
        }

        private void OnCreateFolder(object sender, EventArgs e)
        {
            string name = ValueOf(newFolderBox);
            if (name.Length == 0)
            {
                Report("Type a folder name first.");
                return;
            }

            if (name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
            {
                MessageBox.Show(this, "That name contains characters a folder cannot have.",
                    "Create folder", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            string target = Path.Combine(TargetFolder(), name);
            try
            {
                if (Directory.Exists(target))
                {
                    Report("Folder already exists: " + target);
                    return;
                }
                Directory.CreateDirectory(target);
                Report("Created " + target);
                newFolderBox.Text = (string)newFolderBox.Tag;
                newFolderBox.ForeColor = SystemColors.GrayText;
                Rescan();
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, "Could not create:\n" + target + "\n\n" + ex.Message,
                    "Create folder", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void Report(string message)
        {
            EventHandler<string> handler = StatusChanged;
            if (handler != null) handler(this, message);
        }
    }

    /// <summary>Hosts the asset browser in its own window.</summary>
    internal sealed class AssetBrowserForm : Form
    {
        public AssetBrowser Browser { get; private set; }

        public AssetBrowserForm(string projectRoot)
        {
            Text = "Assets";
            Icon = Program.AppIcon;
            ClientSize = new Size(1180, 420);
            StartPosition = FormStartPosition.CenterParent;
            MinimizeBox = false;

            Browser = new AssetBrowser(projectRoot);
            Browser.Dock = DockStyle.Fill;
            Controls.Add(Browser);
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            // Do not leave a Quick Play clip running after the window is gone.
            AudioPreview.Stop();
            base.OnFormClosed(e);
        }
    }
}
