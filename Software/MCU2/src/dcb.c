#include "dcb.h"

/* Byte offsets from the original 21-byte ADAM Device Control Block. */
#define DCB_OFS_COMMAND       0u
#define DCB_OFS_BUFFER_LEN    3u
#define DCB_OFS_BLOCK_NUMBER  5u
#define DCB_OFS_ADDRESS      16u
#define DCB_OFS_MAX_LENGTH   17u

static uint16_t read_le16(const uint8_t *value)
{
    return (uint16_t)value[0] | ((uint16_t)value[1]<<8);
}

static uint32_t read_le32(const uint8_t *value)
{
    return (uint32_t)value[0] |
        ((uint32_t)value[1]<<8) |
        ((uint32_t)value[2]<<16) |
        ((uint32_t)value[3]<<24);
}

adamnet_result_t dcb_execute(
    const uint8_t *request,
    uint8_t *response,
    uint8_t *block_checksum)
{
    adamnet_result_t result=ADAMNET_RESULT_BAD_ARGUMENT;
    uint8_t index;
    uint8_t address;
    uint8_t command;

    if (request==0 || response==0 || block_checksum==0)
        return ADAMNET_RESULT_BAD_ARGUMENT;

    for (index=0u; index<DCB_SIZE; ++index) response[index]=request[index];
    *block_checksum=0u;
    command=response[DCB_OFS_COMMAND];
    address=(uint8_t)(response[DCB_OFS_ADDRESS]&ADAMNET_ADDRESS_MASK);

    if (address==0u) {
        response[DCB_OFS_COMMAND]=DCB_STATUS_FAILED;
        return ADAMNET_RESULT_BAD_ADDRESS;
    }

    if (command==DCB_CMD_RESET) {
        result=dext_adamnet_soft_reset(address);
        response[DCB_OFS_COMMAND]=(result==ADAMNET_RESULT_OK)
            ? DCB_STATUS_COMPLETE : DCB_STATUS_FAILED;
    } else if (command==DCB_CMD_STATUS) {
        dext_probe_result_t probe;
        result=dext_adamnet_status(address,&probe);
        if (result==ADAMNET_RESULT_OK) {
            /* The original master stores four status bytes at offsets 17-20. */
            for (index=0u; index<4u; ++index)
                response[DCB_OFS_MAX_LENGTH+index]=probe.reply[index+1u];
            response[DCB_OFS_COMMAND]=DCB_STATUS_COMPLETE;
        } else {
            response[DCB_OFS_COMMAND]=DCB_STATUS_FAILED;
        }
    } else if (command==DCB_CMD_READ || command==DCB_CMD_WRITE) {
        uint16_t block_length=0u;
        const uint16_t buffer_length=read_le16(
            &response[DCB_OFS_BUFFER_LEN]);
        const uint16_t maximum_length=read_le16(
            &response[DCB_OFS_MAX_LENGTH]);

        /* A block DCB is valid only when both declared sizes are 1,024 bytes. */
        if (buffer_length!=ADAMNET_DISK_BLOCK_SIZE ||
            maximum_length!=ADAMNET_DISK_BLOCK_SIZE) {
            response[DCB_OFS_COMMAND]=DCB_STATUS_BAD_SIZE;
            result=ADAMNET_RESULT_BAD_LENGTH;
        } else if (address<4u || address>7u) {
            response[DCB_OFS_COMMAND]=DCB_STATUS_FAILED;
            result=ADAMNET_RESULT_BAD_ADDRESS;
        } else if (command==DCB_CMD_READ) {
            result=dext_adamnet_disk_read(
                address,read_le32(&response[DCB_OFS_BLOCK_NUMBER]),
                &block_length,block_checksum);
            response[DCB_OFS_COMMAND]=(result==ADAMNET_RESULT_OK &&
                block_length==ADAMNET_DISK_BLOCK_SIZE)
                ? DCB_STATUS_COMPLETE : DCB_STATUS_FAILED;
        } else {
            result=dext_adamnet_disk_write(
                address,read_le32(&response[DCB_OFS_BLOCK_NUMBER]),
                block_checksum);
            response[DCB_OFS_COMMAND]=(result==ADAMNET_RESULT_OK)
                ? DCB_STATUS_COMPLETE : DCB_STATUS_FAILED;
        }
    } else {
        response[DCB_OFS_COMMAND]=DCB_STATUS_ILLEGAL;
        result=ADAMNET_RESULT_BAD_ARGUMENT;
    }
    return result;
}
