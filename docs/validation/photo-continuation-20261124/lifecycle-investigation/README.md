# Intermediate lifecycle observations

These original results are preserved, including failures and the incomplete SDK-host
probes. The three long event traces are gzip compressed without changing their
decoded bytes; `compressed-logs.json` binds both representations. The original
`files.json` hashes refer to the decompressed originals. For example:

```sh
gzip -dc control-editorhost.log.gz > build-control-editorhost.log
```

Passing lifecycle acceptance is recorded separately in `../lifecycle-after/`.
