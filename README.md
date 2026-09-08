# 🐾 Declaw — Windows 11 AI File Access Guard

**Declaw** is an Endpoint Detection and Response (EDR) style security utility for Windows 11. It acts as an unbreakable kernel-level shield that prevents autonomous AI coding agents (like Claude Code, Cursor, Windsurf, Antigravity, GitHub Copilot, and Aider) from reading, scanning, or modifying your sensitive files and directories.

---

## 🎯 Why Use Declaw?

Modern AI developer tools run under **your Windows user account**. This means they inherently possess the exact same file system permissions that you do. Standard Windows NTFS permissions (like right-clicking a folder and making it read-only) are completely ineffective at stopping an AI agent from accidentally traversing into a folder containing API keys, personal documents, or proprietary code.

**Declaw solves this by operating at Ring 0 (the Windows Kernel).** 
When an AI agent is detected, Declaw instantly cuts off its ability to interact with protected folders at the hardware driver level, while allowing *you* (via Explorer, Notepad, etc.) to access those exact same files without interruption.

### Ideal Use Cases:
1. **Preventing Secret Leaks**: Stop AI agents from accidentally indexing `C:\Secrets`, `.env` folders, or AWS credential files and sending them to an LLM provider.
2. **Isolating Client Work**: If you are using an autonomous agent to build a personal project, you can use Declaw to ensure it cannot wander into your employer's proprietary source code directories.
3. **Containing Autonomous Runaways**: Prevent agents that execute arbitrary terminal commands from mistakenly wiping or modifying critical system files outside of their designated workspace.

---

## ✨ Features

- **Kernel-Level Enforcement**: Uses a Microsoft Windows Minifilter Driver (`Declaw.sys`) to intercept file creation and open requests (`IRP_MJ_CREATE`) before they even hit the disk.
- **Zero-Latency for Normal Apps**: Employs an ultra-fast path inside the kernel. If a process isn't flagged as an AI, it bypasses the filter instantly, ensuring your PC runs at maximum speed.
- **Dynamic Heuristic Engine**: The background service automatically detects AI tools using:
  - **Direct Binaries**: `cursor.exe`, `windsurf.exe`, `claude.exe`, `aider.exe`, `cline.exe`, etc.
  - **Process Tree Inheritance**: If an agent spawns a PowerShell or Node instance, that child process automatically inherits the restriction.
  - **CLI Signatures**: Scans arguments for triggers like `@anthropic-ai/claude-code`, `antigravity`, etc.
- **Real-Time Configuration**: Add and remove protected folders instantly using the `DeclawControl.exe` dashboard without needing to restart the kernel driver.

---

## ⚡ Quick Start

### 1. Compile and Install the Kernel Driver (in a Test VM)

> [!CAUTION]
> **Do NOT test the kernel driver on your primary machine.** Use a Virtual Machine (Hyper-V, VMware, VirtualBox). A kernel bug = instant Blue Screen.

```cmd
:: In the VM — enable test signing and reboot
bcdedit /set testsigning on
shutdown /r /t 0

:: After reboot — install and load the driver
rundll32.exe setupapi,InstallHinfSection DefaultInstall 132 .\Declaw.inf
fltmc load Declaw
```

### 2. Configure Protected Folders

Edit `appsettings.json`:
```json
{
  "Declaw": {
    "ProtectedFolders": [
      "C:\\Users\\YourName\\Documents\\Confidential",
      "C:\\Secrets"
    ]
  }
}
```

### 3. Run the Background Service

```cmd
:: Interactive mode (console, for debugging):
DeclawService.exe

:: Or install as a permanent Windows Service:
sc.exe create DeclawService binPath= "C:\Path\To\DeclawService.exe" start= auto
sc.exe start DeclawService
```

### 4. Management Console

Launch `DeclawControl.exe` to open the interactive dashboard. You can view live telemetry (how many times an AI was blocked), manually block Process IDs (PIDs), and dynamically protect new folders.

---

## 🔨 Building from Source (v1.0)

Declaw's user-space tools are built as **self-contained, zero-dependency** executables. You do not need to install the .NET runtime on the target machine.

### Prerequisites
- **.NET 8 SDK** (To compile the service and UI)
- **Visual Studio 2022 + WDK** (To compile the kernel driver)

### One-Click Build
```cmd
build.bat
```

This will produce all standalone executables and package them alongside the driver source into the `publish/` directory.

---

## 🏛️ Architecture

```
┌─────────────────────┐     ┌──────────────────────────┐
│  DeclawControl      │     │   DeclawService          │
│  (Management CLI)   │     │   (Background Service)   │
│                     │     │                          │
│  • Add/remove paths │     │  • WMI Process Monitor   │
│  • View telemetry   │     │  • AI Heuristic Engine   │
│  • Manual PID block │     │  • Process Tree Tracking │
└────────┬────────────┘     └────────────┬─────────────┘
         │                               │
         │     Filter Communication Port │
         │      (\\DeclawPort)            │
         └───────────┬──────────────┬────┘
                     │              │
         ┌───────────▼──────────────▼─────────────┐
         │     Declaw.sys (Kernel Minifilter)      │
         │                                        │
         │  • Intercepts IRP_MJ_CREATE (Ring 0)   │
         │  • Checks PID against blocked list     │
         │  • Checks path against protected list  │
         │  • Returns STATUS_ACCESS_DENIED        │
         └────────────────────────────────────────┘
```

---

## ⚠️ Disclaimer

> [!CAUTION]
> **IMPORTANT SECURITY & STABILITY NOTICE — PLEASE READ CAREFULLY**

1. **Kernel-Mode Operations (Ring 0 Risk)**:
   * Declaw contains a Windows File System Minifilter Driver (`Declaw.sys`) that runs at the highest privilege level in the operating system.
   * Modifying kernel components carries inherent risk. An unhandled exception or faulty condition in kernel space can result in an immediate **BugCheck / Blue Screen of Death (BSOD)** and potential data loss.
   * **Always test Declaw inside an isolated Virtual Machine (VM)** before considering any broader testing. Never test development kernel drivers on mission-critical or production hardware.

2. **Educational & Security Research Purposes**:
   * This project is provided strictly as a proof-of-concept for research, security evaluation, and educational exploration of process-aware kernel filesystem filtering.
   * The authors and maintainers do not encourage or endorse testing on systems containing unbacked-up sensitive data.

3. **No Warranty & Limitation of Liability**:
   * This software is provided *"as is"*, without warranty of any kind, express or implied, including but not limited to the warranties of merchantability, fitness for a particular purpose, and noninfringement.
   * In no event shall the authors, contributors, or copyright holders be liable for any claim, damages (including, without limitation, direct, indirect, incidental, special, or consequential damages), data loss, or system interruption arising from the use, misuse, or inability to use this software.

4. **Third-Party Trademarks**:
   * Any company, product, or service names mentioned (including Windows, Microsoft, Claude, Cursor, Windsurf, GitHub Copilot, Antigravity, Aider, Cline) are trademarks or registered trademarks of their respective owners. Mention of these names is purely for compatibility context and does not imply any affiliation, sponsorship, or endorsement.

---

## 📄 License

Declaw is licensed under the **[MIT License](LICENSE)**.

You are free to use, study, modify, and distribute this software, subject to the terms and conditions outlined in the [LICENSE](LICENSE) file.

