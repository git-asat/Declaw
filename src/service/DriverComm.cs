using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace Declaw.Service;

public enum DeclawCommand : uint
{
    CmdAddProtectedPath = 1,
    CmdRemoveProtectedPath = 2,
    CmdClearProtectedPaths = 3,
    CmdAddBlockedPid = 4,
    CmdRemoveBlockedPid = 5,
    CmdClearBlockedPids = 6,
    CmdGetStats = 7
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct DeclawReplyMessage
{
    public uint Status;
    public uint ProtectedPathCount;
    public uint BlockedPidCount;
    public uint TotalBlocksCount;
}

public sealed class DriverComm : IDisposable
{
    private const string PortName = "\\DeclawPort";
    private const int MaxPathLen = 512;
    private SafeFileHandle? _portHandle;
    private bool _disposed;

    #region P/Invoke

    [DllImport("fltlib.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern int FilterConnectCommunicationPort(
        string lpPortName,
        uint dwOptions,
        IntPtr lpContext,
        ushort wSizeOfContext,
        IntPtr lpSecurityAttributes,
        out SafeFileHandle hPort);

    [DllImport("fltlib.dll", SetLastError = true)]
    private static extern int FilterSendMessage(
        SafeFileHandle hPort,
        IntPtr lpInBuffer,
        uint dwInBufferSize,
        IntPtr lpOutBuffer,
        uint dwOutBufferSize,
        out uint lpBytesReturned);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern uint QueryDosDevice(
        string lpDeviceName,
        StringBuilder lpTargetPath,
        int ucchMax);

    #endregion

    public bool IsConnected => _portHandle != null && !_portHandle.IsInvalid && !_portHandle.IsClosed;

    public bool Connect()
    {
        if (IsConnected)
            return true;

        int hResult = FilterConnectCommunicationPort(
            PortName,
            0,
            IntPtr.Zero,
            0,
            IntPtr.Zero,
            out _portHandle);

        return hResult == 0 && IsConnected;
    }

    /// <summary>
    /// Converts a Win32 DOS path (e.g. C:\Secret) to an NT Kernel Device Path (e.g. \Device\HarddiskVolume3\Secret)
    /// </summary>
    public static string? ConvertDosToNtPath(string dosPath)
    {
        try
        {
            string fullPath = Path.GetFullPath(dosPath);
            string drive = Path.GetPathRoot(fullPath)?.TrimEnd('\\') ?? string.Empty;

            if (string.IsNullOrEmpty(drive))
                return null;

            StringBuilder sb = new(260);
            if (QueryDosDevice(drive, sb, sb.Capacity) == 0)
                return null;

            string devicePath = sb.ToString();
            string relativePath = fullPath.Substring(drive.Length);

            return devicePath + relativePath;
        }
        catch
        {
            return null;
        }
    }

    public bool AddProtectedPath(string rawPath)
    {
        string? ntPath = ConvertDosToNtPath(rawPath) ?? rawPath;
        return SendPathCommand(DeclawCommand.CmdAddProtectedPath, ntPath);
    }

    public bool ClearProtectedPaths()
    {
        return SendSimpleCommand(DeclawCommand.CmdClearProtectedPaths);
    }

    public bool AddBlockedPid(uint pid)
    {
        return SendPidCommand(DeclawCommand.CmdAddBlockedPid, pid);
    }

    public bool RemoveBlockedPid(uint pid)
    {
        return SendPidCommand(DeclawCommand.CmdRemoveBlockedPid, pid);
    }

    public bool ClearBlockedPids()
    {
        return SendSimpleCommand(DeclawCommand.CmdClearBlockedPids);
    }

    public (bool Success, DeclawReplyMessage Stats) GetStats()
    {
        if (!EnsureConnected())
            return (false, default);

        DeclawReplyMessage reply = default;
        bool success = SendCommandInternal(DeclawCommand.CmdGetStats, null, 0, out reply);
        return (success, reply);
    }

    private bool SendPathCommand(DeclawCommand cmd, string path)
    {
        if (!EnsureConnected())
            return false;

        byte[] pathBytes = new byte[MaxPathLen * sizeof(char)];
        byte[] encoded = Encoding.Unicode.GetBytes(path);
        Array.Copy(encoded, pathBytes, Math.Min(encoded.Length, pathBytes.Length - 2));

        return SendCommandInternal(cmd, pathBytes, 0, out _);
    }

    private bool SendPidCommand(DeclawCommand cmd, uint pid)
    {
        if (!EnsureConnected())
            return false;

        byte[] pidBytes = BitConverter.GetBytes(pid);
        return SendCommandInternal(cmd, pidBytes, 0, out _);
    }

    private bool SendSimpleCommand(DeclawCommand cmd)
    {
        if (!EnsureConnected())
            return false;

        return SendCommandInternal(cmd, null, 0, out _);
    }

    private unsafe bool SendCommandInternal(
        DeclawCommand command,
        byte[]? payload,
        uint extra,
        out DeclawReplyMessage reply)
    {
        reply = default;
        if (_portHandle == null || _portHandle.IsInvalid)
            return false;

        // Size: 4 bytes (Command) + MaxPathLen * 2 (Data union)
        int inBufferSize = sizeof(uint) + (MaxPathLen * sizeof(char));
        byte[] inBuffer = new byte[inBufferSize];

        // Write command
        BitConverter.GetBytes((uint)command).CopyTo(inBuffer, 0);

        // Write payload if present
        if (payload != null && payload.Length > 0)
        {
            Array.Copy(payload, 0, inBuffer, sizeof(uint), Math.Min(payload.Length, inBuffer.Length - sizeof(uint)));
        }

        int outBufferSize = Marshal.SizeOf<DeclawReplyMessage>();
        IntPtr pOutBuffer = Marshal.AllocHGlobal(outBufferSize);
        IntPtr pInBuffer = Marshal.AllocHGlobal(inBufferSize);

        try
        {
            Marshal.Copy(inBuffer, 0, pInBuffer, inBufferSize);

            int hResult = FilterSendMessage(
                _portHandle,
                pInBuffer,
                (uint)inBufferSize,
                pOutBuffer,
                (uint)outBufferSize,
                out uint bytesReturned);

            if (hResult == 0 && bytesReturned >= outBufferSize)
            {
                reply = Marshal.PtrToStructure<DeclawReplyMessage>(pOutBuffer);
                return reply.Status == 0; // STATUS_SUCCESS
            }

            return hResult == 0;
        }
        finally
        {
            Marshal.FreeHGlobal(pInBuffer);
            Marshal.FreeHGlobal(pOutBuffer);
        }
    }

    private bool EnsureConnected()
    {
        if (IsConnected)
            return true;

        return Connect();
    }

    public void Dispose()
    {
        if (!_disposed)
        {
            _portHandle?.Dispose();
            _portHandle = null;
            _disposed = true;
        }
    }
}
