/**
 * @file console.h
 * @brief Salida de texto dual: UART (terminal serial) y HDMI (Core de Video).
 *
 * El launcher usa esta capa para escribir en ambos destinos a la vez, sin
 * duplicar la lógica de impresión. Envuelve la ROM API del monitor (UART) y
 * la librería videocore-6502-cc65 (modo texto del core de vídeo).
 *
 * Modo texto del core: pantalla de 40x30 celdas. La fuente ASCII ($20-$7F) ya
 * reside en la VRAM del core, así que no hay que cargar tileset.
 *
 * Regla del core de vídeo: escribir la VRAM SOLO en VBLANK. Las funciones de
 * esta capa que tocan la pantalla deben llamarse dentro de la ventana de
 * VBLANK (entre vc_wait_vblank() y vc_wait_vblank_end()).
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

/* Geometría del modo texto, expuesta para la lógica de layout del launcher. */
#define CONSOLE_COLS    40
#define CONSOLE_ROWS    30

/* Paletas disponibles para texto. La fuente pinta VC_COLOR3, así que el color
 * de la letra lo decide la paleta de la celda. Se exponen aquí para que el
 * launcher no dependa de video.h. */
#define CONSOLE_PAL_NORMAL      0   /* texto normal */
#define CONSOLE_PAL_SELECTED    2   /* resaltado (selección del menú) */

/* ============================================================================
 * INICIALIZACIÓN
 * ============================================================================ */

/**
 * @brief Inicializa el modo texto del core de vídeo.
 *
 * Espera a que la VRAM esté lista (VIDEO_READY) y limpia la pantalla a
 * espacios. Debe llamarse una sola vez al arrancar, antes de cualquier
 * otra función de esta capa.
 *
 * La UART no necesita inicialización aquí: el monitor ya la deja lista.
 */
void console_init(void);

/* ============================================================================
 * SALIDA DE TEXTO
 * ============================================================================ */

/**
 * @brief Imprime una cadena en la fila actual, avanzando a la siguiente.
 *
 * Escribe en UART y en la pantalla. Respeta los '\n' de la cadena como salto
 * de línea explícito. Al llegar a la columna 40 la pantalla envuelve sola
 * (vc_put_str lo hace), pero esta función evita escribir fuera del viewport.
 *
 * @param s Cadena terminada en null
 */
void console_print(const char *s);

/**
 * @brief Escribe una cadena en una posición fija de la pantalla (sin avanzar).
 *
 * Solo pantalla: útil para pintar el menú fila a fila sin ensuciar la UART.
 *
 * @param col  Columna inicial (0-39)
 * @param row  Fila inicial (0-29)
 * @param s    Cadena terminada en null
 */
void console_put_at(uint8_t col, uint8_t row, const char *s);

/**
 * @brief Igual que console_put_at pero con paleta explícita.
 *
 * La paleta determina el color del texto (la fuente pinta VC_COLOR3). Usar
 * VC_BGPAL_0..3 de video.h para resaltar (selección del menú).
 *
 * @param col    Columna inicial (0-39)
 * @param row    Fila inicial (0-29)
 * @param s      Cadena terminada en null
 * @param paleta VC_BGPAL_0..3
 */
void console_put_at_pal(uint8_t col, uint8_t row, const char *s, uint8_t paleta);

/* ============================================================================
 * CONTROL DE PANTALLA
 * ============================================================================ */

/**
 * @brief Escribe una cadena rellenando con espacios hasta 'width' columnas.
 *
 * A diferencia de console_put_at, sobrescribe SIEMPRE las 'width' celdas. Eso
 * garantiza que al redibujar una fila no queden restos de texto anterior, asi
 * que no hace falta limpiar la fila antes.
 *
 * Si la cadena es mas larga que 'width', se trunca (la pantalla tiene 40
 * columnas: pasarse hace que la linea se desborde a la fila siguiente).
 *
 * @param col    Columna inicial (0-39)
 * @param row    Fila (0-29)
 * @param s      Cadena terminada en null
 * @param width  Ancho total a escribir, incluyendo relleno
 * @param paleta VC_BGPAL_0..3
 */
void console_put_padded(uint8_t col, uint8_t row, const char *s,
                        uint8_t width, uint8_t paleta);

/**
 * @brief Escribe un numero decimal alineado a la derecha en 'width' columnas.
 *
 * @param col    Columna inicial (0-39)
 * @param row    Fila (0-29)
 * @param n      Numero a escribir
 * @param width  Ancho total (rellena con espacios a la izquierda)
 * @param paleta VC_BGPAL_0..3
 */
void console_put_num(uint8_t col, uint8_t row, uint16_t n,
                     uint8_t width, uint8_t paleta);

/**
 * @brief Limpia la pantalla completa (solo HDMI; la UART no se puede limpiar).
 */
void console_clear(void);

/**
 * @brief Reinicia el cursor de console_print a la fila 0.
 *
 * Útil para redibujar la pantalla desde arriba sin borrarla.
 */
void console_home(void);

/**
 * @brief Borra una fila concreta de la pantalla.
 * @param row Fila (0-29)
 */
void console_clear_row(uint8_t row);

/* ============================================================================
 * SINCRONIZACIÓN CON EL CORE DE VÍDEO
 * ============================================================================ */

/**
 * @brief Espera la ventana de VBLANK (fuera de la zona visible).
 *
 * Llamar ANTES de escribir en la pantalla. Toda modificación de la VRAM debe
 * ocurrir aquí para evitar parpadeos.
 */
void console_vblank_begin(void);

/**
 * @brief Cierra la ventana de VBLANK.
 *
 * Llamar DESPUÉS de terminar las escrituras a pantalla.
 */
void console_vblank_end(void);

#endif /* CONSOLE_H */
