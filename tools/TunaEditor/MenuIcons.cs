using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;

namespace TunaEditor
{
    /// <summary>
    /// Menu glyphs drawn in code at 16x16, in the Windows 7 / Aero idiom: saturated gradient
    /// fills, a gloss highlight across the upper half, rounded corners and a darker outline for
    /// definition at small sizes.
    ///
    /// The Visual Studio designer's "standard items" bitmaps live in the designer's own
    /// resources and are not reachable from hand-written code, so these are ours -- no image
    /// files to ship and nothing to look up at runtime.
    ///
    /// Each is drawn once and cached; reopening a menu must not repaint them.
    /// </summary>
    internal static class MenuIcons
    {
        private const int Size = 16;
        private static readonly Dictionary<string, Image> cache = new Dictionary<string, Image>();

        // Aero palettes: a lit top, a deeper bottom, and an outline darker than both.
        private static readonly Color FolderTop = Color.FromArgb(255, 214, 122);
        private static readonly Color FolderBottom = Color.FromArgb(232, 160, 43);
        private static readonly Color FolderEdge = Color.FromArgb(138, 95, 23);

        private static readonly Color DiskTop = Color.FromArgb(125, 186, 240);
        private static readonly Color DiskBottom = Color.FromArgb(47, 111, 181);
        private static readonly Color DiskEdge = Color.FromArgb(30, 74, 122);

        private static readonly Color PaperTop = Color.FromArgb(255, 255, 255);
        private static readonly Color PaperBottom = Color.FromArgb(226, 236, 247);
        private static readonly Color PaperEdge = Color.FromArgb(122, 142, 165);

        private static readonly Color SteelTop = Color.FromArgb(226, 232, 240);
        private static readonly Color SteelBottom = Color.FromArgb(146, 158, 174);
        private static readonly Color SteelEdge = Color.FromArgb(90, 100, 114);

        private static readonly Color BoardTop = Color.FromArgb(222, 192, 134);
        private static readonly Color BoardBottom = Color.FromArgb(176, 136, 70);
        private static readonly Color BoardEdge = Color.FromArgb(108, 80, 38);

        public static Image NewWorld { get { return Get("new", DrawNew); } }
        public static Image OpenWorld { get { return Get("open", DrawFolder); } }
        public static Image SaveWorld { get { return Get("save", DrawDisk); } }
        public static Image WorldProperty { get { return Get("props", DrawProperties); } }
        public static Image Cut { get { return Get("cut", DrawCut); } }
        public static Image Copy { get { return Get("copy", DrawCopy); } }
        public static Image Paste { get { return Get("paste", DrawPaste); } }

        private delegate void Painter(Graphics g);

        private static Image Get(string key, Painter painter)
        {
            Image existing;
            if (cache.TryGetValue(key, out existing))
            {
                return existing;
            }

            Bitmap bitmap = new Bitmap(Size, Size);
            using (Graphics g = Graphics.FromImage(bitmap))
            {
                g.SmoothingMode = SmoothingMode.AntiAlias;
                g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                g.Clear(Color.Transparent);
                painter(g);
            }

            cache[key] = bitmap;
            return bitmap;
        }

        // -------------------------------------------------------------------------------
        // Aero building blocks
        // -------------------------------------------------------------------------------

        private static GraphicsPath RoundedRect(RectangleF r, float radius)
        {
            GraphicsPath path = new GraphicsPath();
            float d = radius * 2f;
            path.AddArc(r.X, r.Y, d, d, 180, 90);
            path.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            path.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
            path.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            path.CloseFigure();
            return path;
        }

        /// <summary>
        /// Vertical gradient body, a gloss pass over the top half, then the outline. Doing the
        /// gloss before the outline keeps the edge crisp instead of washing it out.
        /// </summary>
        private static void PaintAero(Graphics g, GraphicsPath shape, RectangleF bounds,
            Color top, Color bottom, Color edge)
        {
            using (LinearGradientBrush body = new LinearGradientBrush(
                    new RectangleF(bounds.X, bounds.Y - 0.5f, bounds.Width, bounds.Height + 1f),
                    top, bottom, LinearGradientMode.Vertical))
            {
                g.FillPath(body, shape);
            }

            AddGloss(g, shape, bounds);

            using (Pen pen = new Pen(edge))
            {
                g.DrawPath(pen, shape);
            }
        }

        /// <summary>The Aero highlight: a bright sheen fading out by the middle of the shape.</summary>
        private static void AddGloss(Graphics g, GraphicsPath shape, RectangleF bounds)
        {
            RectangleF half = new RectangleF(bounds.X, bounds.Y, bounds.Width, bounds.Height * 0.5f);
            if (half.Height < 1f)
            {
                return;
            }

            Region previous = g.Clip;
            g.SetClip(shape, CombineMode.Intersect);

            using (LinearGradientBrush gloss = new LinearGradientBrush(
                    new RectangleF(half.X, half.Y - 0.5f, half.Width, half.Height + 1f),
                    Color.FromArgb(150, 255, 255, 255),
                    Color.FromArgb(20, 255, 255, 255),
                    LinearGradientMode.Vertical))
            {
                g.FillRectangle(gloss, half);
            }

            g.Clip = previous;
        }

        /// <summary>A sheet with a folded corner, gradient-filled and glossed.</summary>
        private static void PaintPage(Graphics g, float x, float y, float w, float h)
        {
            const float fold = 4.5f;
            RectangleF bounds = new RectangleF(x, y, w, h);

            using (GraphicsPath path = new GraphicsPath())
            {
                path.AddLines(new PointF[]
                {
                    new PointF(x, y),
                    new PointF(x + w - fold, y),
                    new PointF(x + w, y + fold),
                    new PointF(x + w, y + h),
                    new PointF(x, y + h)
                });
                path.CloseFigure();

                PaintAero(g, path, bounds, PaperTop, PaperBottom, PaperEdge);
            }

            // The turned corner, shaded so it reads as folded rather than cut off.
            using (GraphicsPath corner = new GraphicsPath())
            {
                corner.AddLines(new PointF[]
                {
                    new PointF(x + w - fold, y),
                    new PointF(x + w, y + fold),
                    new PointF(x + w - fold, y + fold)
                });
                corner.CloseFigure();

                using (Brush shade = new SolidBrush(Color.FromArgb(206, 219, 234)))
                {
                    g.FillPath(shade, corner);
                }
                using (Pen pen = new Pen(PaperEdge))
                {
                    g.DrawPath(pen, corner);
                }
            }
        }

        // -------------------------------------------------------------------------------
        // Glyphs
        // -------------------------------------------------------------------------------

        private static void DrawNew(Graphics g)
        {
            PaintPage(g, 3.5f, 1.5f, 9f, 13f);
        }

        private static void DrawFolder(Graphics g)
        {
            // Back panel.
            RectangleF backBounds = new RectangleF(1f, 3.5f, 12f, 9.5f);
            using (GraphicsPath back = new GraphicsPath())
            {
                back.AddLines(new PointF[]
                {
                    new PointF(1f, 13f), new PointF(1f, 4.5f),
                    new PointF(5.5f, 4.5f), new PointF(7f, 6f),
                    new PointF(13f, 6f), new PointF(13f, 13f)
                });
                back.CloseFigure();
                PaintAero(g, back, backBounds, FolderTop, FolderBottom, FolderEdge);
            }

            // Front flap, lighter and tilted, which is what makes an Aero folder read as open.
            RectangleF frontBounds = new RectangleF(1f, 7f, 14f, 6f);
            using (GraphicsPath front = new GraphicsPath())
            {
                front.AddLines(new PointF[]
                {
                    new PointF(1f, 13f), new PointF(3.5f, 7.5f),
                    new PointF(15f, 7.5f), new PointF(12.5f, 13f)
                });
                front.CloseFigure();
                PaintAero(g, front, frontBounds,
                    Color.FromArgb(255, 226, 152), Color.FromArgb(240, 178, 62), FolderEdge);
            }
        }

        private static void DrawDisk(Graphics g)
        {
            RectangleF bounds = new RectangleF(1.5f, 1.5f, 13f, 13f);
            using (GraphicsPath body = RoundedRect(bounds, 1.5f))
            {
                PaintAero(g, body, bounds, DiskTop, DiskBottom, DiskEdge);
            }

            // Shutter.
            RectangleF shutter = new RectangleF(5f, 2.5f, 6f, 4.5f);
            using (GraphicsPath path = RoundedRect(shutter, 0.5f))
            {
                PaintAero(g, path, shutter, SteelTop, SteelBottom, DiskEdge);
            }

            // Label.
            RectangleF label = new RectangleF(4f, 9f, 8f, 5.5f);
            using (GraphicsPath path = RoundedRect(label, 0.5f))
            {
                PaintAero(g, path, label, PaperTop, PaperBottom, DiskEdge);
            }
        }

        private static void DrawProperties(Graphics g)
        {
            PaintPage(g, 1f, 1f, 9f, 12f);

            // Glass magnifier: gradient lens, bright rim, steel handle.
            RectangleF lens = new RectangleF(7.5f, 6.5f, 6f, 6f);
            using (GraphicsPath glass = new GraphicsPath())
            {
                glass.AddEllipse(lens);
                PaintAero(g, glass, lens,
                    Color.FromArgb(220, 240, 252), Color.FromArgb(120, 170, 215), SteelEdge);
            }
            using (Pen rim = new Pen(SteelEdge, 1.3f))
            {
                g.DrawEllipse(rim, lens);
            }
            using (Pen handle = new Pen(SteelBottom, 2.2f))
            {
                g.DrawLine(handle, 12.6f, 11.6f, 15f, 14.2f);
            }
        }

        private static void DrawCut(Graphics g)
        {
            using (Pen blade = new Pen(SteelBottom, 1.8f))
            {
                blade.StartCap = LineCap.Round;
                blade.EndCap = LineCap.Round;
                g.DrawLine(blade, 4f, 2f, 10.5f, 10.5f);
                g.DrawLine(blade, 12f, 2f, 5.5f, 10.5f);
            }
            using (Pen shine = new Pen(Color.FromArgb(160, 255, 255, 255), 0.8f))
            {
                g.DrawLine(shine, 4.2f, 2.4f, 10f, 10f);
            }

            // Handles, glossed like the rest.
            foreach (RectangleF ring in new RectangleF[]
            {
                new RectangleF(2.5f, 10.5f, 4.5f, 4.5f),
                new RectangleF(9f, 10.5f, 4.5f, 4.5f)
            })
            {
                using (GraphicsPath path = new GraphicsPath())
                {
                    path.AddEllipse(ring);
                    using (Pen pen = new Pen(SteelEdge, 1.4f))
                    {
                        g.DrawPath(pen, path);
                    }
                }
            }
        }

        private static void DrawCopy(Graphics g)
        {
            PaintPage(g, 1f, 1f, 8f, 10f);
            PaintPage(g, 6f, 5f, 9f, 10f);
        }

        private static void DrawPaste(Graphics g)
        {
            RectangleF bounds = new RectangleF(1f, 2.5f, 12f, 12.5f);
            using (GraphicsPath board = RoundedRect(bounds, 1.5f))
            {
                PaintAero(g, board, bounds, BoardTop, BoardBottom, BoardEdge);
            }

            RectangleF clip = new RectangleF(4.5f, 0.8f, 5f, 3.4f);
            using (GraphicsPath path = RoundedRect(clip, 1f))
            {
                PaintAero(g, path, clip, SteelTop, SteelBottom, SteelEdge);
            }

            // The sheet being pasted on.
            PaintPage(g, 6f, 6f, 9f, 9f);
        }
    }
}
