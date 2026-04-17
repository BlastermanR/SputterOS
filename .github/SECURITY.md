# Security Policy

## Supported Scope

This repository contains the reusable SputterOS control library, its host-native tests, and example projects.

SputterOS is designed as a framework for integration into user-owned embedded systems.

## Important Security Disclaimer

SputterOS currently makes **no guarantees of security**.

Due to its highly configurable nature and deployment-specific integration model,
security properties depend on how each integrator configures hardware,
software, networking, credentials, and operations.

SputterOS does **not** provide a complete end-to-end security solution.

Security of deployment, system hardening, network exposure, credential handling,
secure boot, update signing, key management, and operational controls remains
primarily the responsibility of the integrator/operator.

In short: if you deploy SputterOS in your product or lab system, you own the
security posture of that final integrated system.

## Reporting a Vulnerability

Avoid posting sensitive exploit details in public GitHub issues.

There is currently **no dedicated private security reporting channel** for this
repository.

Until a private intake process is established, maintainers cannot guarantee
timely handling of unsolicited vulnerability reports.

If you are integrating SputterOS into a production or lab system, route
security review and incident handling through your own organization's security
process.

## Response Process

Maintainers will:
- Review security-related feedback on a best-effort basis
- Prioritize fixes according to maintainer availability and project scope

## Safe Harbor

We support good-faith security research and responsible disclosure.
Please avoid privacy violations, service disruption, or data destruction while testing.
