using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Management;
using Microsoft.Extensions.Logging;

namespace Declaw.Service;

public sealed class ProcessMonitor : IDisposable
{
    private readonly ILogger<ProcessMonitor> _logger;
    private readonly DriverComm _driverComm;
    private readonly ConcurrentDictionary<uint, string> _blockedPids = new();

    private ManagementEventWatcher? _startWatcher;
    private ManagementEventWatcher? _stopWatcher;
    private bool _disposed;

    // Direct executable names known to be AI agents
    private static readonly HashSet<string> TargetExecutables = new(StringComparer.OrdinalIgnoreCase)
    {
        "cursor.exe",
        "windsurf.exe",
        "copilot-language-server.exe",
        "antigravity.exe",
        "claude.exe",
        "gemini.exe",
        "aider.exe",
        "cline.exe"
    };

    // Interpreters/runtimes commonly used to execute AI agent CLI tools
    private static readonly HashSet<string> ScriptHosts = new(StringComparer.OrdinalIgnoreCase)
    {
        "node.exe",
        "python.exe",
        "python3.exe",
        "pythonw.exe",
        "deno.exe",
        "bun.exe"
    };

    // Heuristic command-line substrings that indicate an AI agent
    private static readonly string[] AiCommandLineTokens =
    [
        "claude-code",
        "@anthropic-ai",
        "antigravity",
        "aider",
        "open-interpreter",
        "continue.dev",
        "copilot",
        "@google/agy",
        "cline"
    ];

    public ProcessMonitor(ILogger<ProcessMonitor> logger, DriverComm driverComm)
    {
        _logger = logger;
        _driverComm = driverComm;
    }

    public void Start()
    {
        try
        {
            _logger.LogInformation("Starting process monitor...");

            // Scan already-running processes before subscribing to events
            ScanExistingProcesses();

            // Watch for new process start events via WMI ETW
            _startWatcher = new ManagementEventWatcher(
                new WqlEventQuery("SELECT * FROM Win32_ProcessStartTrace"));
            _startWatcher.EventArrived += OnProcessStarted;
            _startWatcher.Start();

            // Watch for process stop events to clean up stale PIDs
            _stopWatcher = new ManagementEventWatcher(
                new WqlEventQuery("SELECT * FROM Win32_ProcessStopTrace"));
            _stopWatcher.EventArrived += OnProcessStopped;
            _stopWatcher.Start();

            _logger.LogInformation("Process monitor active. Watching for AI agents...");
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to initialize WMI process watchers. Ensure this service runs as Administrator/SYSTEM.");
        }
    }

    /// <summary>
    /// Scans all currently running processes at startup to catch AI agents
    /// that were already running before the service started.
    /// </summary>
    private void ScanExistingProcesses()
    {
        try
        {
            using var searcher = new ManagementObjectSearcher(
                "SELECT ProcessId, Name, CommandLine, ParentProcessId FROM Win32_Process");
            using var results = searcher.Get();

            // First pass: block direct matches
            foreach (ManagementObject proc in results)
            {
                uint pid = Convert.ToUInt32(proc["ProcessId"]);
                string name = proc["Name"]?.ToString() ?? string.Empty;

                if (TargetExecutables.Contains(name))
                {
                    RegisterBlocked(pid, name, $"Direct binary match (startup scan): {name}");
                }
            }

            // Second pass: block script hosts with AI command lines, and children
            foreach (ManagementObject proc in results)
            {
                uint pid = Convert.ToUInt32(proc["ProcessId"]);
                uint parentPid = Convert.ToUInt32(proc["ParentProcessId"] ?? 0u);
                string name = proc["Name"]?.ToString() ?? string.Empty;
                string cmdLine = proc["CommandLine"]?.ToString() ?? string.Empty;

                if (_blockedPids.ContainsKey(pid))
                    continue;

                // Child of a blocked process
                if (_blockedPids.ContainsKey(parentPid))
                {
                    RegisterBlocked(pid, name, $"Child of blocked AI process (startup scan, parent PID {parentPid})");
                    continue;
                }

                // Script host with AI command line
                if (ScriptHosts.Contains(name) && !string.IsNullOrEmpty(cmdLine))
                {
                    foreach (string token in AiCommandLineTokens)
                    {
                        if (cmdLine.Contains(token, StringComparison.OrdinalIgnoreCase))
                        {
                            RegisterBlocked(pid, name, $"Command-line heuristic (startup scan): '{token}'");
                            break;
                        }
                    }
                }
            }

            _logger.LogInformation("Startup scan complete. Found {Count} AI agent process(es).", _blockedPids.Count);
        }
        catch (Exception ex)
        {
            _logger.LogWarning(ex, "Failed to scan existing processes at startup.");
        }
    }

    private void RegisterBlocked(uint pid, string processName, string reason)
    {
        if (_blockedPids.TryAdd(pid, reason))
        {
            _logger.LogWarning("[AI DETECTED] Blocking PID: {Pid} ({Name}) - {Reason}", pid, processName, reason);
            _driverComm.AddBlockedPid(pid);
        }
    }

    private void OnProcessStarted(object sender, EventArrivedEventArgs e)
    {
        try
        {
            uint pid = Convert.ToUInt32(e.NewEvent.Properties["ProcessID"].Value);
            uint parentPid = Convert.ToUInt32(e.NewEvent.Properties["ParentProcessID"].Value);
            string processName = e.NewEvent.Properties["ProcessName"].Value?.ToString() ?? string.Empty;

            EvaluateNewProcess(pid, parentPid, processName);
        }
        catch (Exception ex)
        {
            _logger.LogTrace(ex, "Error processing process start event.");
        }
    }

    private void EvaluateNewProcess(uint pid, uint parentPid, string processName)
    {
        // 1. Direct executable name match
        if (TargetExecutables.Contains(processName))
        {
            RegisterBlocked(pid, processName, $"Direct binary match: {processName}");
            return;
        }

        // 2. Parent-child inheritance (Agent spawned a shell/script/child)
        if (_blockedPids.ContainsKey(parentPid))
        {
            RegisterBlocked(pid, processName, $"Child of blocked AI process (Parent PID {parentPid})");
            return;
        }

        // 3. Command-line heuristic inspection for generic script hosts
        if (ScriptHosts.Contains(processName))
        {
            string cmdLine = GetCommandLine(pid);
            if (!string.IsNullOrEmpty(cmdLine))
            {
                foreach (string token in AiCommandLineTokens)
                {
                    if (cmdLine.Contains(token, StringComparison.OrdinalIgnoreCase))
                    {
                        RegisterBlocked(pid, processName, $"Command-line heuristic match: contains '{token}'");
                        return;
                    }
                }
            }
        }
    }

    private void OnProcessStopped(object sender, EventArrivedEventArgs e)
    {
        try
        {
            uint pid = Convert.ToUInt32(e.NewEvent.Properties["ProcessID"].Value);

            if (_blockedPids.TryRemove(pid, out string? reason))
            {
                _logger.LogInformation("[AI EXITED] Unblocking PID: {Pid} (was: {Reason})", pid, reason);
                _driverComm.RemoveBlockedPid(pid);
            }
        }
        catch (Exception ex)
        {
            _logger.LogTrace(ex, "Error handling process stop event.");
        }
    }

    private static string GetCommandLine(uint pid)
    {
        try
        {
            using var searcher = new ManagementObjectSearcher(
                $"SELECT CommandLine FROM Win32_Process WHERE ProcessId = {pid}");
            using var objects = searcher.Get();
            foreach (ManagementObject obj in objects)
            {
                return obj["CommandLine"]?.ToString() ?? string.Empty;
            }
        }
        catch
        {
            // Process might have exited already or requires elevated permissions
        }
        return string.Empty;
    }

    public void Dispose()
    {
        if (!_disposed)
        {
            _startWatcher?.Stop();
            _startWatcher?.Dispose();
            _stopWatcher?.Stop();
            _stopWatcher?.Dispose();
            _disposed = true;
        }
    }
}
