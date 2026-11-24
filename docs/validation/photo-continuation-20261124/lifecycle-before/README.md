# Native removal observations before the repair

These records preserve worker 030's accepting SDK-host probes, including the
normal close and the reproduced removal from inside `beginEdit`. Outcomes and
input provenance are unchanged. The two large trace logs are stored as gzip
files; `compressed-logs.json` binds both their original and compressed bytes.

For example, restore the original trace without altering the archive:

```sh
gzip -dc reentrant01/editorhost.log.gz > build-reentrant01-editorhost.log
```

The untraced `reentrant02` repetition remains separate. These are original control
and failure observations, not positive acceptance for the subsequent repair.
