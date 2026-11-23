# Security and integrity

Do not commit credentials, private contact information, or unlicensed recordings.
Treat instrument profiles, measurement manifests, issue text, and imported research
as untrusted data. Validate bounds and paths before opening files or allocating.
Never execute shell commands obtained from a profile or measurement document.

The project automation is local and deterministic. It does not contact an AI API,
run agents, upload datasets, deploy software, or push changes automatically. Its
locks coordinate cooperating tools in one checkout; they are not a security
boundary against a user who can edit the repository directly.

For a sensitive vulnerability, use a private reporting channel provided by the
repository owner if one is available. Do not publish secrets in an issue. Public
reports for non-sensitive robustness defects should include a minimal reproducer,
affected revision, environment, expected behavior, and observed result.
