using Microsoft.Web.WebView2.WinForms;
using System.Diagnostics;
using System.Drawing;
using System.Net.Http;
using System.Windows.Forms;

namespace KajoLayoutEditor;

static class Program
{
    private const int Port = 8765;
    private const string EditorPath = "/tools/layout_editor.html";
    private const string EditorVersion = "lvgl-previews-20261007";

    [STAThread]
    static void Main(string[] args)
    {
        ApplicationConfiguration.Initialize();

        // The editor only has the native LVGL mode now; a leftover --lvgl argument is ignored.
        const string appTitle = "KAJO-Dash Layout Editor";
        string root = FindProjectRoot();
        string url = $"http://127.0.0.1:{Port}{EditorPath}?v={EditorVersion}";
        Process? server = IsReachable(url) ? null : StartServer(root);
        if (server is null && !IsReachable(url))
        {
            MessageBox.Show(
                "The layout editor server did not start.\n\nRun run_layout_editor.bat from the project root and check logs/layout_editor.stderr.log.",
                appTitle,
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            return;
        }

        if (!WaitForUrl(url, server, TimeSpan.FromSeconds(20)))
        {
            MessageBox.Show(
                $"The local layout editor did not become reachable:\n{url}",
                appTitle,
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            StopServer(server);
            return;
        }

        using Form form = new()
        {
            Text = appTitle,
            Width = 1600,
            Height = 920,
            MinimumSize = new Size(1180, 720),
            StartPosition = FormStartPosition.CenterScreen,
            Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath),
            BackColor = Color.FromArgb(16, 19, 22)
        };

        WebView2 webView = new()
        {
            Dock = DockStyle.Fill,
            DefaultBackgroundColor = Color.FromArgb(16, 19, 22)
        };
        form.Controls.Add(webView);

        form.Load += async (_, _) =>
        {
            await webView.EnsureCoreWebView2Async();
            webView.CoreWebView2.Settings.AreDefaultContextMenusEnabled = true;
            webView.CoreWebView2.Settings.AreDevToolsEnabled = true;
            webView.CoreWebView2.Navigate(url);
        };
        form.FormClosed += (_, _) => StopServer(server);

        Application.Run(form);
    }

    private static string FindProjectRoot()
    {
        string root = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", ".."));
        if (File.Exists(Path.Combine(root, "tools", "serve_editor.py")))
        {
            return root;
        }

        string current = Directory.GetCurrentDirectory();
        if (File.Exists(Path.Combine(current, "tools", "serve_editor.py")))
        {
            return current;
        }

        return root;
    }

    private static Process? StartServer(string root)
    {
        Directory.CreateDirectory(Path.Combine(root, "logs"));
        string stdoutLog = Path.Combine(root, "logs", "layout_editor.stdout.log");
        string stderrLog = Path.Combine(root, "logs", "layout_editor.stderr.log");
        PythonCommand python = ResolvePythonCommand();

        ProcessStartInfo psi = new()
        {
            FileName = python.FileName,
            Arguments = $"{python.ArgumentPrefix}tools\\serve_editor.py {Port}",
            WorkingDirectory = root,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        };

        Process process = new() { StartInfo = psi, EnableRaisingEvents = true };
        process.OutputDataReceived += (_, e) => AppendLine(stdoutLog, e.Data);
        process.ErrorDataReceived += (_, e) => AppendLine(stderrLog, e.Data);

        try
        {
            process.Start();
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();
            return process;
        }
        catch (Exception exc)
        {
            AppendLine(stderrLog, $"Failed to start {python.DisplayName}: {exc}");
            return null;
        }
    }

    private sealed record PythonCommand(string FileName, string ArgumentPrefix, string DisplayName);

    private static PythonCommand ResolvePythonCommand()
    {
        string? py = FindOnPath("py.exe");
        if (py is not null)
        {
            return new PythonCommand(py, "-3 ", $"{py} -3");
        }

        string? python = FindOnPath("python.exe");
        if (python is not null)
        {
            return new PythonCommand(python, "", python);
        }

        return new PythonCommand("python", "", "python");
    }

    private static string? FindOnPath(string fileName)
    {
        string? path = Environment.GetEnvironmentVariable("PATH");
        if (string.IsNullOrWhiteSpace(path)) return null;
        foreach (string entry in path.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            string candidate = Path.Combine(entry, fileName);
            if (File.Exists(candidate) && !candidate.Contains(Path.Combine("Microsoft", "WindowsApps"), StringComparison.OrdinalIgnoreCase))
            {
                return candidate;
            }
        }
        return null;
    }

    private static bool WaitForUrl(string url, Process? server, TimeSpan timeout)
    {
        DateTime deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (IsReachable(url)) return true;
            if (server is { HasExited: true }) return false;
            Thread.Sleep(250);
        }
        return false;
    }

    private static bool IsReachable(string url)
    {
        try
        {
            using HttpClient client = new() { Timeout = TimeSpan.FromSeconds(1) };
            using HttpResponseMessage response = client.GetAsync(url).GetAwaiter().GetResult();
            return response.IsSuccessStatusCode;
        }
        catch
        {
            return false;
        }
    }

    private static void StopServer(Process? server)
    {
        try
        {
            if (server is { HasExited: false })
            {
                server.Kill(entireProcessTree: true);
            }
        }
        catch
        {
            // Best-effort shutdown.
        }
    }

    private static void AppendLine(string path, string? line)
    {
        if (line is null) return;
        try
        {
            File.AppendAllText(path, line + Environment.NewLine);
        }
        catch
        {
            // Logging should not block the editor.
        }
    }
}
