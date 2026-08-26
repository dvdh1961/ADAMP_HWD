#include <stdint.h>
#include "ch559_sdcc.h"
#include "joypads.h"
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
__xdata __at(0x0000) uint8_t ep0_buffer[EP0_SIZE];
/* Exact hetzelfde DMA-adres als Ep1Buffer in de werkende Keil-code. */
__xdata __at(0x000A) uint8_t ep1_in_buffer[64];
__xdata __at(0x004A) uint8_t ep2_in_buffer[64];

static const __code uint8_t device_descriptor[] = {
    18,DESC_DEVICE, 0x10,0x01, 0,0,0,EP0_SIZE,
    0x8A,0x25, 0x35,0x00, 0x50,0x10, 1,2,3,1
};

/* Exact dezelfde 65-byte keyboard-reportstructuur als het Keil-project. */
static const __code uint8_t keyboard_report_descriptor[] = {
    0x05,0x01,0x09,0x06,0xA1,0x01,
    0x05,0x07,0x19,0xE0,0x29,0xE7,0x15,0x00,0x25,0x01,
    0x75,0x01,0x95,0x08,0x81,0x02,
    0x95,0x01,0x75,0x08,0x81,0x01,
    0x95,0x03,0x75,0x01,0x05,0x08,0x19,0x01,0x29,0x03,0x91,0x02,
    0x95,0x01,0x75,0x05,0x91,0x01,
    0x95,0x06,0x75,0x08,0x15,0x00,0x26,0xFF,0x00,
    0x05,0x07,0x19,0x00,0x2A,0xFF,0x00,0x81,0x00,0xC0
};

/*
 * Twee Game Pad Application Collections op één HID-interface. Windows maakt
 * per top-level collection een gamecontroller; Linux maakt afzonderlijke
 * inputapparaten. Report ID 1 is de linker en Report ID 2 de rechter poort.
 * Rapport: ID, X, Y, 13 knoppen plus 3 paddingbits = 5 bytes.
 */
static const __code uint8_t gamepad_report_descriptor[] = {
    0x05,0x01,0x09,0x05,0xA1,0x01,0x85,0x01,
    0x09,0x30,0x09,0x31,0x15,0x81,0x25,0x7F,
    0x75,0x08,0x95,0x02,0x81,0x02,
    0x05,0x09,0x19,0x01,0x29,0x11,0x15,0x00,0x25,0x01,
    0x75,0x01,0x95,0x11,0x81,0x02,
    0x75,0x01,0x95,0x07,0x81,0x03,0xC0,

    0x05,0x01,0x09,0x05,0xA1,0x01,0x85,0x02,
    0x09,0x30,0x09,0x31,0x15,0x81,0x25,0x7F,
    0x75,0x08,0x95,0x02,0x81,0x02,
    0x05,0x09,0x19,0x01,0x29,0x11,0x15,0x00,0x25,0x01,
    0x75,0x01,0x95,0x11,0x81,0x02,
    0x75,0x01,0x95,0x07,0x81,0x03,0xC0
};

static const __code uint8_t configuration_descriptor[] = {
    9,DESC_CONFIGURATION,59,0,2,1,0,0xA0,50,
    9,0x04,0,0,1,0x03,0x01,0x01,0,
    9,0x21,0x10,0x01,0,1,DESC_REPORT,sizeof(keyboard_report_descriptor),0,
    7,0x05,0x81,0x03,8,0,0x18,
    9,0x04,1,0,1,0x03,0x00,0x00,0,
    9,0x21,0x10,0x01,0,1,DESC_REPORT,sizeof(gamepad_report_descriptor),0,
    7,0x05,0x82,0x03,8,0,0x0A
};

static const __code uint8_t language_string[] = {4,DESC_STRING,0x09,0x04};
static const __code uint8_t manufacturer_string[] = {12,DESC_STRING,'A',0,'D',0,'A',0,'M',0,'+',0};
static const __code uint8_t product_string[] = {
    28,DESC_STRING,'A',0,'D',0,'A',0,'M',0,'L',0,'i',0,'n',0,'k',0,' ',0,
    'D',0,'i',0,'a',0,'g',0
};
static const __code uint8_t serial_string[] = {16,DESC_STRING,'S',0,'D',0,'C',0,'C',0,'0',0,'1',0,'0',0};

static const __code uint8_t *descriptor_pointer;
static uint8_t descriptor_remaining, setup_request, pending_address, configuration_value;
static bool ep1_busy, release_pending, ep2_busy;

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
                descriptor_pointer=keyboard_report_descriptor;
                length=sizeof(keyboard_report_descriptor);
            } else if (ep0_buffer[4]==1u) {
                descriptor_pointer=gamepad_report_descriptor;
                length=sizeof(gamepad_report_descriptor);
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
    uep1_dma_high=0; uep1_dma_low=0x0Au;
    uep2_dma_high=0; uep2_dma_low=0x4Au;
    UEP0_CTRL=USB_R_ACK|USB_T_NAK; UEP0_T_LEN=0;
    uep4_1_mod=(uep4_1_mod&(uint8_t)~0xD0u)|0x40u;
    UEP1_CTRL=USB_AUTO_TOG|USB_T_NAK; UEP1_T_LEN=0;
    uep2_3_mod=(uep2_3_mod&(uint8_t)~0x0Du)|0x04u;
    UEP2_CTRL=USB_AUTO_TOG|USB_T_NAK; UEP2_T_LEN=0;
    setup_request=0xFFu; pending_address=0; configuration_value=0;
    ep1_busy=false; release_pending=false; ep2_busy=false;
    USB_INT_FG=0xFFu;
    USB_INT_EN=USB_UIE_SUSPEND|USB_UIE_TRANSFER|USB_UIE_BUS_RST;
    USB_DEV_AD=0;
    USB_CTRL=USB_UC_DEV_PU_EN|USB_UC_INT_BUSY|USB_UC_DMA_EN;
    UDEV_CTRL|=USB_UD_PORT_EN; IE_USB=1;
}

bool usb_device_configured(void) { return configuration_value!=0u; }

/*
 * Vertaal de door ADAMNet geleverde ASCII/speciale code naar USB HID.
 * Deze tabel volgt HIDSendKey() uit het werkende Keil-project. De pc-
 * toetsenbordindeling moet daarbij, net als vroeger, op US staan.
 */
static bool adam_code_to_hid(uint8_t code, uint8_t *modifier, uint8_t *usage)
{
    *modifier=0;
    *usage=0;

    if (code>='a' && code<='z') {
        *usage=(uint8_t)(code-'a'+0x04u);
        return true;
    }
    if (code>='A' && code<='Z') {
        *modifier=0x02u;
        *usage=(uint8_t)(code-'A'+0x04u);
        return true;
    }
    if (code>='1' && code<='9') {
        *usage=(uint8_t)(code-'1'+0x1Eu);
        return true;
    }

    switch (code) {
    case '!': *modifier=0x02u; *usage=0x1Eu; break;
    case '@': *modifier=0x02u; *usage=0x1Fu; break;
    case '#': *modifier=0x02u; *usage=0x20u; break;
    case '$': *modifier=0x02u; *usage=0x21u; break;
    case '%': *modifier=0x02u; *usage=0x22u; break;
    case '_': *modifier=0x02u; *usage=0x2Du; break;
    case '&': *modifier=0x02u; *usage=0x24u; break;
    case '*': *modifier=0x02u; *usage=0x25u; break;
    case '(': *modifier=0x02u; *usage=0x26u; break;
    case ')': *modifier=0x02u; *usage=0x27u; break;
    case '0': *usage=0x27u; break;
    case '`': *usage=0x35u; break;
    case '-': *usage=0x2Du; break;
    case '=': *modifier=0x02u; *usage=0x2Eu; break;
    case '+': *usage=0x2Eu; break;
    case '{': *usage=0x2Fu; break;
    case '[': *modifier=0x02u; *usage=0x2Fu; break;
    case '}': *usage=0x30u; break;
    case ']': *modifier=0x02u; *usage=0x30u; break;
    case ':': *usage=0x33u; break;
    case ';': *modifier=0x02u; *usage=0x33u; break;
    case '"': *usage=0x34u; break;
    case '\'': *modifier=0x02u; *usage=0x34u; break;
    case '<': *usage=0x36u; break;
    case ',': *modifier=0x02u; *usage=0x36u; break;
    case '>': *usage=0x37u; break;
    case '.': *modifier=0x02u; *usage=0x37u; break;
    case '?': *usage=0x38u; break;
    case '/': *modifier=0x02u; *usage=0x38u; break;
    case '|': *usage=0x31u; break;
    case '\\': *modifier=0x02u; *usage=0x31u; break;
    case ' ': *usage=0x2Cu; break;
    case 13u: *usage=0x28u; break;
    case 8u: *usage=0x2Au; break;
    case 151u: *usage=0x63u; break;
    case 148u: *usage=0x49u; break;
    case 9u:
    case 0xB9u: *usage=0x2Bu; break;
    case 1u: *modifier=0x01u; *usage=0x04u; break;
    case 3u: *modifier=0x01u; *usage=0x06u; break;
    case 4u: *modifier=0x01u; *usage=0x07u; break;
    case 22u: *modifier=0x01u; *usage=0x19u; break;
    case 129u: *usage=0x3Au; break;
    case 130u: *usage=0x3Bu; break;
    case 131u: *usage=0x3Cu; break;
    case 132u: *usage=0x3Du; break;
    case 133u: *usage=0x3Eu; break;
    case 134u: *usage=0x3Fu; break;
    case 137u: *usage=0x40u; break;
    case 138u: *usage=0x41u; break;
    case 139u: *usage=0x42u; break;
    case 140u: *usage=0x43u; break;
    case 141u: *usage=0x44u; break;
    case 142u: *usage=0x45u; break;
    case 27u: *usage=0x45u; break; /* Keil koppelde ESCAPE aan F12. */
    case 128u: *usage=0x4Au; break;
    case 163u: *usage=0x50u; break;
    case 160u: *usage=0x52u; break;
    case 161u: *usage=0x4Fu; break;
    case 162u: *usage=0x51u; break;
    default: return false;
    }
    return true;
}

bool usb_device_send_keyboard(uint8_t keycode)
{
    uint8_t i;
    uint8_t modifier;
    uint8_t usage;
    /*
     * Windows pollt EP1 pas nadat het HID-apparaat geconfigureerd is. De
     * automatische EP1-test bewees dat de host dit correct doet, terwijl onze
     * lokale configuration_value niet betrouwbaar gezet bleef. Alleen een
     * nog lopend rapport mag daarom een nieuwe verzending tegenhouden.
     */
    if (ep1_busy) return false;
    if (!adam_code_to_hid(keycode,&modifier,&usage)) return false;
    for (i=0; i<8u; ++i) ep1_in_buffer[i]=0;
    ep1_in_buffer[0]=modifier;
    ep1_in_buffer[2]=usage;
    ep1_busy=true; release_pending=true;
    UEP1_T_LEN=8u;
    UEP1_CTRL=(UEP1_CTRL&(uint8_t)~0x03u)|USB_T_ACK;
    return true;
}

bool usb_device_send_gamepad(uint8_t report_id, uint8_t directions, int8_t button_code)
{
    int8_t x=0;
    int8_t y=0;
    uint32_t buttons=0;

    if (ep2_busy || report_id<1u || report_id>2u) return false;

    if (button_code>=0 && button_code<=11) {
        buttons=(uint32_t)1u << (uint8_t)button_code;
    } else if (button_code==12) {
        buttons=0x1000u; /* Fire = knop 13. */
    }

    /* Horizontaal en verticaal worden onafhankelijk opgebouwd: diagonalen. */
    /*
     * Geen annulering bij twee gelijktijdig waargenomen bits. De klassieke
     * controllercontacten kunnen tijdens een diagonale overgang heel kort ook
     * de tegengestelde ingang laten meekomen. Een vaste prioriteit voorkomt
     * dat Windows daardoor X=0/Y=0 (exact het midden) ontvangt.
     */
    if ((directions&JOY_DIR_LEFT)!=0u) x=-127;
    else if ((directions&JOY_DIR_RIGHT)!=0u) x=127;
    if ((directions&JOY_DIR_UP)!=0u) y=-127;
    else if ((directions&JOY_DIR_DOWN)!=0u) y=127;

    /* Ruwe richtingcontacten extra zichtbaar als knoppen 14 t/m 17. */
    if ((directions&JOY_DIR_UP)!=0u)    buttons|=0x00002000UL;
    if ((directions&JOY_DIR_RIGHT)!=0u) buttons|=0x00004000UL;
    if ((directions&JOY_DIR_DOWN)!=0u)  buttons|=0x00008000UL;
    if ((directions&JOY_DIR_LEFT)!=0u)  buttons|=0x00010000UL;

    ep2_in_buffer[0]=report_id;
    ep2_in_buffer[1]=(uint8_t)x;
    ep2_in_buffer[2]=(uint8_t)y;
    ep2_in_buffer[3]=(uint8_t)buttons;
    ep2_in_buffer[4]=(uint8_t)(buttons>>8);
    ep2_in_buffer[5]=(uint8_t)(buttons>>16);
    ep2_busy=true;
    UEP2_T_LEN=6u;
    UEP2_CTRL=(UEP2_CTRL&(uint8_t)~0x03u)|USB_T_ACK;
    return true;
}

void usb_isr(void) __interrupt(8)
{
    if (UIF_TRANSFER) {
        const uint8_t token=USB_INT_ST&(USB_TOKEN_MASK|USB_ENDPOINT_MASK);
        if (token==USB_TOKEN_SETUP) handle_setup();
        else if (token==USB_TOKEN_IN) handle_ep0_in();
        else if (token==USB_TOKEN_OUT) { UEP0_T_LEN=0; UEP0_CTRL=USB_R_ACK|USB_T_NAK; }
        else if (token==(USB_TOKEN_IN|1u)) {
            if (release_pending) {
                uint8_t i;
                for (i=0; i<8u; ++i) ep1_in_buffer[i]=0;
                release_pending=false; UEP1_T_LEN=8u;
                UEP1_CTRL=(UEP1_CTRL&(uint8_t)~0x03u)|USB_T_ACK;
            } else {
                UEP1_T_LEN=0; UEP1_CTRL=(UEP1_CTRL&(uint8_t)~0x03u)|USB_T_NAK; ep1_busy=false;
            }
        } else if (token==(USB_TOKEN_IN|2u)) {
            UEP2_T_LEN=0;
            UEP2_CTRL=(UEP2_CTRL&(uint8_t)~0x03u)|USB_T_NAK;
            ep2_busy=false;
        }
        UIF_TRANSFER=0;
    }
    if (UIF_BUS_RST) {
        UEP0_CTRL=USB_R_ACK|USB_T_NAK; UEP0_T_LEN=0; USB_DEV_AD=0;
        configuration_value=0; ep1_busy=false; release_pending=false; ep2_busy=false;
        UEP1_T_LEN=0; UEP1_CTRL=USB_AUTO_TOG|USB_T_NAK;
        UEP2_T_LEN=0; UEP2_CTRL=USB_AUTO_TOG|USB_T_NAK;
        setup_request=0xFFu; UIF_BUS_RST=0;
    }
    if (UIF_SUSPEND) UIF_SUSPEND=0;
}
