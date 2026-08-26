#ifndef JOYPADS_H
#define JOYPADS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Iedere controller heeft negen signaallijnen A t/m I.
 * Een bit wordt 1 wanneer de bijbehorende actieve-lage ingang bediend is.
 */
typedef uint16_t joypad_raw_t;

#define JOYPAD_A 0x001u
#define JOYPAD_B 0x002u
#define JOYPAD_C 0x004u
#define JOYPAD_D 0x008u
#define JOYPAD_E 0x010u
#define JOYPAD_F 0x020u
#define JOYPAD_G 0x040u
#define JOYPAD_H 0x080u
#define JOYPAD_I 0x100u

#define JOY_DIR_UP    0x01u
#define JOY_DIR_RIGHT 0x02u
#define JOY_DIR_DOWN  0x04u
#define JOY_DIR_LEFT  0x08u

void joypads_init(void);
joypad_raw_t joypad1_read_raw(void);
joypad_raw_t joypad2_read_raw(void);

/*
 * Leest alleen de zes retourlijnen terwijl de controller in keypadmodus staat.
 * Bit 0..3 = A..D, bit 4 = F en bit 5 = I. Een gezet bit betekent actief-laag.
 * Voor deze eerste hardwaretest hoeven we de toetscode nog niet te decoderen.
 */
uint8_t joypad1_read_keypad_lines(void);
uint8_t joypad2_read_keypad_lines(void);

/*
 * Niet-blokkerende port van JP1keymap()/JP2keymap() uit AdamJPHID.
 * Roep joypads_scan_step() om de 5 ms aan. De key-functies leveren:
 * 0..9, 10='*', 11='#', 12=fire, 14=up, 15=right, 16=down, 17=left,
 * of -1 wanneer niets is ingedrukt.
 */
void joypads_scan_step(void);
int8_t joypad1_key(void);
int8_t joypad2_key(void);
uint8_t joypad1_directions(void);
uint8_t joypad2_directions(void);

#endif
