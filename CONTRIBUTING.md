# Contributing

Contributions are accepted under GPL-3.0-only.

1. Do not reverse-engineer, decompile, or copy source from Nord's proprietary Windows application.
2. Reuse only compatible open-source code and document its origin and license.
3. Do not commit tokens, credentials, tunnel configurations, user SIDs, or unredacted logs.
4. Keep UI code outside the LocalSystem service. Privileged code needs a concrete security reason and tests.
5. Treat IPC, DPAPI, ACL, CNG, SCM, tunnel, route, DNS, installer, and update changes as security-sensitive.
6. Validate caller-controlled values again in the service even if the GUI validates them.
7. Run `cmake --build`, `ctest`, and the Windows CI workflow before requesting review.

