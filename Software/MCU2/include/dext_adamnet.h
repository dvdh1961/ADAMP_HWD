#ifndef DEXT_ADAMNET_H
#define DEXT_ADAMNET_H

#include <stdint.h>

#define ADAMNET_MAX_BLOCK 48u
#define DEXT_MAX_REPLY (ADAMNET_MAX_BLOCK+4u)
#define ADAMNET_DISK_BLOCK_SIZE 1024u

/*
 * ADAMnet command bytes use the upper nibble for the operation and the lower
 * nibble for the node address (1..15).  These values come directly from the
 * original Coleco Master 6801 source listing.
 */
#define ADAMNET_ADDRESS_MASK 0x0Fu

#define ADAMNET_MN_RESET     0x00u
#define ADAMNET_MN_STATUS    0x10u
#define ADAMNET_MN_ACK       0x20u
#define ADAMNET_MN_CLEAR     0x30u
#define ADAMNET_MN_RECEIVE   0x40u
#define ADAMNET_MN_CANCEL    0x50u
#define ADAMNET_MN_SEND      0x60u
#define ADAMNET_MN_NACK      0x70u
#define ADAMNET_MN_READY     0xD0u

/* Response types returned by an ADAMnet node. */
#define ADAMNET_NM_STATUS    0x80u
#define ADAMNET_NM_ACK       0x90u
#define ADAMNET_NM_CANCEL    0xA0u
#define ADAMNET_NM_SEND      0xB0u
#define ADAMNET_NM_NACK      0xC0u

/* Result codes used by the protocol validation helpers. */
typedef enum {
    ADAMNET_RESULT_OK=0u,
    ADAMNET_RESULT_TIMEOUT=1u,
    ADAMNET_RESULT_BAD_ADDRESS=2u,
    ADAMNET_RESULT_BAD_LENGTH=3u,
    ADAMNET_RESULT_BAD_COMMAND=4u,
    ADAMNET_RESULT_BAD_CHECKSUM=5u,
    ADAMNET_RESULT_RX_OVERFLOW=6u,
    ADAMNET_RESULT_BUS_BUSY=7u,
    ADAMNET_RESULT_BAD_ARGUMENT=8u,
    ADAMNET_RESULT_NODE_NACK=9u,
    ADAMNET_RESULT_NODE_CANCEL=10u,
    ADAMNET_RESULT_BLOCK_TOO_LARGE=11u
} adamnet_result_t;

/*
 * Timing values are expressed in milliseconds because the CH559 gateway
 * schedules transactions from its main loop.  The UART itself still runs at
 * the exact ADAMnet bit rate and is serviced by interrupts.
 */
typedef struct {
    uint16_t first_byte_timeout_ms;
    uint16_t inter_byte_timeout_ms;
} adamnet_timing_t;

typedef struct {
    uint8_t sent_command;
    uint8_t reply_length;
    uint8_t echo_ok;
    uint8_t first_rx_status;
    uint8_t line_status;
    uint8_t reply[DEXT_MAX_REPLY];
} dext_probe_result_t;

typedef enum {
    ADAMNET_DEVICE_ABSENT=0u,
    ADAMNET_DEVICE_RESETTING=1u,
    ADAMNET_DEVICE_READY=2u,
    ADAMNET_DEVICE_BUSY=3u,
    ADAMNET_DEVICE_FAILED=4u
} adamnet_device_state_t;

typedef struct {
    adamnet_device_state_t state;
    adamnet_result_t last_result;
    uint8_t status[4];
} adamnet_device_info_t;

void dext_adamnet_init(void);
void dext_adamnet_reset(void);

/*
 * Send MN_STATUS to one ADAMnet address and collect the raw response.
 * A return value of 1 means that at least one response byte was received;
 * zero means that the transaction timed out.
 */
uint8_t dext_adamnet_probe(uint8_t address, dext_probe_result_t *result);

/* Send one raw ADAMnet control byte and remove its local bus echo. */
uint8_t dext_adamnet_control(uint8_t command, dext_probe_result_t *result);

/* Diagnostic transaction that keeps the local echo visible to the caller. */
uint8_t dext_adamnet_echo_test(uint8_t address, dext_probe_result_t *result);

/* Backwards-compatible raw UART diagnostic used by the USB protocol. */
uint8_t dext_uart1_test(uint8_t mode, uint8_t value,
                        dext_probe_result_t *result);

/* Return the UART0 registers: SCON, PCON, TMOD, TH1, TL1 and PIN_FUNC. */
void dext_uart1_registers(uint8_t *registers);

/*
 * Send MN_RESET to one node.  The original Master 6801 does not expect a
 * response to this command; it only waits until its own transmitted echo has
 * been removed from the receive path.
 */
adamnet_result_t dext_adamnet_soft_reset(uint8_t address);

/*
 * Perform MN_STATUS and validate the complete six-byte status frame:
 * response byte, four status bytes and XOR checksum.
 */
adamnet_result_t dext_adamnet_status(uint8_t address,
                                     dext_probe_result_t *result);

/*
 * Run one validated command/response transaction. command_type and
 * expected_response_type contain only the upper command nibble; this function
 * adds and verifies the node address. Set expected_response_type to 0xFF when
 * the caller wants to preserve a raw response without command validation.
 */
adamnet_result_t dext_adamnet_exchange(
    uint8_t address,
    uint8_t command_type,
    uint8_t expected_response_type,
    uint8_t minimum_reply_length,
    uint8_t maximum_reply_length,
    const adamnet_timing_t *timing,
    dext_probe_result_t *result);

/*
 * Repeat a failed validated exchange a bounded number of times. retries is
 * the number of additional attempts after the first transaction.
 */
adamnet_result_t dext_adamnet_exchange_retry(
    uint8_t address,
    uint8_t command_type,
    uint8_t expected_response_type,
    uint8_t minimum_reply_length,
    uint8_t maximum_reply_length,
    const adamnet_timing_t *timing,
    uint8_t retries,
    dext_probe_result_t *result);

/* Return diagnostics for the most recently completed master transaction. */
adamnet_result_t dext_adamnet_last_result(void);
uint8_t dext_adamnet_last_attempts(void);
uint8_t dext_adamnet_bus_busy(void);
uint8_t dext_adamnet_last_stage(void);
uint8_t dext_adamnet_last_packet_reply(uint8_t *reply);

/* Scan addresses 1..15 and update the persistent in-memory device table. */
void dext_adamnet_scan_devices(uint16_t *present_mask, uint16_t *failed_mask);

/* Copy one cached device-table entry without starting a bus transaction. */
adamnet_result_t dext_adamnet_device_info(
    uint8_t address,
    adamnet_device_info_t *info);

/* Read and checksum one 1,024-byte block from an ADAMnet block device. */
adamnet_result_t dext_adamnet_disk_read(
    uint8_t address,
    uint32_t block_number,
    uint16_t *block_length,
    uint8_t *block_checksum);

/*
 * Write the completely uploaded 1,024-byte cache to one disk block. The disk
 * protocol first receives the five-byte block selector and then requests the
 * cached block through a second MN_READY/MN_SEND transaction.
 */
adamnet_result_t dext_adamnet_disk_write(
    uint8_t address,
    uint32_t block_number,
    uint8_t *block_checksum);

/* Copy a fragment from the most recently validated disk block. */
uint8_t dext_adamnet_disk_read_chunk(
    uint16_t offset,
    uint8_t *destination,
    uint8_t maximum_length);

/*
 * Receive and validate a packet of up to 1,024 bytes into the shared transfer
 * cache.  This is used when a packet cannot fit in one 64-byte USB HID report.
 * The cache is intentionally shared with disk reads to avoid reserving a
 * second 1 KiB xdata buffer on the CH559.
 */
adamnet_result_t dext_adamnet_receive_cached(
    uint8_t address,
    uint16_t *block_length,
    uint8_t *block_checksum);

/* Copy a fragment from the most recently validated cached transfer. */
uint8_t dext_adamnet_cached_chunk(
    uint16_t offset,
    uint8_t *destination,
    uint8_t maximum_length);

/* Prepare and fill the shared xRAM cache for a large outgoing packet. */
adamnet_result_t dext_adamnet_upload_begin(uint16_t block_length);
adamnet_result_t dext_adamnet_upload_chunk(
    uint16_t offset,
    const uint8_t *source,
    uint8_t length);

/* Send the completely uploaded cache as one ADAMnet MN_SEND packet. */
adamnet_result_t dext_adamnet_send_cached(uint8_t address);

/*
 * Request one data packet from a node. The complete Master 6801 handshake is:
 * MN_RECEIVE, NM_ACK, MN_CLEAR, NM_SEND packet, and MN_ACK. block_length is
 * always cleared on failure. This compatibility wrapper only returns packets
 * up to 48 bytes; larger packets remain available through the cache API.
 */
adamnet_result_t dext_adamnet_receive_block(
    uint8_t address,
    uint8_t *block,
    uint8_t *block_length);

/*
 * Send one data packet using MN_READY, MN_SEND, a 16-bit big-endian length,
 * payload, XOR checksum, and the final MN_ACK transaction cleanup.
 */
adamnet_result_t dext_adamnet_send_block(
    uint8_t address,
    const uint8_t *block,
    uint8_t block_length);

/* Send one address-qualified master control command and wait only for echo. */
adamnet_result_t dext_adamnet_send_control(
    uint8_t address,
    uint8_t command_type);

#endif
