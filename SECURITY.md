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
- Secret-bearing files are created with a protected System/Administrators DACL before any bytes are written, then atomically renamed through their original handle. No temporary secret file inherits a public read permission.
- The GUI verifies the named-pipe server process against the running SCM service before sending credentials. Interactive clients cannot create additional pipe server instances.
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

## Windows Defender detections

A named malware detection is different from a SmartScreen reputation warning.
Keep the affected file quarantined while investigating. Record the threat
name, relative file path, Defender intelligence version, and SHA-256 hash if
the file can be inspected without restoring it. Do not include credentials or
session files in a report.

The repository previously contained CMake compiler probes, build caches, and
prebuilt application/dependency binaries. A compiler-identification executable
in the committed release build tree was detected as
`Trojan:Win32/Bearfoos.B!ml` during a checkout review. Those generated outputs
are removed from the current source tree; historical Git revisions still
contain them. This does not establish whether the detection was a false
positive. Rebuild from reviewed source using a clean toolchain instead of
running old binaries from Git history.

For a suspected incorrect detection, the maintainer can submit the exact
affected release file through the
[Microsoft Security Intelligence submission portal](https://www.microsoft.com/en-us/wdsi/filesubmission)
as a software developer and track Microsoft's determination. Microsoft explains
the process in its [software developer FAQ](https://learn.microsoft.com/en-us/defender-xdr/developer-faq).
Do not disable Defender or add exclusions to make a build pass.

Authenticode signatures establish publisher identity and detect changes after
signing; they do not certify the absence of malware and do not guarantee an
antivirus or SmartScreen result. Follow [`docs/RELEASING.md`](docs/RELEASING.md)
for clean packaging, optional signing, and checksums. Unsigned CI output is
identified as a development artifact.
