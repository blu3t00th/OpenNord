# Windows releases

Build from a clean checkout with a trusted compiler and Qt installation. Never
package the old committed `package` or `cmake-build-*` directories from Git
history. Do not commit executables, DLLs, compiler probes, CMake caches, or
release archives. The CI workflow installs Qt 6.8.3 on both supported build
hosts, runs tests, and produces explicitly unsigned development artifacts.

## Build and stage

Use the README build steps in a Visual Studio developer PowerShell, run all
tests, and install to a fresh staging directory. Run `windeployqt --release`
for both executables and check that each command succeeds. Copy `LICENSE`,
`README.md`, and `SECURITY.md` into staging. The packaged application needs the
Windows platform plugin and Schannel TLS backend as well as Qt runtime DLLs.
Keep third-party publisher signatures intact.

## Package

With PowerShell 7 and NSIS 3.09 or newer on `PATH`:

```powershell
./scripts/Package-Windows.ps1
```

The script validates required payload files, creates an exact uninstall file
list, builds the installer, and creates a ZIP and `SHA256SUMS.txt` in `dist`.
There is a second checksum manifest inside the payload. Hashes are calculated
after any signing. The script rejects links/junctions in staging and stops if
a packaging or signing tool fails. Its unsigned mode is for development and
does not claim that a release is verified or trusted by Windows.

## Sign a public release

Use a code-signing certificate issued by a trusted certificate authority and
accessible through the current user's Windows certificate store. A hardware
token or managed signing provider may require its own setup. No private key or
PFX password belongs in this repository or on a command line.

The following requires Windows SDK `signtool.exe` on `PATH`; replace the example
thumbprint with the certificate's actual 40-character thumbprint:

```powershell
./scripts/Package-Windows.ps1 -CertificateThumbprint 'YOUR_40_CHARACTER_CERTIFICATE_THUMBPRINT'
```

This signs and verifies the GUI and service, asks NSIS to sign and verify its
embedded uninstaller, then signs and verifies the final installer. It uses
SHA-256 file and RFC 3161 timestamp digests. The timestamp server can be changed
with `-TimestampUrl`. `-SignTool` and `-MakeNsis` accept explicit tool paths.
Only OpenNord's executables are signed; dependency signatures are preserved.
The certificate selector `/sha1` identifies the certificate, while `/fd SHA256`
selects the actual file digest algorithm. See Microsoft's
[SignTool documentation](https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool).

Signing is optional in CI because release credentials are not configured in
this repository. A signature is not a malware verdict or a guarantee of
SmartScreen reputation. Do not publish an artifact with an unresolved Defender
detection; follow the process in `SECURITY.md`.

## Validate and publish

1. Scan the final signed artifacts with up-to-date Defender protection enabled.
2. In a disposable Windows test machine, test fresh installation, upgrading,
   and uninstalling as a standard user with an administrator elevation prompt.
   Close OpenNord from its tray menu before upgrading. Verify that the service
   stops before files are removed and that unrelated files in the installation
   folder survive uninstall.
3. Complete the VPN/network and multi-user checks in `SECURITY.md`.
4. Publish the final ZIP, installer, `SHA256SUMS.txt`, and corresponding source
   revision together. A checksum detects changes only when compared with a
   checksum obtained from a trusted source.

For an individual downloaded artifact, calculate its checksum with:

```powershell
Get-FileHash ./OpenNord-Setup.exe -Algorithm SHA256
Get-AuthenticodeSignature ./OpenNord-Setup.exe
```

Compare the hash with the release's checksum manifest and inspect the expected
publisher on a signed public release. Preserve the exact flagged file and
checksum for Microsoft analysis if a detection occurs; do not rebuild merely
to obtain a different antivirus result.
