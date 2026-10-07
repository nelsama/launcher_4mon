/**
 * @file joy.h
 * @brief Joystick Atari (DB9) sobre el Puerto 1 del FPGA.
 *
 * Lee 5 entradas activas por nivel bajo (UP, DOWN, LEFT, RIGHT, FIRE) de los
 * bits 3-7 del puerto. Los bits 0-2 los ocupa el TM1638, así que TODA escritura
 * al registro de configuración es read-modify-write: nunca se escribe un valor
 * fijo, o se pisaría la configuración del display.
 *
 * Al ser activo por nivel bajo, requiere pull-ups en el FPGA. Sin ellos los
 * bits flotan y las lecturas son erráticas con el joystick quieto.
 */
#ifndef JOY_H
#define JOY_H

#include <stdint.h>

/* Bits del joystick en el Puerto 1 (mapeo real de la placa). */
#define JOY_RIGHT   0x08   /* bit 3 */
#define JOY_LEFT    0x10   /* bit 4 */
#define JOY_DOWN    0x20   /* bit 5 */
#define JOY_UP      0x40   /* bit 6 */
#define JOY_FIRE    0x80   /* bit 7 */

/* Los 5 bits del joystick. Se usa para enmascarar lecturas y para configurar
 * los pines como entrada sin tocar los bits del TM1638. */
#define JOY_MASK    0xF8

/* El joystick Atari es activo por nivel bajo (0 = pulsado). */
#define JOY_ACTIVE_LOW  1

/* Acciones devueltas por joy_read(), como bitmask. El código que la usa no
 * necesita conocer los bits del puerto ni la polaridad. */
#define JOY_A_LEFT   0x01
#define JOY_A_RIGHT  0x02
#define JOY_A_UP     0x04
#define JOY_A_DOWN   0x08
#define JOY_A_FIRE   0x10

/**
 * @brief Configura los bits 3-7 del Puerto 1 como entrada.
 *
 * Read-modify-write sobre el registro de configuración: preserva los bits 0-2
 * que pertenecen al TM1638. Llamar una vez, antes de joy_read().
 */
void joy_init(void);

/**
 * @brief Lee el estado del joystick.
 * @return Bitmask de JOY_A_* (0 = sin pulsar). Ya normalizado a activo-alto.
 */
uint8_t joy_read(void);

#endif /* JOY_H */
