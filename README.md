<img width="1274" height="845" alt="{2C66F2AB-1E13-49F1-9D9B-DAA6043B418E}" src="https://github.com/user-attachments/assets/09440cfc-9677-403f-bbf6-236ebb1c7ca8" />



<img width="1276" height="847" alt="{6073C0A8-7986-4034-8C7F-168A3ABFB621}" src="https://github.com/user-attachments/assets/15eb5d6d-9aa3-471c-ab99-455043571d3c" />



OpenNord is under active development. Review the security notes and test a
release before relying on it for VPN connectivity. Login currently uses a
manual Nord access token.

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

After deploying Qt DLLs into a fresh `staging` directory, copy the documentation
and build with PowerShell 7 and NSIS 3.09 or newer:

```powershell
Copy-Item LICENSE, README.md, SECURITY.md staging
./scripts/Package-Windows.ps1
```

This creates `dist/OpenNord-Setup.exe`, a portable ZIP, and SHA-256 checksums.
The installer removes only the files that were included in its payload.
The installer requires elevation only to install the service. The GUI manifest
uses `asInvoker` and normally runs without administrator privileges.

CI artifacts are unsigned development builds. See
[`docs/RELEASING.md`](docs/RELEASING.md) for certificate signing and release
verification. Build directories and packaged executables are generated outputs;
they must not be committed to the source repository.

If Microsoft Defender detects a named threat, keep the detected file
quarantined and record the threat name and affected file from Protection
History. A detection needs investigation; signing does not prove a file is
safe or guarantee that Defender will accept it. See
[`SECURITY.md`](SECURITY.md#windows-defender-detections).

## Scope

This repository implements standard NordLynx and OpenVPN UDP/TCP connection paths. It is
not yet feature parity with the full Nord product. Obfuscated OpenVPN servers,
Meshnet, dedicated IP/server support, per-process split tunneling, Threat
Protection filtering, ARM64 packaging, and signed updates remain
roadmap work and are not presented as available features.

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) and
[`SECURITY.md`](SECURITY.md) before changing service, IPC, routing, DNS, or
credential code.

## License

GPL-3.0-only. Qt is dynamically linked under its applicable open-source terms;
WireGuard for Windows and OpenVPN Community are separately installed upstream prerequisites.
Country flags use the MIT-licensed flag-icons artwork. Its attribution is bundled
in the application's `licenses/flag-icons.txt` file.
