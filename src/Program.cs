using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

namespace ExamBell
{
    internal static class Program
    {
        [DllImport("user32.dll")]
        private static extern bool SetProcessDPIAware();

        [STAThread]
        private static void Main()
        {
            Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
            Application.ThreadException += delegate(object sender, System.Threading.ThreadExceptionEventArgs e)
            {
                ReportCrash(e.Exception);
                MessageBox.Show("程序发生错误：\r\n" + e.Exception.Message, "错误", MessageBoxButtons.OK, MessageBoxIcon.Error);
            };

            try
            {
                SetProcessDPIAware();
            }
            catch
            {
                // 老系统不支持时忽略，使用系统默认缩放
            }

            try
            {
                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                Application.Run(new MainForm());
            }
            catch (Exception ex)
            {
                ReportCrash(ex);
                MessageBox.Show("程序启动失败：\r\n" + ex.Message, "错误", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private static void ReportCrash(Exception ex)
        {
            try
            {
                string dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "ExamBell");
                Directory.CreateDirectory(dir);
                StringBuilder builder = new StringBuilder();
                builder.AppendLine(DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + "  " + ex.GetType().FullName);
                builder.AppendLine(ex.Message);
                builder.AppendLine(ex.StackTrace);
                File.AppendAllText(Path.Combine(dir, "error.log"), builder.ToString() + Environment.NewLine, Encoding.UTF8);
            }
            catch
            {
                // 日志写入失败时忽略
            }
        }
    }
}
