# Prepared three-register diagnostic

**Approved once and executed successfully:** see [the runtime snapshot](../../../reverse-engineering/archive/IMAGING_CONFIG_OBSERVATION_20261016.md). The temporary helper was removed. No capture or device write occurred.

See [the proposed experiment](../../../reverse-engineering/archive/IMAGING_CONFIG_READ_PROPOSAL_20261016.md) for its exact scope and original-byte review. A separate user approval authorized the completed single register snapshot; it does not authorize capture, writes, extra addresses or repeat invocations.

Build with `sh tools/probes/mmio-config-reader/build.sh`. The 4216-byte static ARM executable accepts only `ssif-config`. It maps only the4096-byte page at20800000 read-only and performs three aligned volatile loads at20800024/20800340/2080034c, then unmaps/closes and prints the original values. It has no arbitrary address interface, register writes, status/acknowledgement reads, cache operations, camera command or capture function. No arguments exits2 before opening `/dev/mem`.

Reviewed executable SHA256: `95817293277899509a87270d02a246870b7986604e8f6e425e471ef50640cf22`. Parent rebuild matches byte-for-byte. Mocked host tests validate exact load set, page/permissions, arguments, bytes and cleanup, but cannot establish live hardware behavior. Ordinary firmware reads of these words support this limited proposal; the successful snapshot proves access at that moment, while future mapping or device-clock conditions remain unverified.
