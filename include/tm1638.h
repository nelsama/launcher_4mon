/**
 * @file tm1638.h
 * @brief Header file for TM1638 display and keyboard module interface.
 *
 * Interface para el control completo del módulo TM1638, incluyendo display de 7-segmentos
 * y teclado de 16 teclas. Compatible con CC65/C89.
 *
 * Funcionalidades:
 *   - Codificación de caracteres ASCII y hexadecimales
 *   - Control de display con common anode
 *   - Lectura de teclado con mapeo específico para QYF-TM1638
 *   - Funciones de utilidad para limpieza y control
 *
 * @author Nelson Figueroa
 * @date 2025
 */
#ifndef TM1638_H
#define TM1638_H

#include <stdint.h>

/* ============================================================================
 * DEFINICIONES Y CONSTANTES
 * ============================================================================ */

// Definiciones de puertos para TM1638 (solo si no están ya definidas)
#ifndef PORT_SALIDA
#define PORT_SALIDA         (*(volatile uint8_t*)0xC000)
#endif

#ifndef CONF_PORT_SALIDA
#define CONF_PORT_SALIDA    (*(volatile uint8_t*)0xC002)
#endif



// Parámetro de timing para delays del TM1638
#define timing_delay 0

/* ============================================================================
 * FUNCIONES DE CODIFICACIÓN Y DISPLAY
 * ============================================================================ */

// Funciones básicas de comunicación
void tm1638_delay(volatile uint16_t delay_counter2);
void tm1638_init(void);

// Funciones de codificación (versión completa con longitud personalizable)
void tm1638_encode_hex(const uint8_t* digits, uint8_t* segments, uint8_t digits_len);
void tm1638_encode_ascii(const char* chars, uint8_t* segments, uint8_t digits_len);
void tm1638_digits_common_anode(const uint8_t* digits, uint8_t* grids, uint8_t digits_len);

// Funciones de codificación simplificadas (asumen 8 dígitos automáticamente)
void tm1638_encode_hex8(const uint8_t* digits, uint8_t* segments);
void tm1638_encode_ascii8(const char* chars, uint8_t* segments);
void tm1638_number_to_segments8(uint32_t number, uint8_t* segments);
void tm1638_digits_common_anode8(const uint8_t* digits, uint8_t* grids);

// Funciones súper simplificadas (todo-en-uno)
void tm1638_show_text(const char* text);
void tm1638_show_hex(const uint8_t* hex_digits);
void tm1638_show_number(uint32_t number);

// Funciones de display
void tm1638_display(const uint8_t* grids);
void tm1638_display_with_brightness(const uint8_t* grids, uint8_t brightness);
void tm1638_clear_display(void);
void tm1638_set_brightness(uint8_t brightness);

/* ============================================================================
 * FUNCIONES DE TECLADO
 * ============================================================================ */

// Funciones de lectura de teclas
uint32_t tm1638_read_keys(void);
uint8_t tm1638_get_key_pressed(void);
uint8_t tm1638_get_all_keys_pressed(uint8_t* pressed_keys);

// Funciones auxiliares (normalmente no necesarias para el usuario final)
uint8_t tm1638_read_byte(void);

#endif // TM1638_H