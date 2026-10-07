/**
 * @file joy.c
 * @brief Joystick Atari (DB9) sobre el Puerto 1 del FPGA.
 *
 * Ver joy.h para el contrato de la API y el mapeo de bits.
 */
#include "joy.h"


/* ============================================================================
 * REGISTROS DEL PUERTO 1
 * ============================================================================
 * Los mismos que usa el TM1638 (bits 0-2), compartidos por byte.
 * Por eso aquí solo se tocan los bits 3-7.
 */

#ifndef JOY_PORT
#define JOY_PORT  (*(volatile uint8_t *)0xC000)   /* datos Puerto 1 (lectura) */
#endif

#ifndef JOY_CFG
#define JOY_CFG   (*(volatile uint8_t *)0xC002)   /* config Puerto 1 (0=out 1=in) */
#endif


/* ============================================================================
 * IMPLEMENTACIÓN
 * ============================================================================ */

void joy_init(void) {
    /* Bits 3-7 como ENTRADA (1), preservando los bits 0-2 del TM1638.
     *
     * READ-MODIFY-WRITE obligatorio: escribir un valor fijo como 0xF8 pisaría
     * la dirección de los 3 pines del display y el TM1638 dejaría de responder. */
    JOY_CFG = (uint8_t)((JOY_CFG & 0x07) | JOY_MASK);
}

uint8_t joy_read(void) {
    uint8_t raw;
    uint8_t act;

    /* Enmascarar para ignorar los bits 3-7 del TM1638 y los no usados. */
    raw = (uint8_t)(JOY_PORT & JOY_MASK);

#if JOY_ACTIVE_LOW
    /* Activo por nivel bajo: pulsado = 0. Invertir para que 1 = pulsado.
     * La inversión se hace sobre los bits ya enmascarados. */
    raw = (uint8_t)(~raw & JOY_MASK);
#endif

    act = 0;

    if (raw & JOY_LEFT)  act |= JOY_A_LEFT;
    if (raw & JOY_RIGHT) act |= JOY_A_RIGHT;
    if (raw & JOY_UP)    act |= JOY_A_UP;
    if (raw & JOY_DOWN)  act |= JOY_A_DOWN;
    if (raw & JOY_FIRE)  act |= JOY_A_FIRE;

    return act;
}
