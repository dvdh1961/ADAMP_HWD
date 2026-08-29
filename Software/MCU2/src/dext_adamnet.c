#include "board.h"
#include "dext_adamnet.h"
#include "timer.h"
#include "uart0.h"

#define DEXT_REPLY_TIMEOUT   120u
#define DEXT_INTERBYTE_MS    2u
#define DEXT_ECHO_TIMEOUT    2u
#define DEXT_RAW_RESPONSE    0xFFu
#define DEXT_BLOCK_TIMEOUT   250u
#define DEXT_DISK_POLL_MS    4u
#define DEXT_DISK_POLLS      40u

static volatile bool adamnet_busy;
static volatile adamnet_result_t adamnet_last_result=ADAMNET_RESULT_OK;
static volatile uint8_t adamnet_last_attempts;
static volatile uint8_t adamnet_last_stage;
static uint8_t adamnet_last_packet_bytes[DEXT_MAX_REPLY];
static uint8_t adamnet_last_packet_length;
static adamnet_device_info_t adamnet_devices[ADAMNET_ADDRESS_MASK];
static __xdata uint8_t adamnet_disk_block[ADAMNET_DISK_BLOCK_SIZE];
static uint16_t adamnet_disk_length;
static uint8_t adamnet_disk_checksum;
static uint16_t adamnet_upload_expected;
static uint16_t adamnet_upload_written;

static void clear_result(uint8_t command, dext_probe_result_t *result)
{
    uint8_t index;

    result->sent_command=command;
    result->reply_length=0u;
    result->echo_ok=0u;
    result->first_rx_status=0u;
    result->line_status=0u;
    for (index=0u; index<DEXT_MAX_REPLY; ++index) result->reply[index]=0u;
}

static bool acquire_bus(void)
{
    bool acquired=false;

    __critical {
        if (!adamnet_busy) {
            adamnet_busy=true;
            acquired=true;
        }
    }
    return acquired;
}

static void release_bus(void)
{
    __critical {
        adamnet_busy=false;
    }
}

/* Keep the cached device table synchronized with every validated status poll. */
static void cache_status_result(uint8_t address, adamnet_result_t result,
                                const dext_probe_result_t *probe)
{
    uint8_t index;
    adamnet_device_info_t *device;

    if (address<1u || address>ADAMNET_ADDRESS_MASK) return;
    device=&adamnet_devices[address-1u];
    device->last_result=result;
    if (result==ADAMNET_RESULT_OK && probe!=0) {
        device->state=ADAMNET_DEVICE_READY;
        for (index=0u; index<4u; ++index)
            device->status[index]=probe->reply[index+1u];
    } else {
        device->state=(result==ADAMNET_RESULT_TIMEOUT)
            ? ADAMNET_DEVICE_ABSENT : ADAMNET_DEVICE_FAILED;
        for (index=0u; index<4u; ++index) device->status[index]=0u;
    }
}

static adamnet_result_t validate_address(uint8_t address)
{
    return (address>=1u && address<=ADAMNET_ADDRESS_MASK)
        ? ADAMNET_RESULT_OK : ADAMNET_RESULT_BAD_ADDRESS;
}

/* Send a control byte while the caller already owns the ADAMnet bus. */
static adamnet_result_t control_echo_locked(uint8_t command)
{
    uint32_t deadline;
    uint8_t received;

    uart0_clear_rx();
    uart0_write_byte(command);
    deadline=timer_millis()+DEXT_ECHO_TIMEOUT;
    do {
        /* Drain an optional local echo; this hardware normally has none. */
        (void)uart0_read_byte(&received);
    } while ((int32_t)(timer_millis()-deadline)<0);
    return uart0_rx_overflowed()
        ? ADAMNET_RESULT_RX_OVERFLOW : ADAMNET_RESULT_OK;
}

/*
 * Transmit a complete packet, remove a byte-for-byte local echo when present,
 * and retain the short control response returned after the checksum byte.
 */
static adamnet_result_t packet_exchange_locked(
    const uint8_t *packet,
    uint8_t packet_length,
    uint8_t *response,
    uint8_t *response_length)
{
    uint8_t index;
    uint8_t value;
    uint8_t echo_index=0u;
    bool response_started=false;
    uint32_t deadline;
    uint32_t interbyte_deadline=0u;

    *response_length=0u;
    uart0_clear_rx();
    for (index=0u; index<packet_length; ++index) uart0_write_byte(packet[index]);

    deadline=timer_millis()+DEXT_BLOCK_TIMEOUT;
    for (;;) {
        const uint32_t now=timer_millis();
        while (uart0_read_byte(&value)) {
            if (!response_started && echo_index<packet_length &&
                value==packet[echo_index]) {
                ++echo_index;
                continue;
            }
            /* The first non-echo byte starts the node response. */
            response_started=true;
            if (*response_length<DEXT_MAX_REPLY) {
                response[(*response_length)++]=value;
            } else {
                return ADAMNET_RESULT_RX_OVERFLOW;
            }
            interbyte_deadline=timer_millis()+DEXT_INTERBYTE_MS;
        }
        if (*response_length!=0u &&
            (int32_t)(now-interbyte_deadline)>=0) break;
        if ((int32_t)(now-deadline)>=0) break;
    }

    if (uart0_rx_overflowed()) return ADAMNET_RESULT_RX_OVERFLOW;
    if (*response_length==0u) return ADAMNET_RESULT_TIMEOUT;
    return ADAMNET_RESULT_OK;
}

static adamnet_result_t stream_read_byte(uint8_t *value, uint16_t timeout_ms)
{
    const uint32_t deadline=timer_millis()+timeout_ms;
    do {
        if (uart0_read_byte(value)) return ADAMNET_RESULT_OK;
    } while ((int32_t)(timer_millis()-deadline)<0);
    return uart0_rx_overflowed()
        ? ADAMNET_RESULT_RX_OVERFLOW : ADAMNET_RESULT_TIMEOUT;
}

static uint8_t transact_byte(uint8_t value, uint8_t remove_echo,
                             uint16_t first_byte_timeout_ms,
                             uint16_t inter_byte_timeout_ms,
                             dext_probe_result_t *result)
{
    uint32_t deadline;
    uint32_t interbyte_deadline=0u;
    uint8_t received;
    bool echo_pending=(remove_echo!=0u);

    clear_result(value,result);

    uart0_clear_rx();
    uart0_write_byte(result->sent_command);

    deadline=timer_millis()+first_byte_timeout_ms;
    for (;;) {
        const uint32_t now=timer_millis();

        while (uart0_read_byte(&received)) {
            if (echo_pending && received==value) {
                result->echo_ok=1u;
                echo_pending=false;
                continue;
            }
            echo_pending=false;
            if (result->reply_length<DEXT_MAX_REPLY) {
                result->reply[result->reply_length++]=received;
            }
            interbyte_deadline=timer_millis()+inter_byte_timeout_ms;
        }

        if (result->reply_length!=0u &&
            (int32_t)(now-interbyte_deadline)>=0) break;
        if ((int32_t)(now-deadline)>=0) break;
    }

    result->line_status=uart0_rx_overflowed()?1u:0u;
    return (result->reply_length!=0u)?1u:0u;
}

/*
 * Block devices may ignore MN_READY briefly while mounting, seeking or
 * completing a previous operation. The original master polls them instead of
 * treating the first silent reply as a permanent failure.
 */
static void transact_ready(uint8_t address, dext_probe_result_t *reply,
                           uint8_t *attempts)
{
    const bool disk_device=(address>=4u && address<=7u);
    const uint8_t maximum_attempts=disk_device?DEXT_DISK_POLLS:1u;
    const uint16_t timeout=disk_device?DEXT_DISK_POLL_MS:DEXT_REPLY_TIMEOUT;
    uint8_t attempt;

    for (attempt=1u; attempt<=maximum_attempts; ++attempt) {
        (void)transact_byte((uint8_t)(ADAMNET_MN_READY|address),1u,
                            timeout,DEXT_INTERBYTE_MS,reply);
        if (reply->line_status || reply->reply_length!=0u) break;
        if (attempt<maximum_attempts) timer_delay_ms(2u);
    }
    if (attempt>maximum_attempts) attempt=maximum_attempts;
    *attempts=attempt;
}

void dext_adamnet_init(void)
{
    uint8_t index;
    /* DKB interface: UART0 on P0.2/P0.3, exactly 62,500 baud at 12 MHz. */
    P4_OUT|=BOARD_DKB_RESET_MASK;
    P4_DIR|=BOARD_DKB_RESET_MASK;
    uart0_init_adamnet();
    for (index=0u; index<ADAMNET_ADDRESS_MASK; ++index) {
        adamnet_devices[index].state=ADAMNET_DEVICE_ABSENT;
        adamnet_devices[index].last_result=ADAMNET_RESULT_TIMEOUT;
        adamnet_devices[index].status[0]=0u;
        adamnet_devices[index].status[1]=0u;
        adamnet_devices[index].status[2]=0u;
        adamnet_devices[index].status[3]=0u;
    }
}

void dext_adamnet_reset(void)
{
    P4_OUT&=(uint8_t)~BOARD_DKB_RESET_MASK;
    timer_delay_ms(3u);
    P4_OUT|=BOARD_DKB_RESET_MASK;
}

uint8_t dext_adamnet_control(uint8_t command, dext_probe_result_t *result)
{
    /*
     * The physical DKB interface is half-duplex and feeds every transmitted
     * byte back into RX.  transact_byte() discards that local echo before it
     * stores bytes returned by the addressed node.
     */
    return transact_byte(command,1u,DEXT_REPLY_TIMEOUT,
                         DEXT_INTERBYTE_MS,result);
}

uint8_t dext_adamnet_probe(uint8_t address, dext_probe_result_t *result)
{
    return dext_adamnet_control(
        (uint8_t)(ADAMNET_MN_STATUS|(address&ADAMNET_ADDRESS_MASK)),result);
}

uint8_t dext_adamnet_echo_test(uint8_t address, dext_probe_result_t *result)
{
    return transact_byte(
        (uint8_t)(ADAMNET_MN_STATUS|(address&ADAMNET_ADDRESS_MASK)),0u,
        DEXT_REPLY_TIMEOUT,DEXT_INTERBYTE_MS,result);
}

uint8_t dext_uart1_test(uint8_t mode, uint8_t value,
                        dext_probe_result_t *result)
{
    return transact_byte(value,(mode==0u)?0u:1u,DEXT_REPLY_TIMEOUT,
                         DEXT_INTERBYTE_MS,result);
}

void dext_uart1_registers(uint8_t *registers)
{
    registers[0]=SCON;
    registers[1]=PCON;
    registers[2]=TMOD;
    registers[3]=TH1;
    registers[4]=TL1;
    registers[5]=PIN_FUNC;
}

adamnet_result_t dext_adamnet_soft_reset(uint8_t address)
{
    adamnet_result_t reset_result;
    const uint8_t command=(uint8_t)(ADAMNET_MN_RESET|address);

    if (address<1u || address>ADAMNET_ADDRESS_MASK) {
        adamnet_last_result=ADAMNET_RESULT_BAD_ADDRESS;
        adamnet_last_attempts=0u;
        return ADAMNET_RESULT_BAD_ADDRESS;
    }
    if (!acquire_bus()) {
        adamnet_last_result=ADAMNET_RESULT_BUS_BUSY;
        adamnet_last_attempts=0u;
        return ADAMNET_RESULT_BUS_BUSY;
    }

    adamnet_devices[address-1u].state=ADAMNET_DEVICE_RESETTING;
    adamnet_devices[address-1u].last_result=ADAMNET_RESULT_OK;

    /*
     * The Master 6801 RESET sequence transmits one control byte and then runs
     * its transmit-clean-up routine. No node response is expected and a local
     * echo is optional, so completion of the UART transmission is sufficient.
     */
    reset_result=control_echo_locked(command);
    if (reset_result!=ADAMNET_RESULT_OK) {
        adamnet_devices[address-1u].state=ADAMNET_DEVICE_FAILED;
        adamnet_devices[address-1u].last_result=reset_result;
    }
    adamnet_last_result=reset_result;
    adamnet_last_attempts=1u;
    release_bus();
    return reset_result;
}

adamnet_result_t dext_adamnet_status(uint8_t address,
                                     dext_probe_result_t *result)
{
    uint8_t checksum;

    const adamnet_result_t exchange_result=dext_adamnet_exchange(
        address,ADAMNET_MN_STATUS,ADAMNET_NM_STATUS,6u,6u,0,result);

    if (exchange_result!=ADAMNET_RESULT_OK) {
        cache_status_result(address,exchange_result,result);
        return exchange_result;
    }

    /* The checksum is the XOR of the four status data bytes. */
    checksum=(uint8_t)(result->reply[1]^result->reply[2]^
                       result->reply[3]^result->reply[4]);
    if (checksum!=result->reply[5]) {
        adamnet_last_result=ADAMNET_RESULT_BAD_CHECKSUM;
        cache_status_result(address,ADAMNET_RESULT_BAD_CHECKSUM,result);
        return ADAMNET_RESULT_BAD_CHECKSUM;
    }

    cache_status_result(address,ADAMNET_RESULT_OK,result);
    return ADAMNET_RESULT_OK;
}

adamnet_result_t dext_adamnet_exchange(
    uint8_t address,
    uint8_t command_type,
    uint8_t expected_response_type,
    uint8_t minimum_reply_length,
    uint8_t maximum_reply_length,
    const adamnet_timing_t *timing,
    dext_probe_result_t *result)
{
    adamnet_result_t exchange_result=ADAMNET_RESULT_OK;
    uint16_t first_timeout=DEXT_REPLY_TIMEOUT;
    uint16_t inter_timeout=DEXT_INTERBYTE_MS;
    uint8_t command;

    if (result==0) {
        adamnet_last_result=ADAMNET_RESULT_BAD_ARGUMENT;
        adamnet_last_attempts=0u;
        return ADAMNET_RESULT_BAD_ARGUMENT;
    }
    if (address<1u || address>ADAMNET_ADDRESS_MASK) {
        clear_result(0u,result);
        adamnet_last_result=ADAMNET_RESULT_BAD_ADDRESS;
        adamnet_last_attempts=0u;
        return ADAMNET_RESULT_BAD_ADDRESS;
    }
    if ((command_type&ADAMNET_ADDRESS_MASK)!=0u ||
        (expected_response_type!=DEXT_RAW_RESPONSE &&
         (expected_response_type&ADAMNET_ADDRESS_MASK)!=0u) ||
        minimum_reply_length>maximum_reply_length ||
        maximum_reply_length>DEXT_MAX_REPLY) {
        clear_result(0u,result);
        adamnet_last_result=ADAMNET_RESULT_BAD_ARGUMENT;
        adamnet_last_attempts=0u;
        return ADAMNET_RESULT_BAD_ARGUMENT;
    }
    if (timing!=0) {
        if (timing->first_byte_timeout_ms==0u ||
            timing->inter_byte_timeout_ms==0u) {
            clear_result(0u,result);
            adamnet_last_result=ADAMNET_RESULT_BAD_ARGUMENT;
            adamnet_last_attempts=0u;
            return ADAMNET_RESULT_BAD_ARGUMENT;
        }
        first_timeout=timing->first_byte_timeout_ms;
        inter_timeout=timing->inter_byte_timeout_ms;
    }

    command=(uint8_t)(command_type|address);
    if (!acquire_bus()) {
        clear_result(command,result);
        adamnet_last_result=ADAMNET_RESULT_BUS_BUSY;
        adamnet_last_attempts=0u;
        return ADAMNET_RESULT_BUS_BUSY;
    }

    (void)transact_byte(command,1u,first_timeout,inter_timeout,result);

    if (result->line_status) {
        exchange_result=ADAMNET_RESULT_RX_OVERFLOW;
    } else if (result->reply_length==0u) {
        exchange_result=ADAMNET_RESULT_TIMEOUT;
    } else if (result->reply_length<minimum_reply_length ||
               result->reply_length>maximum_reply_length) {
        exchange_result=ADAMNET_RESULT_BAD_LENGTH;
    } else if (expected_response_type!=DEXT_RAW_RESPONSE &&
               result->reply[0]!=(uint8_t)(expected_response_type|address)) {
        exchange_result=ADAMNET_RESULT_BAD_COMMAND;
    }

    adamnet_last_result=exchange_result;
    adamnet_last_attempts=1u;
    release_bus();
    return exchange_result;
}

adamnet_result_t dext_adamnet_exchange_retry(
    uint8_t address,
    uint8_t command_type,
    uint8_t expected_response_type,
    uint8_t minimum_reply_length,
    uint8_t maximum_reply_length,
    const adamnet_timing_t *timing,
    uint8_t retries,
    dext_probe_result_t *result)
{
    adamnet_result_t retry_result;
    uint8_t attempt=0u;

    /* Prevent uint8_t attempt wraparound and unreasonably long USB blocking. */
    if (retries>15u) {
        adamnet_last_result=ADAMNET_RESULT_BAD_ARGUMENT;
        adamnet_last_attempts=0u;
        return ADAMNET_RESULT_BAD_ARGUMENT;
    }

    do {
        ++attempt;
        retry_result=dext_adamnet_exchange(
            address,command_type,expected_response_type,
            minimum_reply_length,maximum_reply_length,timing,result);

        if (retry_result==ADAMNET_RESULT_OK ||
            retry_result==ADAMNET_RESULT_BAD_ADDRESS ||
            retry_result==ADAMNET_RESULT_BAD_ARGUMENT ||
            retry_result==ADAMNET_RESULT_BUS_BUSY) break;
    } while (attempt<=(uint8_t)retries);

    adamnet_last_result=retry_result;
    adamnet_last_attempts=attempt;
    return retry_result;
}

adamnet_result_t dext_adamnet_last_result(void)
{
    return adamnet_last_result;
}

uint8_t dext_adamnet_last_attempts(void)
{
    return adamnet_last_attempts;
}

uint8_t dext_adamnet_bus_busy(void)
{
    return adamnet_busy?1u:0u;
}

uint8_t dext_adamnet_last_stage(void)
{
    return adamnet_last_stage;
}

uint8_t dext_adamnet_last_packet_reply(uint8_t *reply)
{
    uint8_t index;
    if (reply!=0) {
        for (index=0u; index<adamnet_last_packet_length; ++index)
            reply[index]=adamnet_last_packet_bytes[index];
    }
    return adamnet_last_packet_length;
}

void dext_adamnet_scan_devices(uint16_t *present_mask, uint16_t *failed_mask)
{
    uint8_t address;
    uint8_t index;
    uint16_t present=0u;
    uint16_t failed=0u;

    for (address=1u; address<=ADAMNET_ADDRESS_MASK; ++address) {
        dext_probe_result_t probe;
        const adamnet_result_t result=dext_adamnet_status(address,&probe);
        adamnet_device_info_t *const device=&adamnet_devices[address-1u];

        device->last_result=result;
        if (result==ADAMNET_RESULT_OK) {
            device->state=ADAMNET_DEVICE_READY;
            for (index=0u; index<4u; ++index)
                device->status[index]=probe.reply[index+1u];
            present|=(uint16_t)(1u<<(address-1u));
        } else {
            device->state=(result==ADAMNET_RESULT_TIMEOUT)
                ? ADAMNET_DEVICE_ABSENT : ADAMNET_DEVICE_FAILED;
            for (index=0u; index<4u; ++index) device->status[index]=0u;
            if (result!=ADAMNET_RESULT_TIMEOUT)
                failed|=(uint16_t)(1u<<(address-1u));
        }
    }
    if (present_mask!=0) *present_mask=present;
    if (failed_mask!=0) *failed_mask=failed;
}

adamnet_result_t dext_adamnet_device_info(
    uint8_t address,
    adamnet_device_info_t *info)
{
    uint8_t index;
    const adamnet_device_info_t *device;

    if (info==0) return ADAMNET_RESULT_BAD_ARGUMENT;
    if (validate_address(address)!=ADAMNET_RESULT_OK)
        return ADAMNET_RESULT_BAD_ADDRESS;
    device=&adamnet_devices[address-1u];
    info->state=device->state;
    info->last_result=device->last_result;
    for (index=0u; index<4u; ++index) info->status[index]=device->status[index];
    return ADAMNET_RESULT_OK;
}

static adamnet_result_t receive_disk_packet(uint8_t address)
{
    dext_probe_result_t reply;
    adamnet_result_t result=ADAMNET_RESULT_OK;
    uint8_t value;
    uint8_t checksum=0u;
    uint8_t attempt;
    uint16_t declared_length=0u;
    uint16_t index;

    if (!acquire_bus()) return ADAMNET_RESULT_BUS_BUSY;
    adamnet_disk_length=0u;
    adamnet_disk_checksum=0u;

    /*
     * A disk node may deliberately ignore MN_RECEIVE while it emulates seek
     * time.  The original ADAM master polls it repeatedly, so do the same for
     * at most about 240 ms (40 x 4 ms reply window plus 2 ms between polls).
     */
    adamnet_last_stage=1u;
    for (attempt=1u; attempt<=DEXT_DISK_POLLS; ++attempt) {
        (void)transact_byte((uint8_t)(ADAMNET_MN_RECEIVE|address),1u,
                            DEXT_DISK_POLL_MS,DEXT_INTERBYTE_MS,&reply);
        if (reply.line_status || reply.reply_length!=0u) break;
        if (attempt<DEXT_DISK_POLLS) timer_delay_ms(2u);
    }
    if (reply.line_status) result=ADAMNET_RESULT_RX_OVERFLOW;
    else if (reply.reply_length==0u) result=ADAMNET_RESULT_TIMEOUT;
    else if (reply.reply_length!=1u) result=ADAMNET_RESULT_BAD_LENGTH;
    else if (reply.reply[0]==(uint8_t)(ADAMNET_NM_NACK|address))
        result=ADAMNET_RESULT_NODE_NACK;
    else if (reply.reply[0]!=(uint8_t)(ADAMNET_NM_ACK|address))
        result=ADAMNET_RESULT_BAD_COMMAND;

    if (result==ADAMNET_RESULT_OK) {
        adamnet_last_stage=2u;
        uart0_clear_rx();
        uart0_write_byte((uint8_t)(ADAMNET_MN_CLEAR|address));

        result=stream_read_byte(&value,DEXT_BLOCK_TIMEOUT);
        if (result==ADAMNET_RESULT_OK &&
            value==(uint8_t)(ADAMNET_MN_CLEAR|address))
            result=stream_read_byte(&value,DEXT_BLOCK_TIMEOUT);
        if (result==ADAMNET_RESULT_OK &&
            value!=(uint8_t)(ADAMNET_NM_SEND|address))
            result=ADAMNET_RESULT_BAD_COMMAND;
        if (result==ADAMNET_RESULT_OK) {
            result=stream_read_byte(&value,DEXT_INTERBYTE_MS);
            if (result==ADAMNET_RESULT_OK)
                declared_length=(uint16_t)value<<8;
        }
        if (result==ADAMNET_RESULT_OK) {
            result=stream_read_byte(&value,DEXT_INTERBYTE_MS);
            if (result==ADAMNET_RESULT_OK) declared_length|=value;
        }

        if (result==ADAMNET_RESULT_OK &&
            declared_length!=ADAMNET_DISK_BLOCK_SIZE)
            result=(declared_length>ADAMNET_DISK_BLOCK_SIZE)
                ? ADAMNET_RESULT_BLOCK_TOO_LARGE : ADAMNET_RESULT_BAD_LENGTH;

        for (index=0u; result==ADAMNET_RESULT_OK &&
             index<declared_length; ++index) {
            result=stream_read_byte(&value,DEXT_INTERBYTE_MS);
            if (result==ADAMNET_RESULT_OK) {
                adamnet_disk_block[index]=value;
                checksum^=value;
            }
        }
        if (result==ADAMNET_RESULT_OK) {
            result=stream_read_byte(&value,DEXT_INTERBYTE_MS);
            if (result==ADAMNET_RESULT_OK && value!=checksum)
                result=ADAMNET_RESULT_BAD_CHECKSUM;
        }

        if (result==ADAMNET_RESULT_OK) {
            adamnet_disk_length=declared_length;
            adamnet_disk_checksum=checksum;
            adamnet_last_stage=3u;
        }
        {
            const adamnet_result_t cleanup_result=control_echo_locked(
                (uint8_t)(((result==ADAMNET_RESULT_OK)
                    ? ADAMNET_MN_ACK : ADAMNET_MN_NACK)|address));
            if (result==ADAMNET_RESULT_OK &&
                cleanup_result!=ADAMNET_RESULT_OK) result=cleanup_result;
        }
    }

    release_bus();
    adamnet_last_result=result;
    adamnet_last_attempts=attempt;
    return result;
}

adamnet_result_t dext_adamnet_disk_read(
    uint8_t address,
    uint32_t block_number,
    uint16_t *block_length,
    uint8_t *block_checksum)
{
    uint8_t request[5];
    adamnet_result_t result;

    if (block_length==0 || block_checksum==0)
        return ADAMNET_RESULT_BAD_ARGUMENT;
    *block_length=0u;
    *block_checksum=0u;
    if (address<4u || address>7u) return ADAMNET_RESULT_BAD_ADDRESS;

    request[0]=(uint8_t)block_number;
    request[1]=(uint8_t)(block_number>>8);
    request[2]=(uint8_t)(block_number>>16);
    request[3]=(uint8_t)(block_number>>24);
    request[4]=0u;
    result=dext_adamnet_send_block(address,request,5u);
    if (result==ADAMNET_RESULT_OK) result=receive_disk_packet(address);
    if (result==ADAMNET_RESULT_OK) {
        *block_length=adamnet_disk_length;
        *block_checksum=adamnet_disk_checksum;
    }
    return result;
}

adamnet_result_t dext_adamnet_disk_write(
    uint8_t address,
    uint32_t block_number,
    uint8_t *block_checksum)
{
    uint8_t request[5];
    uint16_t index;
    uint8_t checksum=0u;
    adamnet_result_t result;

    if (block_checksum==0) return ADAMNET_RESULT_BAD_ARGUMENT;
    *block_checksum=0u;
    if (address<4u || address>7u) return ADAMNET_RESULT_BAD_ADDRESS;
    if (adamnet_upload_expected!=ADAMNET_DISK_BLOCK_SIZE ||
        adamnet_upload_written!=ADAMNET_DISK_BLOCK_SIZE)
        return ADAMNET_RESULT_BAD_LENGTH;

    for (index=0u; index<ADAMNET_DISK_BLOCK_SIZE; ++index)
        checksum^=adamnet_disk_block[index];

    request[0]=(uint8_t)block_number;
    request[1]=(uint8_t)(block_number>>8);
    request[2]=(uint8_t)(block_number>>16);
    request[3]=(uint8_t)(block_number>>24);
    request[4]=0u;

    /*
     * The node ACKs the selector first. Its next READY handshake selects a
     * write, after which the cached 1 KiB packet is sent and ACKed separately.
     */
    result=dext_adamnet_send_block(address,request,5u);
    if (result==ADAMNET_RESULT_OK)
        result=dext_adamnet_send_cached(address);
    if (result==ADAMNET_RESULT_OK) *block_checksum=checksum;
    return result;
}

uint8_t dext_adamnet_disk_read_chunk(
    uint16_t offset,
    uint8_t *destination,
    uint8_t maximum_length)
{
    uint8_t length;
    uint8_t index;
    uint16_t remaining;

    if (destination==0 || offset>=adamnet_disk_length) return 0u;
    remaining=(uint16_t)(adamnet_disk_length-offset);
    length=(remaining<maximum_length)?(uint8_t)remaining:maximum_length;
    for (index=0u; index<length; ++index)
        destination[index]=adamnet_disk_block[offset+index];
    return length;
}

uint8_t dext_adamnet_cached_chunk(
    uint16_t offset,
    uint8_t *destination,
    uint8_t maximum_length)
{
    return dext_adamnet_disk_read_chunk(offset,destination,maximum_length);
}

adamnet_result_t dext_adamnet_upload_begin(uint16_t block_length)
{
    if (block_length==0u || block_length>ADAMNET_DISK_BLOCK_SIZE) {
        adamnet_upload_expected=0u;
        adamnet_upload_written=0u;
        return (block_length>ADAMNET_DISK_BLOCK_SIZE)
            ? ADAMNET_RESULT_BLOCK_TOO_LARGE : ADAMNET_RESULT_BAD_LENGTH;
    }
    adamnet_disk_length=0u;
    adamnet_disk_checksum=0u;
    adamnet_upload_expected=block_length;
    adamnet_upload_written=0u;
    return ADAMNET_RESULT_OK;
}

adamnet_result_t dext_adamnet_upload_chunk(
    uint16_t offset,
    const uint8_t *source,
    uint8_t length)
{
    uint8_t index;

    if (source==0 || length==0u) return ADAMNET_RESULT_BAD_ARGUMENT;
    if (adamnet_upload_expected==0u || offset!=adamnet_upload_written ||
        (uint16_t)(offset+length)>adamnet_upload_expected)
        return ADAMNET_RESULT_BAD_LENGTH;
    for (index=0u; index<length; ++index)
        adamnet_disk_block[offset+index]=source[index];
    adamnet_upload_written=(uint16_t)(adamnet_upload_written+length);
    return ADAMNET_RESULT_OK;
}


adamnet_result_t dext_adamnet_send_control(
    uint8_t address,
    uint8_t command_type)
{
    adamnet_result_t result;
    bool command_allowed;

    command_allowed=(command_type==ADAMNET_MN_ACK ||
                     command_type==ADAMNET_MN_CLEAR ||
                     command_type==ADAMNET_MN_CANCEL ||
                     command_type==ADAMNET_MN_NACK);

    if (validate_address(address)!=ADAMNET_RESULT_OK ||
        (command_type&ADAMNET_ADDRESS_MASK)!=0u || !command_allowed) {
        adamnet_last_result=(validate_address(address)!=ADAMNET_RESULT_OK)
            ? ADAMNET_RESULT_BAD_ADDRESS : ADAMNET_RESULT_BAD_ARGUMENT;
        adamnet_last_attempts=0u;
        return adamnet_last_result;
    }
    if (!acquire_bus()) {
        adamnet_last_result=ADAMNET_RESULT_BUS_BUSY;
        adamnet_last_attempts=0u;
        return ADAMNET_RESULT_BUS_BUSY;
    }
    result=control_echo_locked((uint8_t)(command_type|address));
    release_bus();
    adamnet_last_result=result;
    adamnet_last_attempts=1u;
    return result;
}

adamnet_result_t dext_adamnet_receive_cached(
    uint8_t address,
    uint16_t *block_length,
    uint8_t *block_checksum)
{
    dext_probe_result_t reply;
    adamnet_result_t result=ADAMNET_RESULT_OK;
    uint16_t index;
    uint8_t value;
    uint8_t checksum=0u;
    uint16_t declared_length=0u;

    if (block_length!=0) *block_length=0u;
    if (block_checksum!=0) *block_checksum=0u;
    if (block_length==0 || block_checksum==0)
        result=ADAMNET_RESULT_BAD_ARGUMENT;
    else if (validate_address(address)!=ADAMNET_RESULT_OK)
        result=ADAMNET_RESULT_BAD_ADDRESS;
    if (result!=ADAMNET_RESULT_OK) {
        adamnet_last_result=result;
        adamnet_last_attempts=0u;
        return result;
    }
    if (!acquire_bus()) {
        adamnet_last_result=ADAMNET_RESULT_BUS_BUSY;
        adamnet_last_attempts=0u;
        return ADAMNET_RESULT_BUS_BUSY;
    }

    adamnet_last_stage=1u; /* MN_RECEIVE request and NM_ACK/NM_NACK reply. */
    (void)transact_byte((uint8_t)(ADAMNET_MN_RECEIVE|address),1u,
                        DEXT_REPLY_TIMEOUT,DEXT_INTERBYTE_MS,&reply);
    if (reply.line_status) result=ADAMNET_RESULT_RX_OVERFLOW;
    else if (reply.reply_length==0u) result=ADAMNET_RESULT_TIMEOUT;
    else if (reply.reply_length!=1u) result=ADAMNET_RESULT_BAD_LENGTH;
    else if (reply.reply[0]==(uint8_t)(ADAMNET_NM_NACK|address))
        result=ADAMNET_RESULT_NODE_NACK;
    else if (reply.reply[0]==(uint8_t)(ADAMNET_NM_CANCEL|address))
        result=ADAMNET_RESULT_NODE_CANCEL;
    else if (reply.reply[0]!=(uint8_t)(ADAMNET_NM_ACK|address))
        result=ADAMNET_RESULT_BAD_COMMAND;

    if (result==ADAMNET_RESULT_OK) {
        adamnet_last_stage=2u; /* MN_CLEAR followed by the NM_SEND packet. */
        uart0_clear_rx();
        uart0_write_byte((uint8_t)(ADAMNET_MN_CLEAR|address));

        result=stream_read_byte(&value,DEXT_BLOCK_TIMEOUT);
        if (result==ADAMNET_RESULT_OK &&
            value==(uint8_t)(ADAMNET_MN_CLEAR|address))
            result=stream_read_byte(&value,DEXT_BLOCK_TIMEOUT);
        if (result==ADAMNET_RESULT_OK &&
            value!=(uint8_t)(ADAMNET_NM_SEND|address))
            result=ADAMNET_RESULT_BAD_COMMAND;
        if (result==ADAMNET_RESULT_OK) {
            result=stream_read_byte(&value,DEXT_INTERBYTE_MS);
            if (result==ADAMNET_RESULT_OK)
                declared_length=(uint16_t)value<<8;
        }
        if (result==ADAMNET_RESULT_OK) {
            result=stream_read_byte(&value,DEXT_INTERBYTE_MS);
            if (result==ADAMNET_RESULT_OK) declared_length|=value;
        }
        if (result==ADAMNET_RESULT_OK &&
            declared_length>ADAMNET_DISK_BLOCK_SIZE)
            result=ADAMNET_RESULT_BLOCK_TOO_LARGE;

        for (index=0u; result==ADAMNET_RESULT_OK &&
             index<declared_length; ++index) {
            result=stream_read_byte(&value,DEXT_INTERBYTE_MS);
            if (result==ADAMNET_RESULT_OK) {
                adamnet_disk_block[index]=value;
                checksum^=value;
            }
        }
        if (result==ADAMNET_RESULT_OK) {
            result=stream_read_byte(&value,DEXT_INTERBYTE_MS);
            if (result==ADAMNET_RESULT_OK && value!=checksum)
                result=ADAMNET_RESULT_BAD_CHECKSUM;
        }

        if (result==ADAMNET_RESULT_OK) {
            adamnet_disk_length=declared_length;
            adamnet_disk_checksum=checksum;
            *block_length=declared_length;
            *block_checksum=checksum;
            adamnet_last_stage=3u;
        } else {
            /* Never expose a partial or invalid transfer through USB. */
            adamnet_disk_length=0u;
            adamnet_disk_checksum=0u;
        }

        /* ACK consumes a valid packet; NACK requests retransmission on error. */
        {
            const adamnet_result_t cleanup_result=control_echo_locked(
                (uint8_t)(((result==ADAMNET_RESULT_OK)
                    ? ADAMNET_MN_ACK : ADAMNET_MN_NACK)|address));
            if (result==ADAMNET_RESULT_OK &&
                cleanup_result!=ADAMNET_RESULT_OK) result=cleanup_result;
        }
    }

    release_bus();
    adamnet_last_result=result;
    adamnet_last_attempts=1u;
    return result;
}

adamnet_result_t dext_adamnet_receive_block(
    uint8_t address,
    uint8_t *block,
    uint8_t *block_length)
{
    uint16_t cached_length=0u;
    uint8_t cached_checksum=0u;
    uint8_t index;
    adamnet_result_t result;

    if (block_length!=0) *block_length=0u;
    if (block==0 || block_length==0) return ADAMNET_RESULT_BAD_ARGUMENT;

    result=dext_adamnet_receive_cached(
        address,&cached_length,&cached_checksum);
    (void)cached_checksum;
    if (result!=ADAMNET_RESULT_OK) return result;
    if (cached_length>ADAMNET_MAX_BLOCK) {
        adamnet_last_result=ADAMNET_RESULT_BLOCK_TOO_LARGE;
        return ADAMNET_RESULT_BLOCK_TOO_LARGE;
    }
    for (index=0u; index<(uint8_t)cached_length; ++index)
        block[index]=adamnet_disk_block[index];
    *block_length=(uint8_t)cached_length;
    return ADAMNET_RESULT_OK;
}

adamnet_result_t dext_adamnet_send_block(
    uint8_t address,
    const uint8_t *block,
    uint8_t block_length)
{
    dext_probe_result_t reply;
    adamnet_result_t result=ADAMNET_RESULT_OK;
    uint8_t packet[ADAMNET_MAX_BLOCK+4u];
    uint8_t response[DEXT_MAX_REPLY];
    uint8_t response_length=0u;
    uint8_t checksum=0u;
    uint8_t index;
    uint8_t ready_attempts=0u;

    if ((block==0 && block_length!=0u) || block_length>ADAMNET_MAX_BLOCK)
        result=(block_length>ADAMNET_MAX_BLOCK)
            ? ADAMNET_RESULT_BLOCK_TOO_LARGE : ADAMNET_RESULT_BAD_ARGUMENT;
    else if (validate_address(address)!=ADAMNET_RESULT_OK)
        result=ADAMNET_RESULT_BAD_ADDRESS;
    if (result!=ADAMNET_RESULT_OK) {
        adamnet_last_result=result;
        adamnet_last_attempts=0u;
        return result;
    }
    if (!acquire_bus()) {
        adamnet_last_result=ADAMNET_RESULT_BUS_BUSY;
        adamnet_last_attempts=0u;
        return ADAMNET_RESULT_BUS_BUSY;
    }

    adamnet_last_packet_length=0u;
    adamnet_last_stage=4u; /* MN_READY request and node readiness reply. */
    transact_ready(address,&reply,&ready_attempts);
    if (reply.line_status) result=ADAMNET_RESULT_RX_OVERFLOW;
    else if (reply.reply_length==0u) result=ADAMNET_RESULT_TIMEOUT;
    else if (reply.reply_length!=1u) result=ADAMNET_RESULT_BAD_LENGTH;
    else if (reply.reply[0]==(uint8_t)(ADAMNET_NM_NACK|address))
        result=ADAMNET_RESULT_NODE_NACK;
    else if (reply.reply[0]==(uint8_t)(ADAMNET_NM_CANCEL|address))
        result=ADAMNET_RESULT_NODE_CANCEL;
    else if (reply.reply[0]!=(uint8_t)(ADAMNET_NM_ACK|address))
        result=ADAMNET_RESULT_BAD_COMMAND;

    if (result==ADAMNET_RESULT_OK) {
        packet[0]=(uint8_t)(ADAMNET_MN_SEND|address);
        packet[1]=0u;
        packet[2]=block_length;
        for (index=0u; index<block_length; ++index) {
            packet[3u+index]=block[index];
            checksum^=block[index];
        }
        packet[3u+block_length]=checksum;
        adamnet_last_stage=5u; /* MN_SEND packet and node ACK/NACK reply. */
        result=packet_exchange_locked(packet,(uint8_t)(block_length+4u),
                                      response,&response_length);
        adamnet_last_packet_length=response_length;
        for (index=0u; index<response_length; ++index)
            adamnet_last_packet_bytes[index]=response[index];
        if (result==ADAMNET_RESULT_OK) {
            if (response_length!=1u) result=ADAMNET_RESULT_BAD_LENGTH;
            else if (response[0]==(uint8_t)(ADAMNET_NM_NACK|address))
                result=ADAMNET_RESULT_NODE_NACK;
            else if (response[0]==(uint8_t)(ADAMNET_NM_CANCEL|address))
                result=ADAMNET_RESULT_NODE_CANCEL;
            else if (response[0]!=(uint8_t)(ADAMNET_NM_ACK|address))
                result=ADAMNET_RESULT_BAD_COMMAND;
        }
        /* The original master terminates both ACK and NACK replies with ACK. */
        {
            if (result==ADAMNET_RESULT_OK)
                adamnet_last_stage=6u; /* Final MN_ACK echo cleanup. */
            const adamnet_result_t cleanup_result=control_echo_locked(
                (uint8_t)(ADAMNET_MN_ACK|address));
            if (result==ADAMNET_RESULT_OK &&
                cleanup_result!=ADAMNET_RESULT_OK) result=cleanup_result;
        }
    }

    release_bus();
    adamnet_last_result=result;
    adamnet_last_attempts=ready_attempts;
    return result;
}

adamnet_result_t dext_adamnet_send_cached(uint8_t address)
{
    dext_probe_result_t reply;
    adamnet_result_t result=ADAMNET_RESULT_OK;
    uint16_t index;
    uint8_t value;
    uint8_t checksum=0u;
    uint8_t ready_attempts=0u;

    if (validate_address(address)!=ADAMNET_RESULT_OK)
        result=ADAMNET_RESULT_BAD_ADDRESS;
    else if (adamnet_upload_expected==0u ||
             adamnet_upload_written!=adamnet_upload_expected)
        result=ADAMNET_RESULT_BAD_LENGTH;
    if (result!=ADAMNET_RESULT_OK) {
        adamnet_last_result=result;
        adamnet_last_attempts=0u;
        return result;
    }
    if (!acquire_bus()) {
        adamnet_last_result=ADAMNET_RESULT_BUS_BUSY;
        adamnet_last_attempts=0u;
        return ADAMNET_RESULT_BUS_BUSY;
    }

    adamnet_last_packet_length=0u;
    adamnet_last_stage=4u;
    transact_ready(address,&reply,&ready_attempts);
    if (reply.line_status) result=ADAMNET_RESULT_RX_OVERFLOW;
    else if (reply.reply_length==0u) result=ADAMNET_RESULT_TIMEOUT;
    else if (reply.reply_length!=1u) result=ADAMNET_RESULT_BAD_LENGTH;
    else if (reply.reply[0]==(uint8_t)(ADAMNET_NM_NACK|address))
        result=ADAMNET_RESULT_NODE_NACK;
    else if (reply.reply[0]==(uint8_t)(ADAMNET_NM_CANCEL|address))
        result=ADAMNET_RESULT_NODE_CANCEL;
    else if (reply.reply[0]!=(uint8_t)(ADAMNET_NM_ACK|address))
        result=ADAMNET_RESULT_BAD_COMMAND;

    if (result==ADAMNET_RESULT_OK) {
        /*
         * The tested DKB interface reports ECHO=NO. Streaming avoids a second
         * 1 KiB packet buffer and prevents stack exhaustion on the CH559.
         */
        adamnet_last_stage=5u;
        uart0_clear_rx();
        uart0_write_byte((uint8_t)(ADAMNET_MN_SEND|address));
        uart0_write_byte((uint8_t)(adamnet_upload_expected>>8));
        uart0_write_byte((uint8_t)adamnet_upload_expected);
        for (index=0u; index<adamnet_upload_expected; ++index) {
            value=adamnet_disk_block[index];
            checksum^=value;
            uart0_write_byte(value);
        }
        uart0_write_byte(checksum);

        result=stream_read_byte(&value,DEXT_BLOCK_TIMEOUT);
        if (result==ADAMNET_RESULT_OK) {
            adamnet_last_packet_bytes[0]=value;
            adamnet_last_packet_length=1u;
            if (value==(uint8_t)(ADAMNET_NM_NACK|address))
                result=ADAMNET_RESULT_NODE_NACK;
            else if (value==(uint8_t)(ADAMNET_NM_CANCEL|address))
                result=ADAMNET_RESULT_NODE_CANCEL;
            else if (value!=(uint8_t)(ADAMNET_NM_ACK|address))
                result=ADAMNET_RESULT_BAD_COMMAND;
        }
        {
            if (result==ADAMNET_RESULT_OK) adamnet_last_stage=6u;
            const adamnet_result_t cleanup_result=control_echo_locked(
                (uint8_t)(ADAMNET_MN_ACK|address));
            if (result==ADAMNET_RESULT_OK &&
                cleanup_result!=ADAMNET_RESULT_OK) result=cleanup_result;
        }
    }

    release_bus();
    adamnet_last_result=result;
    adamnet_last_attempts=ready_attempts;
    return result;
}
