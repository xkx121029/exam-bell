using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

namespace ExamBell
{
    /// <summary>
    /// 基于 Win32 MCI（winmm.dll）的音频播放器。
    /// 不依赖第三方库，Windows 7 系统自带解码能力，支持 WAV / MP3 / WMA。
    /// </summary>
    internal sealed class AudioPlayer
    {
        private const string Alias = "exam_bell";

        [DllImport("winmm.dll", CharSet = CharSet.Auto)]
        private static extern int mciSendString(string command, StringBuilder returnValue, int returnLength, IntPtr callback);

        [DllImport("winmm.dll", CharSet = CharSet.Auto)]
        private static extern bool mciGetErrorString(int errorCode, StringBuilder errorText, int errorLength);

        private bool _opened;
        private string _currentPath;

        public string LastError { get; private set; }

        public string CurrentPath
        {
            get { return _currentPath; }
        }

        public bool IsPlaying
        {
            get
            {
                if (!_opened) return false;
                StringBuilder buffer = new StringBuilder(64);
                if (mciSendString("status " + Alias + " mode", buffer, buffer.Capacity, IntPtr.Zero) != 0) return false;
                return buffer.ToString().IndexOf("playing", StringComparison.OrdinalIgnoreCase) >= 0;
            }
        }

        public bool Play(string path)
        {
            Close();
            LastError = null;

            if (string.IsNullOrEmpty(path) || !File.Exists(path))
            {
                LastError = "音频文件不存在";
                return false;
            }

            _currentPath = path;

            // 先让 MCI 按扩展名自动识别，失败再按常见设备类型重试
            int result = Send("open \"" + path + "\" alias " + Alias);
            if (result != 0)
            {
                result = Send("open \"" + path + "\" type mpegvideo alias " + Alias);
            }
            if (result != 0)
            {
                result = Send("open \"" + path + "\" type waveaudio alias " + Alias);
            }
            if (result != 0)
            {
                LastError = Describe(result);
                _currentPath = null;
                return false;
            }

            _opened = true;
            result = Send("play " + Alias);
            if (result != 0)
            {
                LastError = Describe(result);
                Close();
                return false;
            }

            return true;
        }

        public void Stop()
        {
            if (!_opened) return;
            Send("stop " + Alias);
        }

        public void Close()
        {
            if (!_opened) return;
            Send("stop " + Alias);
            Send("close " + Alias);
            _opened = false;
            _currentPath = null;
        }

        private static int Send(string command)
        {
            return mciSendString(command, null, 0, IntPtr.Zero);
        }

        private static string Describe(int errorCode)
        {
            StringBuilder buffer = new StringBuilder(256);
            if (mciGetErrorString(errorCode, buffer, buffer.Capacity))
            {
                return buffer.ToString();
            }
            return "MCI 错误码 " + errorCode;
        }
    }

    /// <summary>
    /// 以北京标准时间（UTC+8）为基准的时钟。
    /// 记录网络校时得到的偏移量，并用高精度计时器推算当前时间，
    /// 避免运行时系统时间被调整造成的偏差。
    /// </summary>
    internal sealed class BeijingClock
    {
        private Stopwatch _stopwatch;
        private DateTime _baseLocal;
        private long _offsetTicks;
        private long _offsetMilliseconds;
        private bool _synced;

        public bool Synced
        {
            get { return _synced; }
        }

        public long OffsetMilliseconds
        {
            get { return _offsetMilliseconds; }
        }

        public DateTime Now
        {
            get
            {
                if (_stopwatch == null) return DateTime.Now.AddTicks(_offsetTicks);
                return _baseLocal.Add(_stopwatch.Elapsed).AddTicks(_offsetTicks);
            }
        }

        public void ApplyNetworkTime(DateTime beijing, long offsetMilliseconds)
        {
            _baseLocal = DateTime.Now;
            _offsetTicks = beijing.Ticks - _baseLocal.Ticks;
            if (_stopwatch == null) _stopwatch = new Stopwatch();
            _stopwatch.Restart();
            _offsetMilliseconds = offsetMilliseconds;
            _synced = true;
        }

        public void UseLocalTime()
        {
            _baseLocal = DateTime.Now;
            _offsetTicks = 0;
            if (_stopwatch == null) _stopwatch = new Stopwatch();
            _stopwatch.Restart();
            _offsetMilliseconds = 0;
            _synced = false;
        }

        /// <summary>
        /// 重建计时基准但保留已有偏移量（用于休眠唤醒后补齐系统计时缺口）。
        /// </summary>
        public void Rebase()
        {
            _baseLocal = DateTime.Now;
            if (_stopwatch == null) _stopwatch = new Stopwatch();
            _stopwatch.Restart();
        }

    }
}
