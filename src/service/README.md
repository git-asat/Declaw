# Declaw Background Service

The **Declaw Background Service** is a .NET 8 Windows Service responsible for:
1. Connecting to the Kernel Minifilter driver (`\DeclawPort`) via `fltlib.dll`.
2. Monitoring process start/stop events across Windows via WMI (`Win32_ProcessStartTrace`).
3. Identifying AI coding agents (direct binary matches, command line heuristics, and process tree inheritance).
4. Synchronizing blocked Process IDs (PIDs) and protected folder paths with the kernel driver in real time.

---

## 1. Prerequisites for Building

* **.NET 8 SDK** (x64)

---

## 2. Compilation Instructions

From the command line:

```cmd
cd src\service
dotnet publish -c Release -r win-x64 --self-contained false -o bin\publish
```

---

## 3. Deployment in Test VM

### Running Interactively (Console Mode for Debugging):
In an elevated command prompt inside your VM:
```cmd
DeclawService.exe
```
This will print live log statements showing process detections, heuristic matches, and kernel telemetry.

### Installing as a Windows Service:
```cmd
sc.exe create DeclawService binPath= "C:\Path\To\DeclawService.exe" start= auto
sc.exe start DeclawService
```

### Stopping and Removing:
```cmd
sc.exe stop DeclawService
sc.exe delete DeclawService
```
