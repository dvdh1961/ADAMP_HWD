#include "ch559_sdcc.h"
#include "joypads.h"

static bool scan_keypad_mode;
static int8_t joy1_keypad;
static int8_t joy2_keypad;
static uint8_t joy1_dirs;
static uint8_t joy2_dirs;
static bool joy1_fire;
static bool joy2_fire;

static int8_t decode_keypad(uint8_t bits)
{
    /* Exacte waarheidstabel uit jGetPad() van het werkende Keil-project. */
    switch (bits & 0x0Fu) {
    case 0x03u: return 0;
    case 0x08u: return 1;
    case 0x04u: return 2;
    case 0x09u: return 3;
    case 0x07u: return 4;
    case 0x06u: return 5;
    case 0x01u: return 6;
    case 0x0Cu: return 7;
    case 0x0Eu: return 8;
    case 0x02u: return 9;
    case 0x0Au: return 10; /* * */
    case 0x05u: return 11; /* # */
    default: return -1;
    }
}

void joypads_init(void)
{
    /* Joypad 1: E=P1.4 en H=P1.7 zijn uitgangen; de overige lijnen ingangen. */
    P1_DIR = 0x90u;
    P1_PU  = 0x6Fu;
    P1 = 0xFFu;

    /*
     * Joypad 1 I zit op P3.0.
     * Joypad 2 A..G zitten op P3.1..P3.7.
     * De volledige P3-poort is daardoor ingang met pull-ups.
     */
    /* Joypad 2 E (P3.5) is een selectielijn en wordt uitgang. */
    P3_DIR = 0x20u;
    P3_PU  = 0xDFu;
    P3 |= 0x20u;

    /* Joypad 2 H en I zitten respectievelijk op P4.0 en P4.1. */
    /* Joypad 2 H (P4.0) is select-uitgang; I (P4.1) blijft ingang. */
    P4_DIR = (P4_DIR & (uint8_t)~0x02u) | 0x01u;
    P4_PU  |= 0x02u;
    P4_OUT |= 0x01u;

    joy1_keypad=joy2_keypad=-1;
    joy1_dirs=joy2_dirs=0u;
    joy1_fire=joy2_fire=false;

    /* Begin zoals Keil in keypadmodus: E laag en H hoog. */
    P1 &= (uint8_t)~0x10u;
    P1 |= 0x80u;
    P3 &= (uint8_t)~0x20u;
    P4_OUT |= 0x01u;
    scan_keypad_mode=true;
}

uint8_t joypad1_read_keypad_lines(void)
{
    uint8_t result;

    /* Keypad selecteren: E (P1.4) laag, H (P1.7) hoog. */
    P1 |= 0x80u;
    P1 &= (uint8_t)~0x10u;

    /* A..D -> bit 0..3, F -> bit 4 en I -> bit 5. */
    result = (uint8_t)((uint8_t)~P1 & 0x0Fu);
    if ((P1 & 0x20u) == 0u) result |= 0x10u;
    if ((P3 & 0x01u) == 0u) result |= 0x20u;

    /* Beide selectielijnen terug in rust hoog zetten. */
    P1 |= 0x10u;
    return result;
}

uint8_t joypad2_read_keypad_lines(void)
{
    uint8_t result;

    /* Keypad selecteren: E (P3.5) laag, H (P4.0) hoog. */
    P4_OUT |= 0x01u;
    P3 &= (uint8_t)~0x20u;

    /* A..D (P3.1..4) -> bit 0..3, F (P3.6) -> bit 4, I -> bit 5. */
    result = (uint8_t)(((uint8_t)~P3 >> 1) & 0x0Fu);
    if ((P3 & 0x40u) == 0u) result |= 0x10u;
    if ((P4_IN & 0x02u) == 0u) result |= 0x20u;

    P3 |= 0x20u;
    return result;
}

static uint8_t read_joy1_directions(void)
{
    uint8_t directions=0u;
    if ((P1 & 0x01u)==0u) directions|=JOY_DIR_UP;
    if ((P1 & 0x08u)==0u) directions|=JOY_DIR_RIGHT;
    if ((P1 & 0x02u)==0u) directions|=JOY_DIR_DOWN;
    if ((P1 & 0x04u)==0u) directions|=JOY_DIR_LEFT;
    return directions;
}

static uint8_t read_joy2_directions(void)
{
    uint8_t directions=0u;
    if ((P3 & 0x02u)==0u) directions|=JOY_DIR_UP;
    if ((P3 & 0x10u)==0u) directions|=JOY_DIR_RIGHT;
    if ((P3 & 0x04u)==0u) directions|=JOY_DIR_DOWN;
    if ((P3 & 0x08u)==0u) directions|=JOY_DIR_LEFT;
    return directions;
}

void joypads_scan_step(void)
{
    if (scan_keypad_mode) {
        /* De selectielijnen stonden 5 ms in keypadmodus: nu uitlezen. */
        joy1_keypad=decode_keypad((uint8_t)((uint8_t)~P1 & 0x0Fu));
        joy2_keypad=decode_keypad((uint8_t)(((uint8_t)~P3 >> 1) & 0x0Fu));
        if ((P1 & 0x20u)==0u) joy1_keypad=12;
        if ((P3 & 0x40u)==0u) joy2_keypad=12;

        /* Voor de volgende 5 ms naar joystickmodus: E hoog, H laag. */
        P1 |= 0x10u;
        P1 &= (uint8_t)~0x80u;
        P3 |= 0x20u;
        P4_OUT &= (uint8_t)~0x01u;
        scan_keypad_mode=false;
    } else {
        joy1_dirs=read_joy1_directions();
        joy2_dirs=read_joy2_directions();
        joy1_fire=((P1 & 0x20u)==0u);
        joy2_fire=((P3 & 0x40u)==0u);

        /* Voor de volgende 5 ms terug naar keypadmodus. */
        P1 &= (uint8_t)~0x10u;
        P1 |= 0x80u;
        P3 &= (uint8_t)~0x20u;
        P4_OUT |= 0x01u;
        scan_keypad_mode=true;
    }
}

int8_t joypad1_key(void)
{
    if (joy1_keypad>=0) return joy1_keypad;
    return joy1_fire?12:-1;
}

int8_t joypad2_key(void)
{
    if (joy2_keypad>=0) return joy2_keypad;
    return joy2_fire?12:-1;
}

uint8_t joypad1_directions(void) { return joy1_dirs; }
uint8_t joypad2_directions(void) { return joy2_dirs; }

joypad_raw_t joypad1_read_raw(void)
{
    joypad_raw_t result;

    /* A..H rechtstreeks uit P1; actieve-lage signalen worden omgekeerd. */
    result = (joypad_raw_t)((uint8_t)~P1);

    /* I: P3.0 wordt bit 8 van het resultaat. */
    if ((P3 & 0x01u) == 0u) {
        result |= JOYPAD_I;
    }

    return result;
}

joypad_raw_t joypad2_read_raw(void)
{
    joypad_raw_t result;
    const uint8_t p3_pressed = (uint8_t)~P3;
    const uint8_t p4_pressed = (uint8_t)~P4_IN;

    /* P3.1..P3.7 worden resultaatbits A..G. */
    result = (joypad_raw_t)((p3_pressed >> 1) & 0x7Fu);

    /* P4.0 = H en P4.1 = I. */
    if (p4_pressed & 0x01u) {
        result |= JOYPAD_H;
    }
    if (p4_pressed & 0x02u) {
        result |= JOYPAD_I;
    }

    return result;
}
