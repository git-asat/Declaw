using System;
using System.IO;
using Declaw.Service;

namespace Declaw.UI;

class Program
{
    static void Main(string[] args)
    {
        Console.Title = "Declaw Management Console";

        Console.ForegroundColor = ConsoleColor.Cyan;
        Console.WriteLine(@"
   ___    _   ____  _     _       _     _ 
  / _ \  (_) / ___|| |__ (_) ___ | | __| |
 / /_\ \ | | \___ \| '_ \| |/ _ \| |/ _` |
 |  _  | | |  ___) | | | | |  __/| | (_| |
 |_| |_| |_| |____/|_| |_|_|\___||_|\__,_|
      Windows 11 AI File Access Guard
");
        Console.ResetColor();

        using var driver = new DriverComm();

        Console.Write("Connecting to kernel minifilter (\\DeclawPort)... ");
        if (!driver.Connect())
        {
            Console.ForegroundColor = ConsoleColor.Red;
            Console.WriteLine("FAILED");
            Console.ResetColor();
            Console.WriteLine("\n[!] The Declaw kernel driver is not currently running or accessible.");
            Console.WriteLine("    Please ensure the driver is compiled and loaded via: 'fltmc load Declaw'\n");
            Console.WriteLine("Press any key to exit...");
            Console.ReadKey();
            return;
        }

        Console.ForegroundColor = ConsoleColor.Green;
        Console.WriteLine("CONNECTED\n");
        Console.ResetColor();

        bool running = true;
        while (running)
        {
            DisplayMenu();
            string? choice = Console.ReadLine()?.Trim();

            Console.WriteLine();
            switch (choice)
            {
                case "1":
                    ShowStatus(driver);
                    break;
                case "2":
                    AddProtectedFolder(driver);
                    break;
                case "3":
                    ClearProtectedFolders(driver);
                    break;
                case "4":
                    AddBlockedPid(driver);
                    break;
                case "5":
                    RemoveBlockedPid(driver);
                    break;
                case "6":
                    ClearBlockedPids(driver);
                    break;
                case "7":
                    running = false;
                    break;
                default:
                    Console.ForegroundColor = ConsoleColor.Yellow;
                    Console.WriteLine("Invalid selection. Try again.");
                    Console.ResetColor();
                    break;
            }

            if (running)
            {
                Console.WriteLine("\nPress Enter to continue...");
                Console.ReadLine();
            }
        }
    }

    static void DisplayMenu()
    {
        Console.ForegroundColor = ConsoleColor.White;
        Console.WriteLine("==================================================");
        Console.WriteLine(" 1. View Driver Status & Telemetry");
        Console.WriteLine(" 2. Add Protected Folder Path");
        Console.WriteLine(" 3. Clear All Protected Folders");
        Console.WriteLine(" 4. Manually Block a Process ID (PID)");
        Console.WriteLine(" 5. Manually Unblock a Process ID (PID)");
        Console.WriteLine(" 6. Clear All Blocked PIDs");
        Console.WriteLine(" 7. Exit");
        Console.WriteLine("==================================================");
        Console.ResetColor();
        Console.Write("Select an option [1-7]: ");
    }

    static void ShowStatus(DriverComm driver)
    {
        var (success, stats) = driver.GetStats();
        if (!success)
        {
            Console.ForegroundColor = ConsoleColor.Red;
            Console.WriteLine("Failed to query telemetry from driver.");
            Console.ResetColor();
            return;
        }

        Console.ForegroundColor = ConsoleColor.Green;
        Console.WriteLine("--- Driver Status ---");
        Console.WriteLine($"  Protected Folder Count : {stats.ProtectedPathCount}");
        Console.WriteLine($"  Blocked Process Count  : {stats.BlockedPidCount}");
        Console.WriteLine($"  Total Blocked Attempts : {stats.TotalBlocksCount}");
        Console.ResetColor();
    }

    static void AddProtectedFolder(DriverComm driver)
    {
        Console.Write("Enter full path of folder to protect (e.g. C:\\Secret): ");
        string? path = Console.ReadLine()?.Trim();

        if (string.IsNullOrWhiteSpace(path))
        {
            Console.WriteLine("Path cannot be empty.");
            return;
        }

        string? ntPath = DriverComm.ConvertDosToNtPath(path);
        Console.WriteLine($"Resolved NT Device Path: {ntPath ?? "(Could not resolve, using raw)"}");

        bool ok = driver.AddProtectedPath(path);
        if (ok)
        {
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine($"Successfully registered folder: {path}");
        }
        else
        {
            Console.ForegroundColor = ConsoleColor.Red;
            Console.WriteLine("Failed to register path with kernel driver.");
        }
        Console.ResetColor();
    }

    static void ClearProtectedFolders(DriverComm driver)
    {
        bool ok = driver.ClearProtectedPaths();
        if (ok)
        {
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine("Cleared all protected folders from driver.");
        }
        else
        {
            Console.ForegroundColor = ConsoleColor.Red;
            Console.WriteLine("Failed to clear protected folders.");
        }
        Console.ResetColor();
    }

    static void AddBlockedPid(DriverComm driver)
    {
        Console.Write("Enter Process ID (PID) to block: ");
        if (uint.TryParse(Console.ReadLine()?.Trim(), out uint pid))
        {
            bool ok = driver.AddBlockedPid(pid);
            if (ok)
            {
                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine($"PID {pid} is now blocked from accessing protected folders.");
            }
            else
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("Failed to register PID.");
            }
            Console.ResetColor();
        }
        else
        {
            Console.WriteLine("Invalid PID.");
        }
    }

    static void RemoveBlockedPid(DriverComm driver)
    {
        Console.Write("Enter Process ID (PID) to unblock: ");
        if (uint.TryParse(Console.ReadLine()?.Trim(), out uint pid))
        {
            bool ok = driver.RemoveBlockedPid(pid);
            if (ok)
            {
                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine($"PID {pid} unblocked.");
            }
            else
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("Failed to unblock PID (may not be in list).");
            }
            Console.ResetColor();
        }
        else
        {
            Console.WriteLine("Invalid PID.");
        }
    }

    static void ClearBlockedPids(DriverComm driver)
    {
        bool ok = driver.ClearBlockedPids();
        if (ok)
        {
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine("Cleared all blocked PIDs.");
        }
        else
        {
            Console.ForegroundColor = ConsoleColor.Red;
            Console.WriteLine("Failed to clear blocked PIDs.");
        }
        Console.ResetColor();
    }
}
