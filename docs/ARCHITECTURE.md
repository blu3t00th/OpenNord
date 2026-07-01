# Architecture

OpenNord follows the separation used by `nordvpn-linux`: the desktop UI is a
client, while a privileged background component owns account state, network
state, and failure recovery.

## Linux-to-Windows mapping

| `nordvpn-linux` responsibility | OpenNord Windows implementation |
| --- | --- |
| Flutter GUI and gRPC repositories | Qt Widgets GUI and framed named-pipe RPC |
| `nordvpnd` privileged daemon | `OpenNordService` LocalSystem service |
| Unix socket permissions and daemon authenticator | Local-only pipe, SDDL ACL, client impersonation, SID-scoped state |
| `core.SimpleClientAPI` | `NordApiClient` using Qt Network |
| `response.NordValidator` | CNG RSA-SHA256 verifier for Nord `X-*` headers |
| access-token and VPN credential session stores | SID-indexed DPAPI machine encryption with System/Administrators-only ACLs |
| `PickServer` and recommendation endpoint | service-side recommendation query and server-ID revalidation |
| NordLynx kernel/userspace implementations | official WireGuard for Windows and WireGuardNT tunnel service |
| OpenVPN config template and management client | signed Nord per-server profile, local config hardening, and localhost OpenVPN management client |
| nftables kill-switch rules | WireGuard for Windows strict `/0` firewall semantics |
| netlink routes and DNS setters | WireGuard tunnel service configuration |
| networker ordered setup and `failureRecover` | validate → persist protected config → install service → await running; rollback on every failure |

## Processes

### `OpenNord.exe`

Runs as the interactive Windows user. It displays state and sends a small RPC
surface to the service. It never receives the stored NordLynx private key or
VPN service password. UI-supplied server metadata is not trusted; connect calls
contain only a server ID from a list cached by the service.

### `OpenNordService.exe`

Runs as LocalSystem. It validates Nord API signatures, stores account material,
selects servers, writes protected tunnel configuration, and controls either the
official WireGuard tunnel service or an OpenVPN Community child process. The active tunnel owner SID is persisted
with a System/Administrators-only ACL to preserve multi-user ownership after a
service restart.

## RPC protocol

The named pipe is `\\.\pipe\OpenNord.Service.v1`. Each connection carries one
little-endian length-prefixed JSON request and response. Frames are limited to
1 MiB. Remote clients are rejected by the kernel. The service impersonates the
pipe client to obtain its SID before dispatch.

## Connection transaction

1. Load and decrypt the caller's SID-scoped session.
2. Select or revalidate a server supporting the saved technology and transport.
3. Validate the server IP, hostname, and technology-specific key/profile material.
4. Generate a full-tunnel configuration and validate every DNS address.
5. Write the configuration under `%ProgramData%\OpenNord` and remove inherited ACLs.
6. Install the WireGuard tunnel service, or launch OpenVPN in a kill-on-close Job Object.
7. Poll SCM for WireGuard, or authenticate the password-protected localhost management channel and wait for `CONNECTED,SUCCESS`.
8. Persist the owning SID and publish `connected` state.

Any failure after step 5 terminates the selected engine and removes secret-bearing
configuration. OpenVPN receives service credentials only through its management
channel; they are never written to the profile or process command line.

The Linux client builds OpenVPN profiles from a signed CDN XSLT template and lets
its shared networker own routing. On Windows, OpenNord downloads Nord's signed
per-server profile, strips runtime-sensitive directives, inserts the API-selected
endpoint and transport, and preserves OpenVPN's Windows routing behavior.

## Deliberate differences

Standard OpenVPN UDP/TCP and NordLynx are supported. Meshnet, dedicated servers,
OpenVPN obfuscation, post-quantum mode, pause, file sharing, and
Threat Protection are separate subsystems and are not represented as working
controls in this GUI.
