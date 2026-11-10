# Still-request component

This pure C component encodes a 48-byte request and calls an already initialized,
caller-supplied `CaptureStillImage` function. It performs no device I/O, context
initialization, completion handling or RAW acquisition.

The legacy `still_request_encode` and `still_request_dispatch` keep command
`0x01010001`, with words `+0x04` and `+0x08` both equal to one. The extended
entry points accept a `still_request_options` structure; null or all-zero
options preserve those bytes. Each zero field selects its default.

`still_request_options_device_shutter` selects `0x01010016`, Cap=Max=1, the
variant in the retained 22 October software-submit run. Native review confirms
that this variant reaches adapter command `0x0201` with a 48-byte body. The
library logs the words at `+0x04` and `+0x08` as Cap and Max. Their full RTOS
semantics, supported ranges and multi-capture behavior are unproved. Refusing
Max below Cap is this component's local policy.

The historical "device shutter" name for `0x01010016` mislabels the command:
the firmware's STL command table identifies it as `E_CAP_CMD_STL_LOCK3A_NORMAL`
(index 22), the mm capture type 21 (LOCK3A_CAPTURE) mapped by the library's
`FUN_00042e18`. It is a lock-3A-then-capture still, not a shutter mechanism;
the plain still is `0x01010001` (`STL_NORMAL`). See
next-edge/still-path/CAPTURE-TYPE-SELECTION.md (not published: `../../../output/decompile/rea/pass-liveview-contract-20261030/next-edge/still-path/CAPTURE-TYPE-SELECTION.md`).

```c
still_request_options options;
still_request_options_device_shutter(&options);
still_request_result result = still_request_dispatch_ex(submit, &storage,
                                                       &options);
```

The command passed to `submit` matches the encoded command. `submit` must copy
or consume the aligned body synchronously. A nonzero vendor result remains
indeterminate and must not trigger an automatic retry. Acceptance does not
establish capture completion.

The extended encoder is wire construction, not validation of arbitrary commands
against the firmware. The recorded live run used Cap=Max=1 with the stock app
still running and handling the result. It did not demonstrate an independent
capture engine or caller-owned sensor RAW.

Review and evidence: parent-review.md (not published: `../../../output/decompile/rea/pass-minimax-review-20261023/parent-review.md`),
[submit contract](../../../reverse-engineering/facts/contracts/submit.yaml). The observed 56-byte envelope
is a fixed-vector test in `../mcb-frame/tests/test_still_envelope.c`.

Build with `sh src/lib/still-request/build.sh`; `CHECK_ARM=1` also
compiles the component for ARMv7. Run `mcb-frame/build.sh` for the envelope test.
