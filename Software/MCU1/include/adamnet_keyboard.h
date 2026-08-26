#ifndef ADAMNET_KEYBOARD_H
#define ADAMNET_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

void adamnet_keyboard_init(void);

/*
 * Niet-blokkerende servicefunctie. Roep deze zo vaak mogelijk aan.
 * Geeft true terug zodra een volledige toetscode beschikbaar is.
 */
bool adamnet_keyboard_task(uint8_t *keycode);

/*
 * Diagnosecode voor RUN_JK:
 * 0 = nog geen herkend antwoord, 1 = NM_NACK, 2 = NM_ACK of toets ontvangen,
 * 4 = onbekend antwoord. Een toets wordt daarnaast direct aan main() gemeld.
 */
uint8_t adamnet_keyboard_diagnostic(void);

#endif
