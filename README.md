<img width="1839" height="1187" alt="image" src="https://github.com/user-attachments/assets/d82df6ee-6c94-46db-8a83-00a05ab28f9d" />

<img width="1651" height="1183" alt="image" src="https://github.com/user-attachments/assets/c03b1c40-32da-490f-bde3-8d647dcb086a" />

## NOTE
This program still has some bugs, and I will fix them later when I have time. Contributions are also welcome. The application is working, but I plan to add more features, including a proper login page instead of requiring only an access token.

# OpenNord for Windows — native C++ client

OpenNord is an unofficial GPLv3 Windows desktop client for connecting an
existing NordVPN subscription with NordLynx or OpenVPN. The GUI, service, API integration,
session storage, IPC, and tunnel orchestration are written in C++20.

The project is informed by the open-source
[`NordSecurity/nordvpn-linux`](https://github.com/NordSecurity/nordvpn-linux)
architecture. It does not copy or depend on Nord's proprietary Windows client.

OpenNord is not affiliated with, sponsored by, or endorsed by Nord Security.
NordVPN and NordLynx are used only to describe compatibility.

## Implemented

- Native Qt 6 desktop GUI written in C++
- LocalSystem Windows service written in C++
- SID-authenticated, local-only named-pipe RPC with 1 MiB frame limits
- Manual Nord access-token login and DPAPI-encrypted per-user sessions
- Nord application-level response signature and digest verification using CNG
- Complete signed country/city catalog with search and service-side location revalidation
- NordLynx connection through official WireGuard for Windows/WireGuardNT
- OpenVPN UDP/TCP connection through OpenVPN Community with protected localhost management
- Signed Nord OpenVPN profile download, service-side sanitization, and exact server pinning
- Quick connect, searchable location list, status, disconnect, account removal
- Strict WireGuard kill switch, flexible mode, custom DNS for both engines, preferred country
- Autoconnect, launch with Windows, diagnostics, rollback on tunnel failure
- Multi-user active-tunnel ownership persisted across service restarts
- No telemetry, advertising identifier, crash upload, or proprietary SDK

## Requirements

- Windows 10 22H2 or Windows 11, x64
- Visual Studio 2022 with the C++ desktop workload
- CMake 3.24+, Ninja or MSBuild
- Qt 6.8+ (`Core`, `Network`, `Concurrent`, `Widgets`, `Test`)
- At least one tunnel engine: [WireGuard for Windows](https://www.wireguard.com/install/) or [OpenVPN Community 2.6+](https://openvpn.net/community-downloads/)
- Active NordVPN subscription and a manual Nord access token

Nord's support documentation notes that its proprietary Windows app can prevent
a third-party OpenVPN adapter from initializing. Uninstall that app before using
OpenNord's OpenVPN engine; WireGuard for Windows and OpenVPN Community remain
independent prerequisites.

## Build

From a Visual Studio developer PowerShell with `Qt6_DIR` configured:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix staging
& "$env:Qt6_DIR\..\..\..\bin\windeployqt.exe" --release staging\OpenNord.exe
& "$env:Qt6_DIR\..\..\..\bin\windeployqt.exe" --release staging\OpenNordService.exe
```

For the repository's MinGW cross-toolchain, `cmake --install` also deploys the
required Qt plugins and recursive MinGW runtime DLL dependencies.

Install the service from an elevated terminal for development:

```powershell
staging\OpenNordService.exe --install
staging\OpenNord.exe
```

For service debugging, stop the installed service and run
`OpenNordService.exe --console` from an elevated terminal.

The setup registers `OpenNordService` for automatic startup and configures
SCM restart recovery. The GUI checks SCM state before every RPC request and
attempts to restart a stopped service when the current user has permission.
While the GUI is running it also provides a Windows notification-area icon.
Closing the window keeps OpenNord in the notification area; its context menu
can open the window or start, stop, and restart the service. Service control
commands request administrator permission through Windows UAC.
Logs never include access-token contents and are written to:

- `%LOCALAPPDATA%\OpenNord\gui.log`
- `%ProgramData%\OpenNord\service.log`

## Package

After deploying Qt DLLs into `staging`, build the NSIS installer:

```powershell
makensis /DSTAGING="$PWD\staging" installer\OpenNord.nsi
```

The installer requires elevation only to install the service. The GUI manifest
uses `asInvoker` and normally runs without administrator privileges.

## Scope

This repository implements standard NordLynx and OpenVPN UDP/TCP connection paths. It is
not yet feature parity with the full Nord product. Obfuscated OpenVPN servers,
Meshnet, dedicated IP/server support, per-process split tunneling, Threat
Protection filtering, tray mode, ARM64 packaging, and signed updates remain
roadmap work and are not presented as available features.

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) and
[`SECURITY.md`](SECURITY.md) before changing service, IPC, routing, DNS, or
credential code.

## License

GPL-3.0-only. Qt is dynamically linked under its applicable open-source terms;
WireGuard for Windows and OpenVPN Community are separately installed upstream prerequisites.
