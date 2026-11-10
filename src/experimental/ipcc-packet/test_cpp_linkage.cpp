/*
 * test_cpp_linkage.cpp - C++ linkage check for the C adapter.
 *
 * The header must be usable from C++ and the C symbols must link with C
 * linkage (no name mangling). This file contains no test logic; it only
 * proves that the header compiles as C++ and that the C implementation links.
 */
#include "ipcc_packet_abi.h"

#include <cstdint>
#include <cstring>
#include <cstdio>

int main()
{
    std::uint8_t req[IPCC_PKT_WIRE_16];
    ipcc_pkt_limits lim;
    ipcc_pkt_result res;

    ipcc_pkt_limits_mcb_incoming(&lim);

    if (ipcc_pkt_encode_read_frame(req, IPCC_PKT_SEL_MCB_IN, 64u,
                                    reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x20000000u)),
                                    4096u, &lim) != 0) {
        std::printf("encode failed\n");
        return 1;
    }
    if (ipcc_pkt_decode_read_frame(req, 0, IPCC_PKT_SEL_MCB_IN, 64u, &res) != IPCC_PKT_ERR_UNCHANGED_OUTPUT) {
        std::printf("decode failed\n");
        return 1;
    }
    std::printf("c++ linkage ok, %d checks\n", 3);
    return 0;
}
