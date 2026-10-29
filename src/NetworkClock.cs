using System;
using System.IO;
using System.Net;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;

namespace ExamBell
{
    /// <summary>
    /// 网络校时：请求淘宝公开时间戳接口，换算出北京时间（UTC+8）与本地时间的偏移。
    /// 界面不阻塞，调用方需放到后台线程执行。
    /// </summary>
    internal static class NetworkClock
    {
        private const string TimestampUrl = "http://api.m.taobao.com/rest/api3.do?api=mtop.common.getTimestamp";
        private const int TimeoutMilliseconds = 3000;

        internal sealed class Result
        {
            public bool Ok;
            public DateTime Beijing;
            public long OffsetMilliseconds;
            public long RoundTripMilliseconds;
            public string Message;
        }

        public static Result Query(int attempts)
        {
            Result last = null;
            for (int i = 0; i < attempts; i++)
            {
                last = QueryOnce();
                if (last.Ok) return last;
                if (i < attempts - 1) Thread.Sleep(600);
            }
            return last;
        }

        private static Result QueryOnce()
        {
            DateTime before = DateTime.Now;
            try
            {
                HttpWebRequest request = (HttpWebRequest)WebRequest.Create(TimestampUrl);
                request.Method = "GET";
                request.Timeout = TimeoutMilliseconds;
                request.ReadWriteTimeout = TimeoutMilliseconds;
                request.UserAgent = "Mozilla/5.0 (Windows NT 6.1; Win64; x64)";
                request.KeepAlive = false;

                string body;
                using (HttpWebResponse response = (HttpWebResponse)request.GetResponse())
                using (Stream stream = response.GetResponseStream())
                using (StreamReader reader = new StreamReader(stream, Encoding.UTF8))
                {
                    body = reader.ReadToEnd();
                }

                DateTime after = DateTime.Now;

                Match match = Regex.Match(body, "\"t\"\\s*:\\s*\"?(\\d{10,14})");
                if (!match.Success) return Fail("校时接口返回内容中没有时间戳字段");

                long stamp = long.Parse(match.Groups[1].Value);
                if (stamp < 100000000000L) stamp *= 1000; // 少数情况下接口返回秒级时间戳

                DateTime utc = new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc).AddMilliseconds(stamp);
                DateTime beijing = utc.AddHours(8);

                long roundTrip = (long)(after - before).TotalMilliseconds;
                DateTime reference = after.AddMilliseconds(-(roundTrip / 2));
                long offset = (beijing.Ticks - reference.Ticks) / TimeSpan.TicksPerMillisecond;

                Result result = new Result();
                result.Ok = true;
                result.Beijing = beijing;
                result.OffsetMilliseconds = offset;
                result.RoundTripMilliseconds = roundTrip;
                result.Message = "往返时延 " + roundTrip + " ms";
                return result;
            }
            catch (Exception ex)
            {
                return Fail(Describe(ex));
            }
        }

        private static Result Fail(string message)
        {
            Result result = new Result();
            result.Ok = false;
            result.Message = message;
            return result;
        }

        private static string Describe(Exception ex)
        {
            if (ex is WebException)
            {
                WebException webException = (WebException)ex;
                if (webException.Status == WebExceptionStatus.Timeout) return "连接校时服务器超时";
                if (webException.Status == WebExceptionStatus.NameResolutionFailure) return "无法解析校时服务器地址（网络未连接）";
                if (webException.Status == WebExceptionStatus.ConnectFailure) return "无法连接校时服务器（网络未连接或被拦截）";
                if (webException.Status == WebExceptionStatus.ProtocolError) return "校时服务器返回" + "异常响应";
            }
            string message = ex.Message;
            if (string.IsNullOrEmpty(message)) message = ex.GetType().Name;
            return message;
        }
    }
}
