using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

namespace Declaw.Service;

public class Program
{
    public static async Task Main(string[] args)
    {
        IHost host = Host.CreateDefaultBuilder(args)
            .UseWindowsService(options =>
            {
                options.ServiceName = "DeclawService";
            })
            .ConfigureServices((hostContext, services) =>
            {
                services.AddSingleton<DriverComm>();
                services.AddSingleton<ProcessMonitor>();
                services.AddHostedService<ShieldWorker>();
            })
            .Build();

        await host.RunAsync();
    }
}

public sealed class ShieldWorker : BackgroundService
{
    private readonly ILogger<ShieldWorker> _logger;
    private readonly IConfiguration _configuration;
    private readonly DriverComm _driverComm;
    private readonly ProcessMonitor _processMonitor;

    public ShieldWorker(
        ILogger<ShieldWorker> logger,
        IConfiguration configuration,
        DriverComm driverComm,
        ProcessMonitor processMonitor)
    {
        _logger = logger;
        _configuration = configuration;
        _driverComm = driverComm;
        _processMonitor = processMonitor;
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        _logger.LogInformation("Declaw Background Service starting...");

        // 1. Wait/Connect to Kernel Driver
        bool connected = false;
        while (!stoppingToken.IsCancellationRequested && !connected)
        {
            connected = _driverComm.Connect();
            if (connected)
            {
                _logger.LogInformation("Successfully connected to \\DeclawPort (Kernel Minifilter).");
                break;
            }

            _logger.LogWarning("Declaw driver not yet available. Retrying in 5 seconds...");
            try
            {
                await Task.Delay(5000, stoppingToken);
            }
            catch (OperationCanceledException)
            {
                return;
            }
        }

        // 2. Load protected folders from configuration
        List<string>? protectedFolders = _configuration
            .GetSection("Declaw:ProtectedFolders")
            .Get<List<string>>();

        if (protectedFolders != null)
        {
            foreach (string folder in protectedFolders)
            {
                if (!string.IsNullOrWhiteSpace(folder))
                {
                    bool ok = _driverComm.AddProtectedPath(folder);
                    _logger.LogInformation("Registered protected folder [{Folder}]: Result = {Result}", folder, ok ? "Success" : "Failed");
                }
            }
        }

        // 3. Start real-time AI process monitoring
        _processMonitor.Start();

        // 4. Monitoring Loop: Polling telemetry & stats
        int intervalSec = _configuration.GetValue("Declaw:StatsPollingIntervalSeconds", 30);

        while (!stoppingToken.IsCancellationRequested)
        {
            try
            {
                await Task.Delay(TimeSpan.FromSeconds(intervalSec), stoppingToken);
            }
            catch (OperationCanceledException)
            {
                break;
            }

            var (success, stats) = _driverComm.GetStats();
            if (success)
            {
                _logger.LogInformation(
                    "[Shield Telemetry] Protected Paths: {PathCount} | Blocked PIDs: {PidCount} | Total Intercepted Blocks: {TotalBlocks}",
                    stats.ProtectedPathCount,
                    stats.BlockedPidCount,
                    stats.TotalBlocksCount
                );
            }
        }

        _logger.LogInformation("Declaw Background Service stopping...");
    }

    public override void Dispose()
    {
        _processMonitor.Dispose();
        _driverComm.Dispose();
        base.Dispose();
    }
}
