# Declaw Kernel-Mode Minifilter Driver

This directory contains the source code for the **Declaw File System Minifilter Driver**, designed to intercept file access in the Windows Kernel and deny access to protected folders when requested by identified AI processes.

---

## ⚠️ CRITICAL SAFETY NOTICE

**DO NOT install or test this driver on your primary machine.**
Always build and test kernel drivers inside an isolated **Virtual Machine (VM)** (e.g., Hyper-V, VMware, or VirtualBox) with snapshotting enabled. A bug in kernel mode will result in an immediate Blue Screen of Death (BSOD).

---

## 1. Prerequisites for Building

To compile this driver, the following must be installed on your build machine:
* **Visual Studio 2022** (with Desktop development with C++)
* **Windows 11 SDK** (matching your target OS build)
* **Windows Driver Kit (WDK)** for Windows 11

---

## 2. Compilation Instructions

Open **Developer Command Prompt for VS 2022** and navigate to this directory:

```cmd
cd src\driver
msbuild /p:Configuration=Release /p:Platform=x64 Declaw.vcxproj
```

The compiled output will be generated in `bin\x64\Release\`:
* `Declaw.sys` (The kernel driver binary)
* `Declaw.inf` (Installation metadata)
* `Declaw.cat` (Security catalog)

---

## 3. Safe Testing in a Virtual Machine (Step-by-Step)

### Step A: Configure Test Signing in the VM
Modern 64-bit Windows requires drivers to be signed with a trusted certificate. For development:
1. Inside the **Virtual Machine**, open an elevated Command Prompt (`cmd.exe` as Administrator):
   ```cmd
   bcdedit /set testsigning on
   ```
2. Reboot the VM. You should see "Test Mode" in the bottom-right corner of the Windows desktop.

### Step B: Install the Driver
1. Copy `Declaw.sys` and `Declaw.inf` into a folder inside the VM (e.g., `C:\Declaw\`).
2. Right-click `Declaw.inf` and select **Install** (or run `rundll32.exe setupapi,InstallHinfSection DefaultInstall 132 .\Declaw.inf`).

### Step C: Load and Unload via Filter Manager
Open an Administrator Command Prompt:

* **To start the minifilter:**
  ```cmd
  fltmc load Declaw
  ```
* **To check active filters:**
  ```cmd
  fltmc filters
  ```
  *(You should see `Declaw` running at altitude `370030`)*.

* **To stop and unload the driver:**
  ```cmd
  fltmc unload Declaw
  ```

---

## 4. Monitoring Kernel Debug Logs

To view output from `DbgPrintEx` statements:
1. Download **DebugView** (Sysinternals).
2. Run `Dbgview.exe` as Administrator in the VM.
3. Check **Capture** $\rightarrow$ **Capture Kernel** and **Enable Verbose Kernel Output**.
4. You will see real-time logs when paths are added, PIDs are registered, and unauthorized file accesses are blocked.
