#ifndef DCB_H
#define DCB_H

#include <stdint.h>
#include "dext_adamnet.h"

/* Original EOS Device Control Block layout used by the ADAM master. */
#define DCB_SIZE             21u
#define DCB_CMD_STATUS       1u
#define DCB_CMD_RESET        2u
#define DCB_CMD_WRITE        3u
#define DCB_CMD_READ         4u

#define DCB_STATUS_COMPLETE  0x80u
#define DCB_STATUS_FAILED    0x03u
#define DCB_STATUS_BAD_SIZE  0xE0u
#define DCB_STATUS_ILLEGAL   0xE2u

/*
 * Execute the currently supported DCB operations. The caller retains the Z80
 * buffer address; a successful READ leaves its data in the ADAMnet disk cache.
 */
adamnet_result_t dcb_execute(
    const uint8_t *request,
    uint8_t *response,
    uint8_t *block_checksum);

#endif
