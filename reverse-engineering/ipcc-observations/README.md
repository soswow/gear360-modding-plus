# Offline IPCC observation decoders

This is the reviewed integration of the useful parts of MiniMax's
`decode_lease.py` and `decode_structures.py` from snapshot `a18b965`.
It reads retained local text and performs no camera communication, mapping,
pointer following or capture.

```sh
python3 reverse-engineering/ipcc-observations/decode_logs.py frames /path/to/log
python3 reverse-engineering/ipcc-observations/decode_logs.py objects /path/to/log
python3 -m unittest discover -s reverse-engineering/ipcc-observations -v
```

Output is one JSON record per decoded observation. The frame parser accepts
both `T ... ring A W ...` and `T ... A W ...` log formats. It checks that the
printed bytes contain the declared inner frame, reconciles the body and inner
lengths, and separates extra printed bytes from the body. Truncated, malformed
or inconsistent records fail with a diagnostic and exit status 2. Output before
an error may contain earlier successfully parsed records.

Object addresses are decimal as printed by the helper; `0x` explicitly selects
hexadecimal. `FOLLOW` source-word indexes stay separate from target addresses.
The object parser checks the declared byte extent and prints opaque words with
offset-independent values. It assigns no width, height, stride, plane, pointer,
pool or lease meaning to arbitrary structures.

The original decoders could read helper decimal addresses as hexadecimal,
misread the ring label, accept partial frame bodies and attach image-object
field names without proving the object's type. Those behaviors are corrected.
Synthetic regression tests cover these failures. Retained records with fewer
printed bytes than their declared length are intentionally refused.

Parsing establishes text/layout consistency only. It does not establish an
immutable packet copy, lossless observation, request association, buffer
ownership or sensor provenance. See the
follow-up review (not published: `../../output/decompile/rea/pass-minimax-followup-review-20261023/parent-review.md`).
