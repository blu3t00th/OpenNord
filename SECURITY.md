# Security policy

Report credential exposure, pipe authorization bypass, signature bypass,
privilege escalation, tunnel ownership bypass, or traffic/DNS leaks privately
to the maintainers before public disclosure. Never attach a real access token,
private key, or unredacted diagnostic bundle.

## Trust boundaries

- The Qt GUI is an unprivileged and untrusted client of the service.
- The LocalSystem service is the only OpenNord process allowed to handle saved credentials or tunnel configuration.
- Named-pipe requests are accepted locally, bounded, parsed as data, and associated with an impersonated caller SID.
- Session files use machine-scoped DPAPI encryption and a System/Administrators-only DACL; the owning SID can access them only through authenticated RPC.
- Tunnel and ownership files remove inherited ACLs and allow only System and Administrators.
- Nord API and OpenVPN profile responses require TLS plus the signed `X-Digest`, `X-Accept-Before`, `X-Authorization`, and `X-Signature` headers used by the GPL Linux client.
- WireGuard for Windows and WireGuardNT are separate trusted upstream components.
- OpenVPN Community is a separate trusted upstream component. Its process is contained in a kill-on-close Job Object and its password-protected management socket binds only to localhost.
- The service strips runtime-sensitive OpenVPN directives for scripts, plugins, logs, authentication, management, endpoint, transport, and DNS, then inserts validated local values.

## Kill-switch semantics

Strict mode uses the official WireGuard for Windows `/0` firewall behavior. It
blocks non-tunnel traffic while the tunnel service is active. Flexible mode
uses split default routes and does not claim leak protection during connection
transitions. Persistent blocking while intentionally disconnected requires a
separate audited Windows Filtering Platform backend and is not claimed here.

OpenVPN enables `block-outside-dns` and blocks IPv6 through its own profile, but
OpenNord does not claim persistent or transition-safe kill-switch behavior for
OpenVPN. The GUI disables the strict kill-switch and LAN policy controls when
OpenVPN is selected.

## Release checklist

- Run Linux common tests and the full Windows build/test workflow.
- Test IPv4, IPv6, DNS, sleep/resume, network changes, service restart, and two interactive Windows users.
- Verify strict-mode behavior during tunnel failure and WireGuard process termination.
- Test OpenVPN UDP/TCP authentication, management reconnects, DNS behavior, process failure, and profile-signature rejection.
- Sign the service, GUI, and installer; publish checksums and corresponding source.
- Review Qt, Windows SDK, and WireGuard dependency changes.
