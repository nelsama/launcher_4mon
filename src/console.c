/**
 * @file console.c
 * @brief Salida de texto dual: UART (terminal serial) y HDMI (Core de Video).
 *
 * Ver console.h para el contrato de la API.
 *
 * @author Nelson Figueroa
 * @date 2026
 */
#include "console.h"
#include "romapi.h"
#include "video.h"


/* ============================================================================
 * ESTADO INTERNO
 * ============================================================================ */

/* Cursor para console_print: fila del viewport donde se escribirá la próxima
 * cadena. Las columnas las gestiona vc_put_str internamente. */
static uint8_t cur_row;


/* ============================================================================
 * INICIALIZACIÓN
 * ============================================================================ */

void console_init(void) {
    /* El core puede tardar en inicializar la VRAM tras el arranque.
     * VIDEO_READY garantiza que se puede escribir sin corromperla. */
    vc_wait_ready();
    vc_text_init();

    cur_row = 0;
}


/* ============================================================================
 * SALIDA DE TEXTO
 * ============================================================================ */

/**
 * @brief Escribe una cadena en pantalla tratando '\n' como salto de línea.
 *
 * vc_put_str ya envuelve al llegar a la columna 40, pero no interpreta '\n'.
 * Como el launcher usa tanto cadenas cortas ("OK") como largas con saltos,
 * aquí se recorre la cadena pintando carácter a carácter: al topar con '\n'
 * se salta de fila, y al llegar a la columna 40 se envuelve.
 *
 * @param row Fila inicial; devuelve la fila siguiente a la última escrita.
 */
static uint8_t screen_puts(uint8_t row, const char *s) {
    uint8_t col;

    col = 0;

    while (*s) {
        if (*s == '\n') {
            row++;
            if (row >= CONSOLE_ROWS) {
                row = 0;    /* envolver arriba */
            }
            col = 0;
        } else if (*s != '\r') {
            vc_put_cell(col, row, (uint8_t)*s);
            col++;
            if (col >= CONSOLE_COLS) {
                col = 0;
                row++;
                if (row >= CONSOLE_ROWS) {
                    row = 0;
                }
            }
        }
        /* '\r' se ignora en pantalla: el salto lo da el '\n' */
        s++;
    }

    return row;
}

void console_print(const char *s) {
    /* UART primero: el eco serial sale completo, con sus '\r\n'. */
    const char *p;
    for (p = s; *p; p++) {
        rom_uart_putc(*p);
    }

    /* Pantalla: misma cadena, interpretando saltos. */
    cur_row = screen_puts(cur_row, s);
}

void console_put_at(uint8_t col, uint8_t row, const char *s) {
    vc_put_str(col, row, s);
}

void console_put_at_pal(uint8_t col, uint8_t row, const char *s, uint8_t paleta) {
    vc_put_str_pal(col, row, s, paleta);
}

void console_put_padded(uint8_t col, uint8_t row, const char *s,
                        uint8_t width, uint8_t paleta) {
    uint8_t i;

    for (i = 0; i < width; i++) {
        uint8_t c;

        if (*s != '\0') {
            c = (uint8_t)*s;
            s++;
        } else {
            c = VC_CHAR_SPACE;
        }

        vc_put_cell(col + i, row, c);
        vc_set_cell_attr(col + i, row, paleta, 0);
    }
}

void console_put_num(uint8_t col, uint8_t row, uint16_t n,
                     uint8_t width, uint8_t paleta) {
    char buf[6];    /* 5 digitos + null: uint16 llega hasta 65535 */
    uint8_t len;
    uint8_t i;
    uint8_t pad;

    /* Convertir a decimal (al reves) */
    len = 0;
    if (n == 0) {
        buf[len] = '0';
        len++;
    } else {
        while (n > 0 && len < 5) {
            buf[len] = (char)('0' + (n % 10));
            n /= 10;
            len++;
        }
    }

    /* Relleno a la izquierda */
    pad = (width > len) ? (width - len) : 0;
    for (i = 0; i < pad; i++) {
        vc_put_cell(col + i, row, VC_CHAR_SPACE);
        vc_set_cell_attr(col + i, row, paleta, 0);
    }

    /* Digitos, del mas significativo al menos */
    for (i = 0; i < len; i++) {
        vc_put_cell(col + pad + i, row, (uint8_t)buf[len - 1 - i]);
        vc_set_cell_attr(col + pad + i, row, paleta, 0);
    }
}


/* ============================================================================
 * CONTROL DE PANTALLA
 * ============================================================================ */

void console_clear(void) {
    vc_text_init();
    cur_row = 0;
}

void console_home(void) {
    cur_row = 0;
}

void console_clear_row(uint8_t row) {
    uint8_t col;

    for (col = 0; col < CONSOLE_COLS; col++) {
        vc_put_cell(col, row, VC_CHAR_SPACE);
    }
}


/* ============================================================================
 * SINCRONIZACIÓN CON EL CORE DE VÍDEO
 * ============================================================================ */

void console_vblank_begin(void) {
    vc_wait_vblank();
}

void console_vblank_end(void) {
    vc_wait_vblank_end();
}
