# Bounded REA bridge and exported-evidence reader

Four small files for the native side of the Gear 360 investigation. None of
them opens the camera, the device or the network, and none writes outside its
own directory.

| file | what it does |
| --- | --- |
| `rea-edge.py` | runs one bounded read-only REA/Ghidra pass over a query document |
| `read-exported-evidence.py` | reads an exported evidence file without a live session |
| `show-ev.py` | prints the readable payload of one evidence record |
| `test_rea_edge.py` | synthetic tests: fixtures plus a localhost fake MCP server |

## `rea-edge.py` - a process-local REA/Ghidra bridge

Starts the REA MCP server, opens a pinned artifact against a pinned snapshot,
runs a queue of **read-only** queries and closes the session again.

```
python3 rea-edge.py queries.json            # the carrier, as before
python3 rea-edge.py queries.json library    # or carrier | app | agent | bridge | shell
```

The query file is a JSON list:

```
{"tool": "procedure_pseudo_code", "arguments": {"procedure": "0x80280570"},
 "save": "01-pseudo.json"}
```

### Runtime and artifacts come from the environment

Nothing is downloaded, installed or written to a client configuration. The
run is refused, clearly, when the runtime is not already there:

| variable | meaning |
| --- | --- |
| `REA_NODE` | node binary that runs the REA MCP launcher |
| `REA_LAUNCHER` | path to the REA MCP launcher (`rea.mjs`) |
| `JAVA_HOME` | JDK 21 home used for Ghidra |
| `GHIDRA_INSTALL_DIR` | Ghidra installation directory |
| `GEAR360_RESEARCH_ROOT` | repository root holding the pinned artifacts |

`GEAR360_RESEARCH_ROOT` defaults to the repository root detected above this
script (looked for by `dump/extracted` or `output/decompile/rea`), so a
worktree can point at the retained main artifacts. The pinned paths are
relative to that root; the pinned hashes and the address-space notes are
unchanged:

* `carrier` - the seeded analysis ELF, `8321a211...`. **Code VA is the
  original offset plus `0x80000000`.** The original RTOS it was built from is
  `cfe69efe...`.
* `agent`, `library`, `bridge`, `shell` - shared objects, so the Ghidra
  address is the ELF virtual address plus `0x10000`.
* `app` - the ELF virtual address, with no shared-library bias.
* `bridge` pins `libmulticore-bridge.so` (`9129761c...`); `shell` pins
  `libshell-command.so` (`0fd079a9...`). The shared-library bias also
  applies to their data-table coordinates. These are offline analysis targets.

Do not read a default-space data address as a live pointer without applying
the initialized-copy mapping.

### Each run owns a fresh directory

A run writes into `runs/<stamp>-<pid>-<random>/`, which it creates for itself
and never reuses. Retained evidence from an earlier run is therefore never
overwritten, and a refused run leaves nothing behind at all. The directory
holds the replies, `tools.json`, `open.json`, the exported evidence bundle,
the session snapshot and the lifecycle receipts.

### Arguments are checked against the connected schemas

The input schemas of the connected tools are pinned from the real `tools/list`
inventory, and the live inventory is used as well when it carries schemas, so
a server change is caught rather than guessed at. The check is hand-written
over the keywords those schemas use - no schema library is downloaded.

* **Admitted surface.** `procedure_pseudo_code`,
  `read_function_instructions`, `procedure_callers`, `procedure_callees`,
  `xrefs`, `read_bytes`, `search_procedures`, `search_strings`,
  `search_bytes`, `resolve_containing_procedure`, `procedure_address`,
  `inspect_native_instruction`. `analyze_function` is not admitted: a
  per-function dossier is not a bounded read.
* **Search bytes.** `pattern` (hex byte pairs), an explicit `start` and an
  explicit `end`, and `limit` up to 100. `max_results` and `length` are not
  arguments of this tool. Without an explicit range the query is refused,
  so that a null result can never be read as "no hits".
* **Read bytes.** `address` plus an explicit `length` of at most 4096 bytes.
  An unbounded raw byte read is not admitted.
* **Target switch refused.** `document` is the provider's own target-switch
  argument and is refused, as are `path`, `snapshot_path`, `provider_id` and
  the other target keys, in an argument or in the query document. One target
  per run.
* **Save path confined.** A reply is only written inside the run directory.
  An absolute path, a `..` segment or a path that resolves outside it is
  refused before execution.

### Subjects are verified

The actual evidence envelope carries its subject as `local_path` plus
`digest.sha256`. Every subject is verified against the declared target hash
and against the target file itself: a query-declared subject before that
query runs, and a subject returned by the provider before its reply counts as
evidence. A subject that does not verify is refused, and the refusal is
written next to the reply. The flat legacy subject (`path` plus `sha256`) is
still understood, but an actual envelope is never quietly reported as
unverified.

### The session is always closed

`export_evidence_bundle(path)` writes the evidence bundle, then
`close_binary(snapshot_path)` closes the session. Both run in a `finally`, on
a query error and on an open error alike, and both write into the fresh run
directory with overwrite off. The provider's reply to each call is kept as a
receipt in its own file under `receipts/`; the receipt is never written over
the exported bundle. The first error is kept and raised unchanged, and any
cleanup problem - including a provider `isError` - is reported next to it.
Only the process this script spawned is terminated.

### Reading one reply

```
python3 show-ev.py evidence/01-pseudo.json
python3 show-ev.py evidence/03-search.json 0x8028
python3 show-ev.py --raw evidence/03-search.json
```

A byte search is carried in `matches` and is printed as matches. An empty
`matches` list means the provider searched a range and found nothing; a null
result means no result was recorded at all, so the value may or may not be
there. The two are not the same, and `--raw` prints the provider's original
record without touching the evidence file.

## `read-exported-evidence.py` - read an exported evidence file

The parent pass exported 1811 evidence records from its closed REA session.
This reads them without opening a session, so that a claim can be re-checked
against the same records instead of against a new analysis.

```
python3 read-exported-evidence.py 0x80280570            # every record for that address
python3 read-exported-evidence.py 0x80280570 pseudo     # only the pseudocode record
```

The export is resolved under `GEAR360_RESEARCH_ROOT` at
`output/decompile/rea/pass-4d2deb0-review-20261023/rtos-evidence.json`; a
path may also be given as a third argument. Both record shapes are read: the
actual envelope (`evidence_id`, `subject`, `parameters`, `limitations`,
`value`) and the older one (`operation`, `parameters`, `normalized_result`,
`raw_result`). Every evidence subject that names a file and a digest is
verified before its record is shown; a record whose subject does not verify
is not shown as evidence, and a record whose subject file is simply not
present locally is marked absent instead.

## Why these exist

The first version of the evidence reader was written in the pass that
produced this bridge and it was **wrong**: it split a record on the first
`" B"`, which confuses ring B's name with the hex-preview marker, and it
walked the whole 64-byte preview as if it were frames, so stale ring bytes
could be read as current events. The reviewed decoder for that dialect is
`reverse-engineering/observer-trace/parse_frames.py`, and it is the
authority on the record format, the published extent of a dump and the
direction of a route (`03 00` is Linux to RTOS, `00 03` is RTOS to Linux).

The bridge is a copy of the shape the parent's broker used, with the
difference that everything lives in this pass's own directory and its own
spool. The parent's own spool path is read-only to this work: nothing there
is written, deleted or recreated.


Parent integration accepts only a freshly spawned stdio server. Existing MCP
network peers are refused because their active target is not owned by this run.
Evidence can arrive as MCP structuredContent or JSON text content. The full
Evidence shape uses raw_result/normalized_result; the compact parent spool
shape also remains readable. A fresh run directory retains the server log,
bundle, snapshot and separate lifecycle receipts. No camera operation is made.

Default run artifacts are retained under the repository’s ignored `output/rea-edge-runs/`, each in a fresh directory. No snapshot or evidence bundle is committed.
