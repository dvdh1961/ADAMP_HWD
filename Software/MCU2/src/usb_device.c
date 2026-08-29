#include <stdint.h>
#include "board.h"
#include "ch559_sdcc.h"
#include "dcb.h"
#include "dext_adamnet.h"
#include "timer.h"
#include "usb_device.h"

#define EP0_SIZE 8u
#define USB_GET_STATUS 0x00u
#define USB_CLEAR_FEATURE 0x01u
#define USB_SET_ADDRESS 0x05u
#define USB_GET_DESCRIPTOR 0x06u
#define USB_GET_CONFIGURATION 0x08u
#define USB_SET_CONFIGURATION 0x09u
#define USB_GET_INTERFACE 0x0Au
#define USB_SET_INTERFACE 0x0Bu
#define HID_SET_REPORT 0x09u
#define HID_SET_IDLE 0x0Au
#define DESC_DEVICE 0x01u
#define DESC_CONFIGURATION 0x02u
#define DESC_STRING 0x03u
#define DESC_REPORT 0x22u

__xdata __at(0x2446) volatile uint8_t uep4_1_mod;
__xdata __at(0x2447) volatile uint8_t uep2_3_mod;
__xdata __at(0x2448) volatile uint8_t uep0_dma_high;
__xdata __at(0x2449) volatile uint8_t uep0_dma_low;
__xdata __at(0x244A) volatile uint8_t uep1_dma_high;
__xdata __at(0x244B) volatile uint8_t uep1_dma_low;
__xdata __at(0x244C) volatile uint8_t uep2_dma_high;
__xdata __at(0x244D) volatile uint8_t uep2_dma_low;
__xdata __at(0x244E) volatile uint8_t uep3_dma_high;
__xdata __at(0x244F) volatile uint8_t uep3_dma_low;
__xdata __at(0x0000) uint8_t ep0_buffer[EP0_SIZE];
/* Use the same proven DMA address as Ep1Buffer in the original Keil build. */
__xdata __at(0x0100) uint8_t ep3_out_buffer[64];
__xdata __at(0x0140) uint8_t ep3_in_buffer[64];

static const __code uint8_t device_descriptor[] = {
    18,DESC_DEVICE, 0x10,0x01, 0,0,0,EP0_SIZE,
    0x8A,0x25, 0x36,0x00, 0x86,0x10, 1,2,3,1
};

static const __code uint8_t gateway_report_descriptor[] = {
    0x06,0x00,0xFF,0x09,0x01,0xA1,0x01,
    0x15,0x00,0x26,0xFF,0x00,0x75,0x08,0x95,0x40,
    0x09,0x01,0x81,0x02,
    0x75,0x08,0x95,0x40,0x09,0x01,0x91,0x02,
    0xC0
};

static const __code uint8_t configuration_descriptor[] = {
    9,DESC_CONFIGURATION,41,0,1,1,0,0xA0,50,
    9,0x04,0,0,2,0x03,0x00,0x00,0,
    9,0x21,0x10,0x01,0,1,DESC_REPORT,sizeof(gateway_report_descriptor),0,
    7,0x05,0x83,0x03,64,0,0x01,
    7,0x05,0x03,0x03,64,0,0x01
};

static const __code uint8_t language_string[] = {4,DESC_STRING,0x09,0x04};
static const __code uint8_t manufacturer_string[] = {12,DESC_STRING,'A',0,'D',0,'A',0,'M',0,'+',0};
static const __code uint8_t product_string[] = {
    26,DESC_STRING,'A',0,'D',0,'A',0,'M',0,'n',0,'e',0,'t',0,' ',0,
    'M',0,'C',0,'U',0,'2',0
};
static const __code uint8_t serial_string[] = {16,DESC_STRING,'S',0,'D',0,'C',0,'C',0,'0',0,'1',0,'9',0};

static const __code uint8_t *descriptor_pointer;
static uint8_t descriptor_remaining, setup_request, pending_address, configuration_value;
static bool ep3_busy;
static volatile bool ep3_request_pending;
static volatile uint8_t ep3_request_length;
static bool ep3_stream_active;
static uint16_t ep3_stream_offset;
static uint8_t ep3_stream_sequence;
static uint8_t ep3_stream_device;

/*
 * Gateway packet layout (always exactly 64 bytes):
 *   0..1  : signature bytes 'A', 'L'
 *   2     : protocol version (1)
 *   3     : command; bit 7 is set in the response
 *   4     : sequence number, returned unchanged
 *   5     : ADAMnet device address
 *   6     : number of valid data bytes in positions 8..63 (0..56)
 *   7     : status; requests must set this byte to zero
 *   8..63 : payload data
 */
#define GATEWAY_MAGIC0          0x41u /* 'A' */
#define GATEWAY_MAGIC1          0x4Cu /* 'L' */
#define GATEWAY_PROTOCOL        0x01u
#define GATEWAY_HEADER_SIZE     8u
#define GATEWAY_DATA_SIZE       56u
#define GATEWAY_RESPONSE_BIT    0x80u

#define GATEWAY_CMD_PING        0x01u
#define GATEWAY_CMD_VERSION     0x02u
#define GATEWAY_CMD_STATUS      0x03u
#define GATEWAY_CMD_DEXT_PROBE  0x10u
#define GATEWAY_CMD_DEXT_RESET  0x11u
#define GATEWAY_CMD_DEVICE4_SEQ 0x12u
#define GATEWAY_CMD_UART0_DIAG  0x13u
#define GATEWAY_CMD_MASTER_STAT 0x14u
#define GATEWAY_CMD_MASTER_RST  0x15u
#define GATEWAY_CMD_MASTER_DIAG 0x16u
#define GATEWAY_CMD_MASTER_RECV 0x17u
#define GATEWAY_CMD_MASTER_SEND 0x18u
#define GATEWAY_CMD_MASTER_CTRL 0x19u
#define GATEWAY_CMD_MASTER_SCAN 0x1Au
#define GATEWAY_CMD_DEVICE_INFO 0x1Bu
#define GATEWAY_CMD_DISK_READ   0x1Cu
#define GATEWAY_CMD_DISK_CHUNK  0x1Du
#define GATEWAY_CMD_DCB_EXEC    0x1Eu
#define GATEWAY_CMD_RECV_CHUNK  0x1Fu
#define GATEWAY_CMD_SEND_BEGIN  0x20u
#define GATEWAY_CMD_SEND_CHUNK  0x21u
#define GATEWAY_CMD_SEND_COMMIT 0x22u
#define GATEWAY_CMD_STREAM_BEGIN 0x23u
#define GATEWAY_CMD_STREAM_DATA  0x24u
#define GATEWAY_CMD_ERROR       0x7Fu

#define GATEWAY_OK              0x00u
#define GATEWAY_BAD_MAGIC       0x01u
#define GATEWAY_BAD_PROTOCOL    0x02u
#define GATEWAY_BAD_LENGTH      0x03u
#define GATEWAY_BAD_STATUS      0x04u
#define GATEWAY_BAD_COMMAND     0x05u

static void gateway_make_response(uint8_t received_length)
{
    uint8_t i;
    uint8_t command=GATEWAY_CMD_ERROR;
    uint8_t sequence=0u;
    uint8_t device=0u;
    uint8_t payload_length=0u;
    uint8_t status=GATEWAY_OK;

    if (received_length>3u) command=ep3_out_buffer[3];
    if (received_length>4u) sequence=ep3_out_buffer[4];
    if (received_length>5u) device=ep3_out_buffer[5];

    if (received_length!=64u || ep3_out_buffer[6]>GATEWAY_DATA_SIZE) {
        status=GATEWAY_BAD_LENGTH;
    } else if (ep3_out_buffer[0]!=GATEWAY_MAGIC0 ||
               ep3_out_buffer[1]!=GATEWAY_MAGIC1) {
        status=GATEWAY_BAD_MAGIC;
    } else if (ep3_out_buffer[2]!=GATEWAY_PROTOCOL) {
        status=GATEWAY_BAD_PROTOCOL;
    } else if (ep3_out_buffer[7]!=0u) {
        status=GATEWAY_BAD_STATUS;
    }

    for (i=0u; i<64u; ++i) ep3_in_buffer[i]=0u;
    ep3_in_buffer[0]=GATEWAY_MAGIC0;
    ep3_in_buffer[1]=GATEWAY_MAGIC1;
    ep3_in_buffer[2]=GATEWAY_PROTOCOL;
    ep3_in_buffer[3]=(uint8_t)(command|GATEWAY_RESPONSE_BIT);
    ep3_in_buffer[4]=sequence;
    ep3_in_buffer[5]=device;
    ep3_in_buffer[7]=status;

    if (status==GATEWAY_OK) {
        switch (command) {
        case GATEWAY_CMD_PING:
            payload_length=ep3_out_buffer[6];
            for (i=0u; i<payload_length; ++i) {
                ep3_in_buffer[GATEWAY_HEADER_SIZE+i]=
                    ep3_out_buffer[GATEWAY_HEADER_SIZE+i];
            }
            break;

        case GATEWAY_CMD_VERSION:
            /* Firmware 2.14.1: block-device MN_READY uses bounded polling. */
            payload_length=5u;
            ep3_in_buffer[8]=2u;
            ep3_in_buffer[9]=14u;
            ep3_in_buffer[10]=1u;
            ep3_in_buffer[11]=GATEWAY_PROTOCOL;
            ep3_in_buffer[12]=0x59u;
            break;

        case GATEWAY_CMD_STATUS:
            /* Raw port values make hardware diagnostics reproducible. */
            payload_length=6u;
            ep3_in_buffer[8]=configuration_value;
            ep3_in_buffer[9]=P0;
            ep3_in_buffer[10]=P1;
            ep3_in_buffer[11]=P2;
            ep3_in_buffer[12]=P3;
            ep3_in_buffer[13]=P4_IN;
            break;

        case GATEWAY_CMD_UART0_DIAG:
            payload_length=6u;
            dext_uart1_registers(&ep3_in_buffer[8]);
            break;

        case GATEWAY_CMD_DEXT_PROBE:
            if (ep3_out_buffer[6]!=1u || ep3_out_buffer[8]<1u ||
                ep3_out_buffer[8]>15u) {
                ep3_in_buffer[7]=GATEWAY_BAD_LENGTH;
            } else {
                dext_probe_result_t probe;
                uint8_t found;
                found=dext_adamnet_probe(ep3_out_buffer[8],&probe);
                /* Six metadata bytes leave room for at most 50 raw bytes. */
                if (probe.reply_length>50u) probe.reply_length=50u;
                payload_length=(uint8_t)(6u+probe.reply_length);
                ep3_in_buffer[8]=found;
                ep3_in_buffer[9]=probe.echo_ok;
                ep3_in_buffer[10]=probe.sent_command;
                ep3_in_buffer[11]=probe.reply_length;
                ep3_in_buffer[12]=probe.first_rx_status;
                ep3_in_buffer[13]=probe.line_status;
                for (i=0u; i<probe.reply_length; ++i) {
                    ep3_in_buffer[14u+i]=probe.reply[i];
                }
            }
            break;

        case GATEWAY_CMD_MASTER_STAT:
            if (ep3_out_buffer[6]!=0u || device<1u || device>15u) {
                ep3_in_buffer[7]=GATEWAY_BAD_LENGTH;
            } else {
                dext_probe_result_t probe;
                const adamnet_result_t master_result=
                    dext_adamnet_status(device,&probe);

                /*
                 * Byte 0 is the validated master result.  The remaining bytes
                 * preserve the raw response for diagnostics and emulator use.
                 */
                ep3_in_buffer[8]=(uint8_t)master_result;
                ep3_in_buffer[9]=probe.reply_length;
                for (i=0u; i<probe.reply_length; ++i) {
                    ep3_in_buffer[10u+i]=probe.reply[i];
                }
                payload_length=(uint8_t)(2u+probe.reply_length);
            }
            break;

        case GATEWAY_CMD_MASTER_RST:
            if (ep3_out_buffer[6]!=0u || device<1u || device>15u) {
                ep3_in_buffer[7]=GATEWAY_BAD_LENGTH;
            } else {
                /* MN_RESET has no node response; report the echo-cleanup result. */
                ep3_in_buffer[8]=(uint8_t)dext_adamnet_soft_reset(device);
                payload_length=1u;
            }
            break;

        case GATEWAY_CMD_MASTER_DIAG:
            if (ep3_out_buffer[6]!=0u) {
                ep3_in_buffer[7]=GATEWAY_BAD_LENGTH;
            } else {
                /* Last result, number of attempts and current bus lock state. */
                ep3_in_buffer[8]=(uint8_t)dext_adamnet_last_result();
                ep3_in_buffer[9]=dext_adamnet_last_attempts();
                ep3_in_buffer[10]=dext_adamnet_bus_busy();
                payload_length=3u;
            }
            break;

        case GATEWAY_CMD_MASTER_RECV:
            if (ep3_out_buffer[6]!=0u || device<1u || device>15u) {
                ep3_in_buffer[7]=GATEWAY_BAD_LENGTH;
            } else {
                uint16_t block_length=0u;
                uint8_t block_checksum=0u;
                const adamnet_result_t master_result=
                    dext_adamnet_receive_cached(
                        device,&block_length,&block_checksum);
                ep3_in_buffer[8]=(uint8_t)master_result;
                if (master_result==ADAMNET_RESULT_OK &&
                    block_length<=ADAMNET_MAX_BLOCK) {
                    const uint8_t inline_length=dext_adamnet_cached_chunk(
                        0u,&ep3_in_buffer[10],(uint8_t)block_length);
                    ep3_in_buffer[9]=inline_length;
                    payload_length=(uint8_t)(2u+inline_length);
                } else if (master_result==ADAMNET_RESULT_OK) {
                    /* 0xFF marks an extended cached response. */
                    ep3_in_buffer[9]=0xFFu;
                    ep3_in_buffer[10]=(uint8_t)block_length;
                    ep3_in_buffer[11]=(uint8_t)(block_length>>8);
                    ep3_in_buffer[12]=block_checksum;
                    payload_length=5u;
                } else {
                    ep3_in_buffer[9]=0u;
                    payload_length=2u;
                }
            }
            break;

        case GATEWAY_CMD_MASTER_SEND:
            if (ep3_out_buffer[6]>ADAMNET_MAX_BLOCK ||
                device<1u || device>15u) {
                ep3_in_buffer[7]=GATEWAY_BAD_LENGTH;
            } else {
                uint8_t raw_length;
                ep3_in_buffer[8]=(uint8_t)dext_adamnet_send_block(
                    device,&ep3_out_buffer[8],ep3_out_buffer[6]);
                ep3_in_buffer[9]=dext_adamnet_last_stage();
                raw_length=dext_adamnet_last_packet_reply(&ep3_in_buffer[11]);
                ep3_in_buffer[10]=raw_length;
                payload_length=(uint8_t)(3u+raw_length);
            }
            break;

        case GATEWAY_CMD_MASTER_CTRL:
            if (ep3_out_buffer[6]!=1u || device<1u || device>15u ||
                (ep3_out_buffer[8]&ADAMNET_ADDRESS_MASK)!=0u) {
                ep3_in_buffer[7]=GATEWAY_BAD_LENGTH;
            } else {
                ep3_in_buffer[8]=(uint8_t)dext_adamnet_send_control(
                    device,ep3_out_buffer[8]);
                payload_length=1u;
            }
            break;

        case GATEWAY_CMD_MASTER_SCAN:
            if (ep3_out_buffer[6]!=0u) {
                ep3_in_buffer[7]=GATEWAY_BAD_LENGTH;
            } else {
                uint16_t present_mask;
                uint16_t failed_mask;
                dext_adamnet_scan_devices(&present_mask,&failed_mask);
                ep3_in_buffer[8]=15u;
                ep3_in_buffer[9]=(uint8_t)present_mask;
                ep3_in_buffer[10]=(uint8_t)(present_mask>>8);
                ep3_in_buffer[11]=(uint8_t)failed_mask;
                ep3_in_buffer[12]=(uint8_t)(failed_mask>>8);
                payload_length=5u;
            }
            break;

        case GATEWAY_CMD_DEVICE_INFO:
            if (ep3_out_buffer[6]!=0u || device<1u || device>15u) {
                ep3_in_buffer[7]=GATEWAY_BAD_LENGTH;
            } else {
                adamnet_device_info_t info;
                (void)dext_adamnet_device_info(device,&info);
                ep3_in_buffer[8]=(uint8_t)info.state;
                ep3_in_buffer[9]=(uint8_t)info.last_result;
                for (i=0u; i<4u; ++i)
                    ep3_in_buffer[10u+i]=info.status[i];
                payload_length=6u;
            }
            break;

        case GATEWAY_CMD_DISK_READ:
            if (ep3_out_buffer[6]!=4u || device<4u || device>7u) {
                ep3_in_buffer[7]=GATEWAY_BAD_LENGTH;
            } else {
                uint16_t block_length;
                uint8_t block_checksum;
                const uint32_t block_number=
                    (uint32_t)ep3_out_buffer[8] |
                    ((uint32_t)ep3_out_buffer[9]<<8) |
                    ((uint32_t)ep3_out_buffer[10]<<16) |
                    ((uint32_t)ep3_out_buffer[11]<<24);
                ep3_in_buffer[8]=(uint8_t)dext_adamnet_disk_read(
                    device,block_number,&block_length,&block_checksum);
                ep3_in_buffer[9]=(uint8_t)block_length;
                ep3_in_buffer[10]=(uint8_t)(block_length>>8);
                ep3_in_buffer[11]=block_checksum;
                ep3_in_buffer[12]=dext_adamnet_last_stage();
                ep3_in_buffer[13]=dext_adamnet_last_attempts();
                payload_length=6u;
            }
            break;

        case GATEWAY_CMD_DISK_CHUNK:
            if (ep3_out_buffer[6]!=3u || ep3_out_buffer[10]>52u) {
                ep3_in_buffer[7]=GATEWAY_BAD_LENGTH;
            } else {
                const uint16_t offset=(uint16_t)ep3_out_buffer[8] |
                    ((uint16_t)ep3_out_buffer[9]<<8);
                const uint8_t chunk_length=dext_adamnet_disk_read_chunk(
                    offset,&ep3_in_buffer[10],ep3_out_buffer[10]);
                ep3_in_buffer[8]=ADAMNET_RESULT_OK;
                ep3_in_buffer[9]=chunk_length;
                payload_length=(uint8_t)(2u+chunk_length);
            }
            break;

        case GATEWAY_CMD_RECV_CHUNK:
            if (ep3_out_buffer[6]!=3u || ep3_out_buffer[10]>52u) {
                ep3_in_buffer[7]=GATEWAY_BAD_LENGTH;
            } else {
                const uint16_t offset=(uint16_t)ep3_out_buffer[8] |
                    ((uint16_t)ep3_out_buffer[9]<<8);
                const uint8_t chunk_length=dext_adamnet_cached_chunk(
                    offset,&ep3_in_buffer[10],ep3_out_buffer[10]);
                ep3_in_buffer[8]=ADAMNET_RESULT_OK;
                ep3_in_buffer[9]=chunk_length;
                payload_length=(uint8_t)(2u+chunk_length);
            }
            break;

        case GATEWAY_CMD_SEND_BEGIN:
            if (ep3_out_buffer[6]!=2u) {
                ep3_in_buffer[7]=GATEWAY_BAD_LENGTH;
            } else {
                const uint16_t block_length=(uint16_t)ep3_out_buffer[8] |
                    ((uint16_t)ep3_out_buffer[9]<<8);
                ep3_in_buffer[8]=(uint8_t)dext_adamnet_upload_begin(
                    block_length);
                payload_length=1u;
            }
            break;

        case GATEWAY_CMD_SEND_CHUNK:
            if (ep3_out_buffer[6]<3u) {
                ep3_in_buffer[7]=GATEWAY_BAD_LENGTH;
            } else {
                const uint16_t offset=(uint16_t)ep3_out_buffer[8] |
                    ((uint16_t)ep3_out_buffer[9]<<8);
                ep3_in_buffer[8]=(uint8_t)dext_adamnet_upload_chunk(
                    offset,&ep3_out_buffer[10],
                    (uint8_t)(ep3_out_buffer[6]-2u));
                payload_length=1u;
            }
            break;

        case GATEWAY_CMD_SEND_COMMIT:
            if (ep3_out_buffer[6]!=0u || device<1u || device>15u) {
                ep3_in_buffer[7]=GATEWAY_BAD_LENGTH;
            } else {
                uint8_t raw_length;
                ep3_in_buffer[8]=(uint8_t)dext_adamnet_send_cached(device);
                ep3_in_buffer[9]=dext_adamnet_last_stage();
                raw_length=dext_adamnet_last_packet_reply(&ep3_in_buffer[11]);
                ep3_in_buffer[10]=raw_length;
                payload_length=(uint8_t)(3u+raw_length);
            }
            break;

        case GATEWAY_CMD_STREAM_BEGIN:
            if (ep3_out_buffer[6]!=0u) {
                ep3_in_buffer[7]=GATEWAY_BAD_LENGTH;
            } else {
                /*
                 * A successful DCB READ leaves exactly 1 KiB in the shared
                 * xRAM cache. Acknowledge first, then emit ordered cache
                 * fragments without requiring another host OUT request.
                 */
                ep3_in_buffer[8]=ADAMNET_RESULT_OK;
                ep3_in_buffer[9]=0x00u;
                ep3_in_buffer[10]=0x04u;
                payload_length=3u;
                ep3_stream_offset=0u;
                ep3_stream_sequence=sequence;
                ep3_stream_device=device;
                ep3_stream_active=true;
            }
            break;

        case GATEWAY_CMD_DCB_EXEC:
            if (ep3_out_buffer[6]!=DCB_SIZE) {
                ep3_in_buffer[7]=GATEWAY_BAD_LENGTH;
            } else {
                uint8_t block_checksum;
                const adamnet_result_t dcb_result=dcb_execute(
                    &ep3_out_buffer[8],&ep3_in_buffer[12],&block_checksum);
                ep3_in_buffer[8]=(uint8_t)dcb_result;
                ep3_in_buffer[9]=dext_adamnet_last_stage();
                ep3_in_buffer[10]=dext_adamnet_last_attempts();
                ep3_in_buffer[11]=block_checksum;
                payload_length=(uint8_t)(4u+DCB_SIZE);
            }
            break;

        case GATEWAY_CMD_DEXT_RESET:
            if (ep3_out_buffer[6]!=0u) {
                ep3_in_buffer[7]=GATEWAY_BAD_LENGTH;
            } else {
                ep3_in_buffer[8]=P4_IN;
                dext_adamnet_init();
                dext_adamnet_reset();
                ep3_in_buffer[9]=P4_IN;
                payload_length=2u;
            }
            break;

        case GATEWAY_CMD_DEVICE4_SEQ:
            if (ep3_out_buffer[6]!=0u) {
                ep3_in_buffer[7]=GATEWAY_BAD_LENGTH;
            } else {
                uint8_t transaction;
                uint8_t offset=9u;
                const uint32_t started=timer_millis();

                /*
                 * Four MN_STATUS requests to device 4 in true half-duplex.
                 * Each record is 12 bytes: command, response length,
                 * initial/final LSR, elapsed ms and up to six raw RX bytes.
                 */
                ep3_in_buffer[8]=4u;
                for (transaction=0u; transaction<4u; ++transaction) {
                    dext_probe_result_t result;
                    uint8_t stored_length;
                    uint16_t elapsed;

                    (void)dext_adamnet_control(0x14u,&result);
                    elapsed=(uint16_t)(timer_millis()-started);
                    stored_length=result.reply_length;
                    if (stored_length>6u) stored_length=6u;

                    ep3_in_buffer[offset+0u]=result.sent_command;
                    ep3_in_buffer[offset+1u]=stored_length;
                    ep3_in_buffer[offset+2u]=result.first_rx_status;
                    ep3_in_buffer[offset+3u]=result.line_status;
                    ep3_in_buffer[offset+4u]=(uint8_t)elapsed;
                    ep3_in_buffer[offset+5u]=(uint8_t)(elapsed>>8);
                    for (i=0u; i<6u; ++i) {
                        ep3_in_buffer[offset+6u+i]=
                            (i<stored_length) ? result.reply[i] : 0u;
                    }
                    offset=(uint8_t)(offset+12u);
                    if (transaction<3u) timer_delay_ms(100u);
                }
                payload_length=49u;
            }
            break;

        default:
            ep3_in_buffer[3]=(uint8_t)(GATEWAY_CMD_ERROR|GATEWAY_RESPONSE_BIT);
            ep3_in_buffer[7]=GATEWAY_BAD_COMMAND;
            break;
        }
    }
    ep3_in_buffer[6]=payload_length;
}

static void gateway_make_stream_packet(void)
{
    uint8_t i;
    uint8_t chunk_length;
    const uint16_t remaining=
        (uint16_t)(ADAMNET_DISK_BLOCK_SIZE-ep3_stream_offset);

    chunk_length=(remaining>54u) ? 54u : (uint8_t)remaining;
    for (i=0u; i<64u; ++i) ep3_in_buffer[i]=0u;
    ep3_in_buffer[0]=GATEWAY_MAGIC0;
    ep3_in_buffer[1]=GATEWAY_MAGIC1;
    ep3_in_buffer[2]=GATEWAY_PROTOCOL;
    ep3_in_buffer[3]=(uint8_t)(GATEWAY_CMD_STREAM_DATA|GATEWAY_RESPONSE_BIT);
    ep3_in_buffer[4]=ep3_stream_sequence;
    ep3_in_buffer[5]=ep3_stream_device;
    ep3_in_buffer[6]=(uint8_t)(chunk_length+2u);
    ep3_in_buffer[7]=GATEWAY_OK;
    ep3_in_buffer[8]=(uint8_t)ep3_stream_offset;
    ep3_in_buffer[9]=(uint8_t)(ep3_stream_offset>>8);
    (void)dext_adamnet_disk_read_chunk(
        ep3_stream_offset,&ep3_in_buffer[10],chunk_length);

    ep3_stream_offset=(uint16_t)(ep3_stream_offset+chunk_length);
    if (ep3_stream_offset>=ADAMNET_DISK_BLOCK_SIZE)
        ep3_stream_active=false;
}

static void ep0_copy(uint8_t length)
{
    uint8_t i;
    for (i=0; i<length; ++i) ep0_buffer[i]=*descriptor_pointer++;
    descriptor_remaining-=length;
}

static void ep0_start(uint8_t length)
{
    UEP0_T_LEN=length;
    UEP0_CTRL=USB_R_TOG|USB_T_TOG|USB_R_ACK|USB_T_ACK;
}

static void ep0_stall(void)
{
    setup_request=0xFFu;
    UEP0_T_LEN=0;
    UEP0_CTRL=USB_R_TOG|USB_T_TOG|USB_R_STALL|USB_T_STALL;
}

static void handle_setup(void)
{
    uint8_t length=0;
    const uint8_t request_type=ep0_buffer[0];
    const uint8_t requested_length=ep0_buffer[6];

    if (USB_RX_LEN!=8u) { ep0_stall(); return; }
    setup_request=ep0_buffer[1];

    if ((request_type&0x60u)==0x20u) {
        if (setup_request==HID_SET_IDLE || setup_request==HID_SET_REPORT) ep0_start(0);
        else ep0_stall();
        return;
    }
    if ((request_type&0x60u)!=0u) { ep0_stall(); return; }
    descriptor_remaining=requested_length;

    switch (setup_request) {
    case USB_GET_DESCRIPTOR:
        switch (ep0_buffer[3]) {
        case DESC_DEVICE: descriptor_pointer=device_descriptor; length=sizeof(device_descriptor); break;
        case DESC_CONFIGURATION: descriptor_pointer=configuration_descriptor; length=sizeof(configuration_descriptor); break;
        case DESC_REPORT:
            if (ep0_buffer[4]==0u) {
                descriptor_pointer=gateway_report_descriptor;
                length=sizeof(gateway_report_descriptor);
            } else { ep0_stall(); return; }
            break;
        case DESC_STRING:
            switch (ep0_buffer[2]) {
            case 0: descriptor_pointer=language_string; length=sizeof(language_string); break;
            case 1: descriptor_pointer=manufacturer_string; length=sizeof(manufacturer_string); break;
            case 2: descriptor_pointer=product_string; length=sizeof(product_string); break;
            case 3: descriptor_pointer=serial_string; length=sizeof(serial_string); break;
            default: ep0_stall(); return;
            }
            break;
        default: ep0_stall(); return;
        }
        if (descriptor_remaining>length) descriptor_remaining=length;
        length=(descriptor_remaining>EP0_SIZE)?EP0_SIZE:descriptor_remaining;
        ep0_copy(length); ep0_start(length); break;
    case USB_SET_ADDRESS: pending_address=ep0_buffer[2]&0x7Fu; ep0_start(0); break;
    case USB_SET_CONFIGURATION: configuration_value=ep0_buffer[2]; ep0_start(0); break;
    case USB_GET_CONFIGURATION: ep0_buffer[0]=configuration_value; ep0_start(1); break;
    case USB_GET_STATUS:
        ep0_buffer[0]=0; ep0_buffer[1]=0;
        ep0_start((requested_length<2u)?requested_length:2u); break;
    case USB_GET_INTERFACE: ep0_buffer[0]=0; ep0_start(1); break;
    case USB_CLEAR_FEATURE:
    case USB_SET_INTERFACE: ep0_start(0); break;
    default: ep0_stall(); break;
    }
}

static void handle_ep0_in(void)
{
    uint8_t length;
    if (setup_request==USB_GET_DESCRIPTOR) {
        length=(descriptor_remaining>EP0_SIZE)?EP0_SIZE:descriptor_remaining;
        ep0_copy(length); UEP0_T_LEN=length; UEP0_CTRL^=USB_T_TOG;
    } else if (setup_request==USB_SET_ADDRESS) {
        USB_DEV_AD=(USB_DEV_AD&USB_UDA_GP_BIT)|pending_address;
        UEP0_T_LEN=0; UEP0_CTRL=USB_R_ACK|USB_T_NAK; setup_request=0xFFu;
    } else {
        UEP0_T_LEN=0; UEP0_CTRL=USB_R_ACK|USB_T_NAK; setup_request=0xFFu;
    }
}

void usb_device_init(void)
{
    IE_USB=0; USB_CTRL=0;
    uep4_1_mod&=(uint8_t)~0x0Cu;
    uep0_dma_high=0; uep0_dma_low=0;
    uep3_dma_high=0x01u; uep3_dma_low=0x00u;
    UEP0_CTRL=USB_R_ACK|USB_T_NAK; UEP0_T_LEN=0;
    uep2_3_mod=(uep2_3_mod&(uint8_t)~0xF0u)|0xC0u;
    UEP3_CTRL=USB_AUTO_TOG|USB_R_ACK|USB_T_NAK; UEP3_T_LEN=0;
    setup_request=0xFFu; pending_address=0; configuration_value=0;
    ep3_busy=false;
    ep3_request_pending=false; ep3_request_length=0u;
    ep3_stream_active=false; ep3_stream_offset=0u;
    USB_INT_FG=0xFFu;
    USB_INT_EN=USB_UIE_SUSPEND|USB_UIE_TRANSFER|USB_UIE_BUS_RST;
    USB_DEV_AD=0;
    USB_CTRL=USB_UC_DEV_PU_EN|USB_UC_INT_BUSY|USB_UC_DMA_EN;
    UDEV_CTRL|=USB_UD_PORT_EN; IE_USB=1;
}

bool usb_device_configured(void) { return configuration_value!=0u; }

void usb_device_task(void)
{
    uint8_t length;

    if (ep3_busy) return;

    if (ep3_request_pending) {
        /* ADAMnet operations may block briefly, but never inside the USB ISR. */
        length=ep3_request_length;
        gateway_make_response(length);
        ep3_request_pending=false;
    } else if (ep3_stream_active) {
        gateway_make_stream_packet();
    } else {
        return;
    }
    ep3_busy=true;
    UEP3_T_LEN=64u;
    UEP3_CTRL=(UEP3_CTRL&(uint8_t)~0x0Fu)|USB_R_NAK|USB_T_ACK;
}

void usb_isr(void) __interrupt(8)
{
    if (UIF_TRANSFER) {
        const uint8_t token=USB_INT_ST&(USB_TOKEN_MASK|USB_ENDPOINT_MASK);
        if (token==USB_TOKEN_SETUP) handle_setup();
        else if (token==USB_TOKEN_IN) handle_ep0_in();
        else if (token==USB_TOKEN_OUT) { UEP0_T_LEN=0; UEP0_CTRL=USB_R_ACK|USB_T_NAK; }
        else if (token==(USB_TOKEN_OUT|3u)) {
            uint8_t length=USB_RX_LEN;
            if (length>64u) length=64u;
            /* A new command cancels any abandoned stream from the host. */
            ep3_stream_active=false;
            ep3_stream_offset=0u;
            ep3_request_length=length;
            ep3_request_pending=true;
            UEP3_T_LEN=0u;
            UEP3_CTRL=(UEP3_CTRL&(uint8_t)~0x0Fu)|USB_R_NAK|USB_T_NAK;
        } else if (token==(USB_TOKEN_IN|3u)) {
            ep3_busy=false;
            UEP3_T_LEN=0;
            UEP3_CTRL=(UEP3_CTRL&(uint8_t)~0x0Fu)|USB_R_ACK|USB_T_NAK;
        }
        UIF_TRANSFER=0;
    }
    if (UIF_BUS_RST) {
        UEP0_CTRL=USB_R_ACK|USB_T_NAK; UEP0_T_LEN=0; USB_DEV_AD=0;
        configuration_value=0; ep3_busy=false;
        ep3_request_pending=false; ep3_request_length=0u;
        ep3_stream_active=false; ep3_stream_offset=0u;
        UEP3_T_LEN=0; UEP3_CTRL=USB_AUTO_TOG|USB_R_ACK|USB_T_NAK;
        setup_request=0xFFu; UIF_BUS_RST=0;
    }
    if (UIF_SUSPEND) UIF_SUSPEND=0;
}
