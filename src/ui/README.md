# Declaw Management Console

The **Declaw Management Console** is a terminal-based interface allowing developers to interact directly with the `\DeclawPort` communication port on the running Kernel Minifilter.

---

## Features

* **Query Status**: Displays live telemetry counts (active protected folders, active blocked PIDs, and intercepted block attempts).
* **Dynamic Folder Protection**: Add or clear folder paths to protect in real time.
* **Manual PID Management**: Add or remove individual Process IDs for testing purposes.

---

## How to Build

From the command line:

```cmd
cd src\ui
dotnet publish -c Release -r win-x64 --self-contained false -o bin\publish
```

---

## How to Run in Test VM

Ensure the driver is loaded in your VM (`fltmc load Declaw`), then launch:
```cmd
DeclawControl.exe
```
