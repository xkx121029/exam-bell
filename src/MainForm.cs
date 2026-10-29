using System;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Media;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using Microsoft.Win32;

namespace ExamBell
{
    internal enum BellState
    {
        Pending,
        Playing,
        Done,
        Missed,
        Cancelled
    }

    internal sealed class BellNode
    {
        public string Title;
        public string Detail;
        public DateTime Scheduled;
        public BellState State = BellState.Pending;
    }

    internal sealed class MainForm : Form
    {
        private static readonly Color ColorBackground = Color.FromArgb(245, 246, 248);
        private static readonly Color ColorSurface = Color.FromArgb(255, 255, 255);
        private static readonly Color ColorInk = Color.FromArgb(24, 27, 31);
        private static readonly Color ColorMuted = Color.FromArgb(108, 116, 128);
        private static readonly Color ColorLine = Color.FromArgb(224, 227, 233);
        private static readonly Color ColorStart = Color.FromArgb(22, 105, 74);
        private static readonly Color ColorStop = Color.FromArgb(196, 30, 34);
        private static readonly Color ColorOk = Color.FromArgb(30, 107, 79);
        private static readonly Color ColorBad = Color.FromArgb(179, 38, 30);

        private static readonly string[] NodeTitles = new string[]
        {
            "考前 10 分钟 · 考生进入考场",
            "考前 5 分钟 · 下发试卷与答题卡",
            "考试开始 · 考生开始作答"
        };

        private static readonly string[] AudioCaptions = new string[]
        {
            "考前 10 分钟",
            "考前 5 分钟",
            "考试开始"
        };

        private readonly BeijingClock _clock = new BeijingClock();
        private readonly AudioPlayer _player = new AudioPlayer();
        private readonly BellNode[] _nodes = new BellNode[3];
        private readonly string[] _audioFiles = new string[3];
        private readonly ToolTip _toolTip = new ToolTip();

        private Label _labelClock;
        private Label _labelDate;
        private Label _labelSync;
        private Label _labelCountdown;
        private TextBox _textStart;
        private Label[] _labelNodeTime;
        private Label[] _labelNodeState;
        private Label[] _labelAudio;
        private TextBox _textLog;
        private Button _buttonStart;
        private Button _buttonStop;
        private Button _buttonSync;
        private Timer _ticker;

        private int _playingIndex = -1;
        private bool _running;
        private bool _stoppedByUser;
        private bool _syncBusy;
        private DateTime _examStart;

        public MainForm()
        {
            _labelNodeTime = new Label[3];
            _labelNodeState = new Label[3];
            _labelAudio = new Label[3];

            BuildUi();
            LoadSettings();
            UpdateAllNodeRows();
            UpdateAudioLabels();

            _ticker = new Timer();
            _ticker.Interval = 200;
            _ticker.Tick += OnTick;
            _ticker.Start();

            SystemEvents.PowerModeChanged += OnPowerModeChanged;
        }

        #region 界面构建

        private static Font UiFont(float size, FontStyle style)
        {
            return new Font("Microsoft YaHei", size, style, GraphicsUnit.Point);
        }

        private static Label MakeLabel(string text, float size, FontStyle style, Color color)
        {
            Label label = new Label();
            label.Text = text;
            label.Font = UiFont(size, style);
            label.ForeColor = color;
            label.BackColor = Color.Transparent;
            label.AutoSize = true;
            return label;
        }

        private Button MakeToolButton(string text)
        {
            Button button = new Button();
            button.Text = text;
            button.Font = UiFont(8.5f, FontStyle.Regular);
            button.FlatStyle = FlatStyle.Flat;
            button.FlatAppearance.BorderSize = 1;
            button.FlatAppearance.BorderColor = ColorLine;
            button.FlatAppearance.MouseOverBackColor = Color.FromArgb(242, 244, 247);
            button.FlatAppearance.MouseDownBackColor = Color.FromArgb(230, 234, 239);
            button.BackColor = ColorSurface;
            button.ForeColor = ColorInk;
            button.UseVisualStyleBackColor = false;
            button.Cursor = Cursors.Hand;
            return button;
        }

        private static void ApplyFlatButton(Button button, Color normal, Color hover, Color pressed)
        {
            button.FlatStyle = FlatStyle.Flat;
            button.FlatAppearance.BorderSize = 0;
            button.FlatAppearance.MouseOverBackColor = hover;
            button.FlatAppearance.MouseDownBackColor = pressed;
            button.BackColor = normal;
            button.UseVisualStyleBackColor = false;
            button.ForeColor = Color.White;
            button.Cursor = Cursors.Hand;
        }

        private static void AddCell(TableLayoutPanel grid, Control control, int column, int row)
        {
            control.Dock = DockStyle.Fill;
            control.Margin = new Padding(0);
            grid.Controls.Add(control, column, row);
        }

        private void BuildUi()
        {
            SuspendLayout();

            Text = "考试自动打铃";
            ClientSize = new Size(980, 700);
            FormBorderStyle = FormBorderStyle.FixedSingle;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = ColorBackground;
            ForeColor = ColorInk;
            Font = UiFont(9f, FontStyle.Regular);
            AutoScaleMode = AutoScaleMode.Dpi;
            AutoScaleDimensions = new SizeF(96f, 96f);
            KeyPreview = true;
            KeyDown += OnFormKeyDown;

            TableLayoutPanel root = new TableLayoutPanel();
            root.Dock = DockStyle.Fill;
            root.ColumnCount = 1;
            root.RowCount = 3;
            root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 92f));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 140f));
            root.Margin = new Padding(0);
            root.Padding = new Padding(0);
            root.BackColor = ColorBackground;
            Controls.Add(root);

            root.Controls.Add(BuildHeader(), 0, 0);
            root.Controls.Add(BuildBody(), 0, 1);
            root.Controls.Add(BuildFooter(), 0, 2);

            ResumeLayout();
        }

        private Control BuildHeader()
        {
            Panel header = new Panel();
            header.Dock = DockStyle.Fill;
            header.BackColor = ColorSurface;
            header.Margin = new Padding(0);
            header.Paint += delegate(object sender, PaintEventArgs e)
            {
                using (Pen pen = new Pen(ColorLine))
                {
                    e.Graphics.DrawLine(pen, 0, header.Height - 1, header.Width, header.Height - 1);
                }
            };

            TableLayoutPanel grid = new TableLayoutPanel();
            grid.Dock = DockStyle.Fill;
            grid.ColumnCount = 3;
            grid.RowCount = 1;
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 258f));
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 334f));
            grid.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
            grid.Margin = new Padding(0);
            grid.Padding = new Padding(0);
            grid.BackColor = ColorSurface;
            header.Controls.Add(grid);

            Panel titlePanel = new Panel();
            titlePanel.Dock = DockStyle.Fill;
            titlePanel.BackColor = ColorSurface;
            titlePanel.Margin = new Padding(0);
            grid.Controls.Add(titlePanel, 0, 0);

            TableLayoutPanel clockPanel = new TableLayoutPanel();
            clockPanel.Dock = DockStyle.Fill;
            clockPanel.ColumnCount = 1;
            clockPanel.RowCount = 3;
            clockPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            clockPanel.RowStyles.Add(new RowStyle(SizeType.Absolute, 38f));
            clockPanel.RowStyles.Add(new RowStyle(SizeType.Absolute, 18f));
            clockPanel.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
            clockPanel.Padding = new Padding(0, 4, 24, 4);
            clockPanel.Margin = new Padding(0);
            clockPanel.BackColor = ColorSurface;
            grid.Controls.Add(clockPanel, 2, 0);

            _labelClock = new Label();
            _labelClock.Dock = DockStyle.Fill;
            _labelClock.Margin = new Padding(0);
            _labelClock.TextAlign = ContentAlignment.MiddleRight;
            _labelClock.Font = UiFont(21f, FontStyle.Bold);
            _labelClock.ForeColor = ColorInk;
            _labelClock.Text = "--:--:--";
            clockPanel.Controls.Add(_labelClock, 0, 0);

            _labelDate = new Label();
            _labelDate.Dock = DockStyle.Fill;
            _labelDate.Margin = new Padding(0);
            _labelDate.TextAlign = ContentAlignment.MiddleRight;
            _labelDate.Font = UiFont(8.5f, FontStyle.Regular);
            _labelDate.ForeColor = ColorMuted;
            _labelDate.Text = "北京时间";
            clockPanel.Controls.Add(_labelDate, 0, 1);

            TableLayoutPanel syncPanel = new TableLayoutPanel();
            syncPanel.Dock = DockStyle.Fill;
            syncPanel.ColumnCount = 2;
            syncPanel.RowCount = 1;
            syncPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            syncPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 72f));
            syncPanel.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
            syncPanel.Margin = new Padding(0);
            syncPanel.Padding = new Padding(0);
            clockPanel.Controls.Add(syncPanel, 0, 2);

            _labelSync = new Label();
            _labelSync.Dock = DockStyle.Fill;
            _labelSync.Margin = new Padding(0);
            _labelSync.TextAlign = ContentAlignment.MiddleRight;
            _labelSync.Font = UiFont(8.5f, FontStyle.Regular);
            _labelSync.ForeColor = ColorMuted;
            _labelSync.Text = "正在校时…";
            syncPanel.Controls.Add(_labelSync, 0, 0);

            _buttonSync = MakeToolButton("重新校时");
            _buttonSync.Dock = DockStyle.Fill;
            _buttonSync.Margin = new Padding(6, 2, 0, 2);
            _buttonSync.Click += async delegate { await SyncTimeAsync(true); };
            syncPanel.Controls.Add(_buttonSync, 1, 0);

            Label title = MakeLabel("考试自动打铃", 19f, FontStyle.Bold, ColorInk);
            title.Location = new Point(24, 12);
            titlePanel.Controls.Add(title);

            Label subtitle = MakeLabel("北京时间基准 · 三段音频自动播放", 8.5f, FontStyle.Regular, ColorMuted);
            subtitle.Location = new Point(26, 52);
            titlePanel.Controls.Add(subtitle);

            _labelCountdown = MakeLabel("待启动", 11.5f, FontStyle.Bold, ColorMuted);
            _labelCountdown.AutoSize = false;
            _labelCountdown.Dock = DockStyle.Fill;
            _labelCountdown.Margin = new Padding(0);
            _labelCountdown.Padding = new Padding(0, 0, 18, 0);
            _labelCountdown.TextAlign = ContentAlignment.MiddleCenter;
            grid.Controls.Add(_labelCountdown, 1, 0);

            return header;
        }

        private Control BuildBody()
        {
            TableLayoutPanel body = new TableLayoutPanel();
            body.Dock = DockStyle.Fill;
            body.ColumnCount = 2;
            body.RowCount = 1;
            body.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 61f));
            body.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 39f));
            body.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
            body.Padding = new Padding(20, 14, 20, 10);
            body.Margin = new Padding(0);
            body.BackColor = ColorBackground;

            body.Controls.Add(BuildLeftColumn(), 0, 0);
            body.Controls.Add(BuildLogPanel(), 1, 0);
            return body;
        }

        private Control BuildLeftColumn()
        {
            TableLayoutPanel left = new TableLayoutPanel();
            left.Dock = DockStyle.Fill;
            left.ColumnCount = 1;
            left.RowCount = 3;
            left.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            left.RowStyles.Add(new RowStyle(SizeType.Absolute, 114f));
            left.RowStyles.Add(new RowStyle(SizeType.Absolute, 170f));
            left.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
            left.Padding = new Padding(0);
            left.Margin = new Padding(0, 0, 12, 0);

            left.Controls.Add(BuildSettingsPanel(), 0, 0);
            left.Controls.Add(BuildNodePanel(), 0, 1);
            left.Controls.Add(BuildAudioPanel(), 0, 2);
            return left;
        }

        private Control BuildSettingsPanel()
        {
            GroupBox settings = new GroupBox();
            settings.Text = "考试设置";
            settings.Dock = DockStyle.Fill;
            settings.Margin = new Padding(0, 0, 0, 10);
            settings.BackColor = ColorBackground;
            settings.ForeColor = ColorInk;

            Label caption = MakeLabel("考试开始时间（北京时间）", 9f, FontStyle.Regular, ColorInk);
            caption.Location = new Point(14, 22);
            settings.Controls.Add(caption);

            _textStart = new TextBox();
            _textStart.Font = UiFont(11f, FontStyle.Regular);
            _textStart.BorderStyle = BorderStyle.FixedSingle;
            _textStart.AutoSize = false;
            _textStart.SetBounds(14, 44, 216, 28);
            _textStart.KeyDown += delegate(object sender, KeyEventArgs e)
            {
                if (e.KeyCode == Keys.Enter)
                {
                    e.SuppressKeyPress = true;
                    OnStartClicked(sender, e);
                }
            };
            settings.Controls.Add(_textStart);

            Button fill = MakeToolButton("填入当前 +30 分钟");
            fill.SetBounds(238, 44, 142, 28);
            fill.Click += delegate { FillStartFromClock(); };
            settings.Controls.Add(fill);

            Label hint = MakeLabel("格式 YYYY-MM-DD HH:MM:SS，例如 2026-10-30 09:00:00", 8.5f, FontStyle.Regular, ColorMuted);
            hint.Location = new Point(14, 76);
            settings.Controls.Add(hint);

            return settings;
        }

        private Control BuildNodePanel()
        {
            GroupBox group = new GroupBox();
            group.Text = "打铃节点";
            group.Dock = DockStyle.Fill;
            group.Margin = new Padding(0, 0, 0, 10);
            group.BackColor = ColorBackground;
            group.ForeColor = ColorInk;

            TableLayoutPanel grid = new TableLayoutPanel();
            grid.Dock = DockStyle.Fill;
            grid.ColumnCount = 3;
            grid.RowCount = 4;
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 44f));
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 30f));
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 26f));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 22f));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 36f));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 36f));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 36f));
            grid.Padding = new Padding(10, 2, 10, 2);
            grid.Margin = new Padding(0);
            group.Controls.Add(grid);

            AddCell(grid, MakeLabel("节点", 8.5f, FontStyle.Regular, ColorMuted), 0, 0);
            AddCell(grid, MakeLabel("预定时间", 8.5f, FontStyle.Regular, ColorMuted), 1, 0);
            AddCell(grid, MakeLabel("状态", 8.5f, FontStyle.Regular, ColorMuted), 2, 0);

            for (int i = 0; i < 3; i++)
            {
                Label name = MakeLabel(NodeTitles[i], 9f, FontStyle.Regular, ColorInk);
                name.TextAlign = ContentAlignment.MiddleLeft;
                AddCell(grid, name, 0, i + 1);

                _labelNodeTime[i] = MakeLabel("—", 9f, FontStyle.Regular, ColorInk);
                _labelNodeTime[i].TextAlign = ContentAlignment.MiddleLeft;
                AddCell(grid, _labelNodeTime[i], 1, i + 1);

                _labelNodeState[i] = MakeLabel("未排期", 8.5f, FontStyle.Regular, ColorMuted);
                _labelNodeState[i].AutoSize = true;
                _labelNodeState[i].Padding = new Padding(10, 3, 10, 3);
                _labelNodeState[i].TextAlign = ContentAlignment.MiddleCenter;
                _labelNodeState[i].Anchor = AnchorStyles.None;
                grid.Controls.Add(_labelNodeState[i], 2, i + 1);
            }

            return group;
        }

        private Control BuildAudioPanel()
        {
            GroupBox group = new GroupBox();
            group.Text = "音频文件";
            group.Dock = DockStyle.Fill;
            group.Margin = new Padding(0, 0, 0, 0);
            group.BackColor = ColorBackground;
            group.ForeColor = ColorInk;

            TableLayoutPanel list = new TableLayoutPanel();
            list.Dock = DockStyle.Fill;
            list.ColumnCount = 4;
            list.RowCount = 3;
            list.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 86f));
            list.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            list.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 60f));
            list.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 60f));
            list.RowStyles.Add(new RowStyle(SizeType.Percent, 33.33f));
            list.RowStyles.Add(new RowStyle(SizeType.Percent, 33.33f));
            list.RowStyles.Add(new RowStyle(SizeType.Percent, 33.34f));
            list.Padding = new Padding(10, 2, 10, 4);
            list.Margin = new Padding(0);
            group.Controls.Add(list);

            for (int i = 0; i < 3; i++)
            {
                int index = i;

                Label caption = MakeLabel(AudioCaptions[i], 9f, FontStyle.Regular, ColorInk);
                caption.TextAlign = ContentAlignment.MiddleLeft;
                AddCell(list, caption, 0, i);

                _labelAudio[i] = MakeLabel("未选择", 8.5f, FontStyle.Regular, ColorMuted);
                _labelAudio[i].AutoSize = false;
                _labelAudio[i].AutoEllipsis = true;
                _labelAudio[i].TextAlign = ContentAlignment.MiddleLeft;
                AddCell(list, _labelAudio[i], 1, i);

                Button choose = MakeToolButton("选择");
                choose.Dock = DockStyle.Fill;
                choose.Margin = new Padding(4, 7, 0, 7);
                choose.Click += delegate { ChooseAudio(index); };
                list.Controls.Add(choose, 2, i);

                Button preview = MakeToolButton("试听");
                preview.Dock = DockStyle.Fill;
                preview.Margin = new Padding(4, 7, 0, 7);
                preview.Click += delegate { PreviewAudio(index); };
                list.Controls.Add(preview, 3, i);
            }

            return group;
        }

        private Control BuildLogPanel()
        {
            GroupBox group = new GroupBox();
            group.Text = "运行日志";
            group.Dock = DockStyle.Fill;
            group.Margin = new Padding(0);
            group.BackColor = ColorBackground;
            group.ForeColor = ColorInk;

            _textLog = new TextBox();
            _textLog.Multiline = true;
            _textLog.ReadOnly = true;
            _textLog.ScrollBars = ScrollBars.Vertical;
            _textLog.BorderStyle = BorderStyle.FixedSingle;
            _textLog.BackColor = ColorSurface;
            _textLog.ForeColor = ColorInk;
            _textLog.Font = UiFont(8.5f, FontStyle.Regular);
            _textLog.Dock = DockStyle.Fill;
            _textLog.Margin = new Padding(0);
            group.Controls.Add(_textLog);

            return group;
        }

        private Control BuildFooter()
        {
            Panel footer = new Panel();
            footer.Dock = DockStyle.Fill;
            footer.BackColor = ColorSurface;
            footer.Margin = new Padding(0);
            footer.Paint += delegate(object sender, PaintEventArgs e)
            {
                using (Pen pen = new Pen(ColorLine))
                {
                    e.Graphics.DrawLine(pen, 0, 0, footer.Width, 0);
                }
            };

            TableLayoutPanel actions = new TableLayoutPanel();
            actions.Dock = DockStyle.Fill;
            actions.ColumnCount = 2;
            actions.RowCount = 1;
            actions.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 300f));
            actions.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            actions.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
            actions.Padding = new Padding(20, 12, 20, 16);
            actions.Margin = new Padding(0);
            footer.Controls.Add(actions);

            _buttonStart = new Button();
            _buttonStart.Text = "启 动 监 测";
            _buttonStart.Dock = DockStyle.Fill;
            _buttonStart.Margin = new Padding(0, 0, 14, 0);
            _buttonStart.Font = UiFont(15f, FontStyle.Bold);
            ApplyFlatButton(_buttonStart, ColorStart, Color.FromArgb(28, 122, 88), Color.FromArgb(18, 86, 60));
            _buttonStart.Click += OnStartClicked;
            actions.Controls.Add(_buttonStart, 0, 0);

            _buttonStop = new Button();
            _buttonStop.Text = "急  停";
            _buttonStop.Dock = DockStyle.Fill;
            _buttonStop.Margin = new Padding(0);
            _buttonStop.Font = UiFont(26f, FontStyle.Bold);
            ApplyFlatButton(_buttonStop, ColorStop, Color.FromArgb(218, 52, 56), Color.FromArgb(166, 20, 24));
            _buttonStop.Enabled = false;
            _buttonStop.Click += OnStopClicked;
            actions.Controls.Add(_buttonStop, 1, 0);

            return footer;
        }

        #endregion

        #region 生命周期

        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            FillDefaultStartIfEmpty();
            BeginInvoke((Action)(delegate { SyncSilently(); }));
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            if (_running && e.CloseReason == CloseReason.UserClosing)
            {
                DialogResult answer = MessageBox.Show(this,
                    "打铃任务正在运行，关闭程序将中止全部打铃。\r\n确定要关闭吗？",
                    "确认关闭", MessageBoxButtons.YesNo, MessageBoxIcon.Question, MessageBoxDefaultButton.Button2);
                if (answer != DialogResult.Yes)
                {
                    e.Cancel = true;
                    return;
                }
            }

            _ticker.Stop();
            _player.Close();
            SystemEvents.PowerModeChanged -= OnPowerModeChanged;
            SaveSettings();
            base.OnFormClosing(e);
        }

        private void OnPowerModeChanged(object sender, PowerModeChangedEventArgs e)
        {
            if (e.Mode != PowerModes.Resume) return;
            if (IsDisposed) return;
            BeginInvoke((Action)delegate
            {
                WriteLog("系统已从休眠恢复，重新校时并校正计时基准");
                SyncSilently();
            });
        }

        #endregion

        #region 时钟与校时

        private async void SyncSilently()
        {
            await SyncTimeAsync(false);
        }

        private async Task<bool> SyncTimeAsync(bool warnOnFailure)
        {
            if (_syncBusy) return _clock.Synced;
            _syncBusy = true;
            _buttonSync.Enabled = false;
            SetSyncLabel("正在校时…", ColorMuted, false);

            NetworkClock.Result result = await Task.Run(() => NetworkClock.Query(2));

            if (IsDisposed)
            {
                _syncBusy = false;
                return false;
            }

            _syncBusy = false;
            _buttonSync.Enabled = true;

            if (result.Ok)
            {
                _clock.ApplyNetworkTime(result.Beijing, result.OffsetMilliseconds);
                SetSyncLabel("已同步 · 偏差 " + Signed(result.OffsetMilliseconds) + " ms", ColorOk, false);
                WriteLog("校时成功：北京时间 " + result.Beijing.ToString("yyyy-MM-dd HH:mm:ss") + "，" + result.Message);
                FillDefaultStartIfEmpty();
                return true;
            }

            bool keptPrevious = _clock.Synced;
            if (keptPrevious)
            {
                _clock.Rebase();
            }
            else
            {
                _clock.UseLocalTime();
            }

            SetSyncLabel(keptPrevious ? "校时失败 · 沿用上次偏移" : "未同步 · 使用本机时间", ColorBad, true);
            WriteLog("校时失败：" + result.Message + (keptPrevious ? "，继续沿用上次校正结果" : "，已回退使用本机时间"));

            if (warnOnFailure)
            {
                WriteLog("【警告】时间基准不可靠：当前以本机系统时间为准，打铃时刻可能与北京时间存在偏差，请确认本机时间准确。");
            }

            FillDefaultStartIfEmpty();
            return false;
        }

        private void SetSyncLabel(string text, Color color, bool bold)
        {
            _labelSync.Text = text;
            _labelSync.ForeColor = color;
            _labelSync.Font = UiFont(8.5f, bold ? FontStyle.Bold : FontStyle.Regular);
        }

        private static string Signed(long value)
        {
            if (value > 0) return "+" + value.ToString(CultureInfo.InvariantCulture);
            return value.ToString(CultureInfo.InvariantCulture);
        }

        private void UpdateClockLabels(DateTime now)
        {
            string time = now.ToString("HH:mm:ss", CultureInfo.InvariantCulture);
            if (_labelClock.Text != time) _labelClock.Text = time;

            string date = now.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture) + "  星期" + "日一二三四五六"[(int)now.DayOfWeek];
            if (_labelDate.Text != date) _labelDate.Text = date;
        }

        #endregion

        #region 打铃调度

        private void OnTick(object sender, EventArgs e)
        {
            DateTime now = _clock.Now;
            UpdateClockLabels(now);

            if (_playingIndex >= 0)
            {
                if (!_player.IsPlaying)
                {
                    BellNode finished = _nodes[_playingIndex];
                    finished.State = BellState.Done;
                    UpdateNodeRow(_playingIndex);
                    WriteLog("播放结束：" + finished.Title);
                    _playingIndex = -1;
                }
            }

            if (!_running)
            {
                UpdateCountdown(now);
                return;
            }

            for (int i = 0; i < _nodes.Length; i++)
            {
                BellNode node = _nodes[i];
                if (node == null || node.State != BellState.Pending) continue;
                if (now < node.Scheduled) continue;

                TimeSpan late = now - node.Scheduled;
                if (late.TotalSeconds <= 60)
                {
                    TriggerBell(i);
                }
                else
                {
                    node.State = BellState.Missed;
                    UpdateNodeRow(i);
                    WriteLog("已错过：" + node.Title + "（预定 " + node.Scheduled.ToString("HH:mm:ss", CultureInfo.InvariantCulture) + "）");
                }
            }

            UpdateCountdown(now);
            CheckAllFinished(now);
        }

        private void TriggerBell(int index)
        {
            BellNode node = _nodes[index];
            node.State = BellState.Playing;
            UpdateNodeRow(index);

            string file = _audioFiles[index];
            WriteLog("打铃：" + node.Title);

            if (!string.IsNullOrEmpty(file) && File.Exists(file))
            {
                if (_player.Play(file))
                {
                    _playingIndex = index;
                    return;
                }
                WriteLog("音频播放失败：" + (_player.LastError ?? "未知原因"));
            }
            else if (!string.IsNullOrEmpty(file))
            {
                WriteLog("音频文件不存在：" + file);
            }
            else
            {
                WriteLog("未设置音频文件，改用系统提示音");
            }

            SystemSounds.Exclamation.Play();
            node.State = BellState.Done;
            UpdateNodeRow(index);
            _playingIndex = -1;
        }

        private void CheckAllFinished(DateTime now)
        {
            for (int i = 0; i < _nodes.Length; i++)
            {
                BellNode node = _nodes[i];
                if (node == null) continue;
                if (node.State == BellState.Pending || node.State == BellState.Playing) return;
            }

            if (!_running) return;

            _running = false;
            _buttonStop.Enabled = false;
            _buttonStart.Enabled = true;
            _buttonStart.Text = "启 动 监 测";
            _textStart.Enabled = true;
            WriteLog("本轮打铃任务已全部结束，可再次启动下一场");
            UpdateCountdown(now);
        }

        private void UpdateCountdown(DateTime now)
        {
            if (_running)
            {
                BellNode next = null;
                for (int i = 0; i < _nodes.Length; i++)
                {
                    BellNode node = _nodes[i];
                    if (node == null || node.State != BellState.Pending) continue;
                    if (next == null || node.Scheduled < next.Scheduled) next = node;
                }

                if (next == null)
                {
                    _labelCountdown.Text = "本轮任务已完成";
                }
                else
                {
                    TimeSpan remain = next.Scheduled - now;
                    if (remain < TimeSpan.Zero) remain = TimeSpan.Zero;
                    _labelCountdown.Text = "距下一铃 " + FormatRemain(remain);
                }
                _labelCountdown.ForeColor = ColorInk;
            }
            else if (_stoppedByUser)
            {
                _labelCountdown.Text = "已急停 · 后续任务已取消";
                _labelCountdown.ForeColor = ColorStop;
            }
            else
            {
                _labelCountdown.Text = "待启动";
                _labelCountdown.ForeColor = _clock.Synced ? ColorMuted : ColorBad;
            }
        }

        private static string FormatRemain(TimeSpan span)
        {
            if (span.TotalHours >= 1)
            {
                return ((int)span.TotalHours).ToString("00", CultureInfo.InvariantCulture)
                    + ":" + span.Minutes.ToString("00", CultureInfo.InvariantCulture)
                    + ":" + span.Seconds.ToString("00", CultureInfo.InvariantCulture);
            }
            return span.Minutes.ToString("00", CultureInfo.InvariantCulture) + ":" + span.Seconds.ToString("00", CultureInfo.InvariantCulture);
        }

        private void UpdateNodeRow(int index)
        {
            BellNode node = _nodes[index];

            if (node == null)
            {
                _labelNodeTime[index].Text = "—";
                _labelNodeState[index].Text = "未排期";
                ApplyStateStyle(_labelNodeState[index], BellState.Pending, false);
                return;
            }

            _labelNodeTime[index].Text = node.Scheduled.ToString("HH:mm:ss", CultureInfo.InvariantCulture);
            _labelNodeState[index].Text = StateText(node.State);
            ApplyStateStyle(_labelNodeState[index], node.State, true);
        }

        private void UpdateAllNodeRows()
        {
            for (int i = 0; i < 3; i++) UpdateNodeRow(i);
        }

        private static string StateText(BellState state)
        {
            switch (state)
            {
                case BellState.Pending: return "待触发";
                case BellState.Playing: return "播放中";
                case BellState.Done: return "已完成";
                case BellState.Missed: return "已错过";
                case BellState.Cancelled: return "已取消";
            }
            return "未知";
        }

        private static void ApplyStateStyle(Label label, BellState state, bool scheduled)
        {
            if (!scheduled)
            {
                label.Text = "未排期";
                label.BackColor = Color.FromArgb(237, 239, 242);
                label.ForeColor = ColorMuted;
                return;
            }

            switch (state)
            {
                case BellState.Pending:
                    label.BackColor = Color.FromArgb(237, 239, 242);
                    label.ForeColor = Color.FromArgb(91, 101, 114);
                    break;
                case BellState.Playing:
                    label.BackColor = Color.FromArgb(251, 227, 204);
                    label.ForeColor = Color.FromArgb(138, 75, 0);
                    break;
                case BellState.Done:
                    label.BackColor = Color.FromArgb(220, 235, 221);
                    label.ForeColor = Color.FromArgb(42, 107, 60);
                    break;
                case BellState.Missed:
                    label.BackColor = Color.FromArgb(246, 222, 222);
                    label.ForeColor = Color.FromArgb(163, 42, 38);
                    break;
                case BellState.Cancelled:
                    label.BackColor = Color.FromArgb(243, 228, 228);
                    label.ForeColor = Color.FromArgb(150, 68, 68);
                    break;
            }
        }

        #endregion

        #region 启动与急停

        private async void OnStartClicked(object sender, EventArgs e)
        {
            if (_running) return;

            DateTime start;
            if (!TryParseStart(out start))
            {
                MessageBox.Show(this, "请输入正确的考试开始时间，格式为 YYYY-MM-DD HH:MM:SS。",
                    "时间格式有误", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                _textStart.Focus();
                _textStart.SelectAll();
                return;
            }

            _buttonStart.Enabled = false;
            _buttonStart.Text = "启动中…";
            await SyncTimeAsync(true);

            if (IsDisposed) return;

            DateTime beijingNow = _clock.Now;
            if (start <= beijingNow)
            {
                MessageBox.Show(this,
                    "考试开始时间已经过去（当前北京时间 " + beijingNow.ToString("HH:mm:ss", CultureInfo.InvariantCulture) + "），请重新填写。",
                    "时间无效", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                _buttonStart.Enabled = true;
                _buttonStart.Text = "启 动 监 测";
                return;
            }

            StartMonitoring(start);
        }

        private void StartMonitoring(DateTime start)
        {
            _examStart = start;
            _stoppedByUser = false;
            _playingIndex = -1;
            _player.Close();

            DateTime now = _clock.Now;
            SetupNode(0, "考生进入考场", start.AddMinutes(-10), now);
            SetupNode(1, "下发试卷与答题卡", start.AddMinutes(-5), now);
            SetupNode(2, "考生开始作答", start, now);

            _running = true;
            _buttonStart.Enabled = false;
            _buttonStart.Text = "运 行 中";
            _buttonStop.Enabled = true;
            _textStart.Enabled = false;

            WriteLog("开始监测：考试开始时间 " + start.ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture) + "（北京时间）");
            UpdateAllNodeRows();
            UpdateCountdown(now);
            SaveSettings();
        }

        private void SetupNode(int index, string detail, DateTime scheduled, DateTime now)
        {
            BellNode node = new BellNode();
            node.Title = NodeTitles[index];
            node.Detail = detail;
            node.Scheduled = scheduled;

            // 落在容差窗口内（已过不足 60 秒）的节点保留为待触发，由监测循环立即补响；
            // 超出窗口才判定为错过，避免启动瞬间把刚过的节点直接判死。
            TimeSpan late = now - scheduled;
            node.State = (late.TotalSeconds > 60) ? BellState.Missed : BellState.Pending;
            _nodes[index] = node;

            if (node.State == BellState.Missed)
            {
                WriteLog("节点已过：" + node.Title + "（" + scheduled.ToString("HH:mm:ss", CultureInfo.InvariantCulture) + "）");
            }
        }

        private bool TryParseStart(out DateTime value)
        {
            return DateTime.TryParseExact(_textStart.Text.Trim(), "yyyy-MM-dd HH:mm:ss",
                CultureInfo.InvariantCulture, DateTimeStyles.None, out value);
        }

        private void FillDefaultStartIfEmpty()
        {
            if (_textStart.Text.Trim().Length > 0) return;
            FillStartFromClock();
        }

        private void FillStartFromClock()
        {
            DateTime value = _clock.Now.AddMinutes(30);
            value = new DateTime(value.Year, value.Month, value.Day, value.Hour, value.Minute, 0);
            _textStart.Text = value.ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture);
            _textStart.SelectionStart = _textStart.Text.Length;
        }

        private void OnStopClicked(object sender, EventArgs e)
        {
            PerformEmergencyStop();
        }

        private void PerformEmergencyStop()
        {
            if (!_running) return;

            _player.Stop();
            _player.Close();
            _playingIndex = -1;

            for (int i = 0; i < _nodes.Length; i++)
            {
                BellNode node = _nodes[i];
                if (node == null) continue;
                if (node.State == BellState.Pending || node.State == BellState.Playing)
                {
                    node.State = BellState.Cancelled;
                }
            }

            _running = false;
            _stoppedByUser = true;
            _buttonStop.Enabled = false;
            _buttonStart.Enabled = true;
            _buttonStart.Text = "启 动 监 测";
            _textStart.Enabled = true;

            WriteLog("【急停】已立即停止播放，并取消全部后续打铃任务");
            UpdateAllNodeRows();
            UpdateCountdown(_clock.Now);
        }

        private void OnFormKeyDown(object sender, KeyEventArgs e)
        {
            if (e.KeyCode == Keys.Escape && _running)
            {
                e.SuppressKeyPress = true;
                PerformEmergencyStop();
            }
        }

        #endregion

        #region 音频设置

        private void ChooseAudio(int index)
        {
            using (OpenFileDialog dialog = new OpenFileDialog())
            {
                dialog.Title = "选择第 " + (index + 1) + " 段音频";
                dialog.Filter = "音频文件 (*.wav;*.mp3;*.wma)|*.wav;*.mp3;*.wma|所有文件 (*.*)|*.*";
                dialog.CheckFileExists = true;
                if (!string.IsNullOrEmpty(_audioFiles[index]))
                {
                    dialog.FileName = _audioFiles[index];
                }

                if (dialog.ShowDialog(this) == DialogResult.OK)
                {
                    _audioFiles[index] = dialog.FileName;
                    UpdateAudioLabel(index);
                    SaveSettings();
                    WriteLog("已设置音频（" + AudioCaptions[index] + "）：" + Path.GetFileName(dialog.FileName));
                }
            }
        }

        private void PreviewAudio(int index)
        {
            string file = _audioFiles[index];
            if (string.IsNullOrEmpty(file))
            {
                MessageBox.Show(this, "请先为该段选择音频文件。", "未设置音频", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }
            if (!File.Exists(file))
            {
                MessageBox.Show(this, "音频文件不存在：\r\n" + file, "文件缺失", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            if (_playingIndex >= 0)
            {
                _nodes[_playingIndex].State = BellState.Done;
                UpdateNodeRow(_playingIndex);
                _playingIndex = -1;
            }

            if (!_player.Play(file))
            {
                MessageBox.Show(this, "音频播放失败：\r\n" + (_player.LastError ?? "未知原因") +
                    "\r\n\r\n请确认文件为 WAV / MP3 / WMA 格式且可正常播放。",
                    "播放失败", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            WriteLog("试听：" + Path.GetFileName(file));
        }

        private void UpdateAudioLabels()
        {
            for (int i = 0; i < 3; i++) UpdateAudioLabel(i);
        }

        private void UpdateAudioLabel(int index)
        {
            string file = _audioFiles[index];
            if (string.IsNullOrEmpty(file))
            {
                _labelAudio[index].Text = "未选择（点击“选择”添加）";
                _labelAudio[index].ForeColor = ColorMuted;
                _toolTip.SetToolTip(_labelAudio[index], string.Empty);
            }
            else
            {
                _labelAudio[index].Text = Path.GetFileName(file);
                _labelAudio[index].ForeColor = File.Exists(file) ? ColorInk : ColorBad;
                _toolTip.SetToolTip(_labelAudio[index], file);
            }
        }

        #endregion

        #region 配置

        private static string ConfigPath()
        {
            string dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "ExamBell");
            Directory.CreateDirectory(dir);
            return Path.Combine(dir, "config.ini");
        }

        private void LoadSettings()
        {
            try
            {
                string path = ConfigPath();
                if (File.Exists(path))
                {
                    string[] lines = File.ReadAllLines(path, Encoding.UTF8);
                    foreach (string raw in lines)
                    {
                        string line = raw.Trim();
                        if (line.Length == 0 || line.StartsWith("#")) continue;
                        int split = line.IndexOf('=');
                        if (split <= 0) continue;
                        string key = line.Substring(0, split).Trim().ToLowerInvariant();
                        string value = line.Substring(split + 1).Trim();

                        if (key == "start")
                        {
                            DateTime parsed;
                            if (DateTime.TryParseExact(value, "yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture, DateTimeStyles.None, out parsed))
                            {
                                _textStart.Text = value;
                            }
                        }
                        else if (key == "audio0" && value.Length > 0) _audioFiles[0] = value;
                        else if (key == "audio1" && value.Length > 0) _audioFiles[1] = value;
                        else if (key == "audio2" && value.Length > 0) _audioFiles[2] = value;
                    }
                }
                else
                {
                    _textStart.Text = string.Empty;
                }
            }
            catch
            {
                // 配置读取失败时按默认值运行
            }

            for (int i = 0; i < 3; i++)
            {
                if (string.IsNullOrEmpty(_audioFiles[i]))
                {
                    _audioFiles[i] = FindDefaultAudio(i);
                }
            }
        }

        private void SaveSettings()
        {
            try
            {
                StringBuilder builder = new StringBuilder();
                builder.AppendLine("# 考试自动打铃 配置文件");
                builder.AppendLine("start=" + _textStart.Text.Trim());
                for (int i = 0; i < 3; i++)
                {
                    builder.AppendLine("audio" + i + "=" + (_audioFiles[i] ?? string.Empty));
                }
                File.WriteAllText(ConfigPath(), builder.ToString(), Encoding.UTF8);
            }
            catch
            {
                // 配置保存失败不影响打铃主流程
            }
        }

        private static string FindDefaultAudio(int index)
        {
            try
            {
                string dir = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "audio");
                if (!Directory.Exists(dir)) return null;

                string prefix = (index + 1).ToString("00", CultureInfo.InvariantCulture);
                string[] files = Directory.GetFiles(dir);
                Array.Sort(files, StringComparer.OrdinalIgnoreCase);

                foreach (string file in files)
                {
                    string name = Path.GetFileName(file);
                    if (!name.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) continue;
                    if (IsAudioFile(file)) return file;
                }
            }
            catch
            {
                // 忽略自动查找异常
            }
            return null;
        }

        private static bool IsAudioFile(string path)
        {
            string extension = Path.GetExtension(path).ToLowerInvariant();
            return extension == ".wav" || extension == ".mp3" || extension == ".wma";
        }

        #endregion

        private void WriteLog(string message)
        {
            if (_textLog == null || _textLog.IsDisposed) return;
            _textLog.AppendText("[" + _clock.Now.ToString("HH:mm:ss", CultureInfo.InvariantCulture) + "] " + message + Environment.NewLine);
        }
    }
}
