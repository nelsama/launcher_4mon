/**
 * ============================================================================
 * APP LAUNCHER - Lanzador de aplicaciones para Monitor 6502
 * ============================================================================
 *
 * Lanza binarios desde SD Card con interfaz UART y TM1638.
 *
 * Controles UART:
 *   W/w            - Subir en la lista
 *   S/s            - Bajar en la lista
 *   ENTER (0x0D)   - Cargar y ejecutar seleccion
 *
 * Controles TM1638:
 *   Key 1 (S1)     - Subir en la lista
 *   Key 2 (S2)     - Bajar en la lista
 *   Key 8 (S8)     - Cargar y ejecutar seleccion
 *
 * ROM API utilizada:
 *   - rom_sd_init()       - Inicializar SD Card
 *   - rom_mfs_mount()     - Montar sistema de archivos
 *   - rom_mfs_list_via_zp - Listar archivos
 *   - rom_mfs_open()      - Abrir archivo
 *   - rom_mfs_get_size()  - Obtener tamaño
 *   - rom_mfs_read_via_zp - Leer datos
 *   - rom_mfs_close()     - Cerrar archivo
 *   - rom_uart_putc()     - Enviar caracteres UART
 *   - rom_uart_puts()     - Enviar strings UART
 *   - rom_uart_getc()     - Leer caracter UART
 *   - rom_uart_rx_ready() - Verificar datos UART
 *   - rom_delay_ms()      - Delays
 *
 * Librería externa:
 *   - TM1638 v2.0        - Display y teclado
 *
 * Mapa de memoria:
 *   $0800-$2CA9  - Código, datos y BSS del launcher (~9.3KB)
 *   $2D00-$3DFF  - Buffer de carga para apps (hasta 4.3KB)
 *   $3E00-$3FFF  - Stack del sistema (512 bytes)
 * ============================================================================
 */

#include <stdint.h>
#include "romapi.h"
#include "tm1638.h"


/* ============================================================================
 * CONSTANTES
 * ============================================================================ */

#define MAX_APPS            60      /* Máximo número de aplicaciones listables */
#define APP_NAME_LEN        13      /* name(12) + null + margen */
#define LOAD_ADDR           0x0800  /* Dirección de carga de los binarios */
/* El buffer temporal ya no es necesario: rom_mfs_load_run()
 * carga el archivo directamente en $0800 sobrescribiendonos. */

/* TM1638 key mapping */
#define TM_KEY_UP           1       /* S1 - Subir */
#define TM_KEY_DOWN         2       /* S2 - Bajar */
#define TM_KEY_SELECT       8       /* S8 - Seleccionar/Ejecutar */

/* UART key mapping */
#define UART_KEY_UP_W       'w'
#define UART_KEY_UP_W_CAPS  'W'
#define UART_KEY_DOWN_S     's'
#define UART_KEY_DOWN_S_CAPS 'S'
#define UART_KEY_UP_8       '8'
#define UART_KEY_DOWN_2     '2'
#define UART_KEY_SELECT     0x0D    /* Enter */
#define UART_KEY_QUIT       'q'
#define UART_KEY_QUIT_CAPS  'Q'

/* TM1638 key mapping */
#define TM_KEY_QUIT         7       /* S7 - Volver al monitor */


/* ============================================================================
 * ESTRUCTURA DE DATOS
 * ============================================================================ */

typedef struct {
    char     name[APP_NAME_LEN]; /* Nombre del archivo */
    uint16_t size;                 /* Tamaño en bytes */
} app_entry_t;


/* ============================================================================
 * FUNCIONES AUXILIARES UART
 * ============================================================================ */

void uart_print(const char *s) {
    while (*s) rom_uart_putc(*s++);
}

void uart_println(const char *s) {
    uart_print(s);
    uart_print("\r\n");
}

void uart_print_num16(uint16_t n) {
    char buf[6];
    uint8_t i = 0;
    uint8_t j;
    char tmp;

    if (n == 0) {
        rom_uart_putc('0');
        return;
    }

    while (n > 0) {
        buf[i++] = (n % 10) + '0';
        n /= 10;
    }

    /* Invertir */
    for (j = 0; j < i / 2; j++) {
        tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }

    for (j = 0; j < i; j++) {
        rom_uart_putc(buf[j]);
    }
}

void uart_print_hex8(uint8_t n) {
    const char hex[] = "0123456789ABCDEF";
    rom_uart_putc(hex[(n >> 4) & 0x0F]);
    rom_uart_putc(hex[n & 0x0F]);
}

void uart_print_hex16(uint16_t n) {
    uart_print_hex8((uint8_t)(n >> 8));
    uart_print_hex8((uint8_t)(n & 0xFF));
}


/* ============================================================================
 * FUNCIONES AUXILIARES TM1638
 * ============================================================================ */

/* Muestra el índice en el display: "  1/10" */
void tm1638_show_index(uint8_t current, uint8_t total) {
    char buf[9];
    uint8_t i;
    uint8_t digit;
    uint8_t n;
    uint8_t pos;

    /* Inicializar con espacios */
    for (i = 0; i < 8; i++) {
        buf[i] = ' ';
    }
    buf[8] = '\0';

    /* Formato: "  1/10" en los últimos 5 dígitos */
    /* Escribir total en las últimas posiciones */
    n = total;
    pos = 7;
    if (n == 0) {
        buf[pos--] = '0';
    } else {
        while (n > 0 && pos > 3) {
            digit = n % 10;
            buf[pos--] = digit + '0';
            n /= 10;
        }
    }

    buf[pos--] = '/';

    /* Escribir current */
    n = current;
    if (n == 0) {
        buf[pos--] = '0';
    } else {
        while (n > 0 && pos > 0) {
            digit = n % 10;
            buf[pos--] = digit + '0';
            n /= 10;
        }
    }

    tm1638_show_text(buf);
}

/* Muestra un mensaje de error en TM1638 */
void tm1638_show_error(const char* msg) {
    tm1638_show_text(msg);
}

/* Muestra el nombre del archivo en el display (8 chars max) */
void tm1638_show_filename(const char* name) {
    char buf[9];
    uint8_t i;
    for (i = 0; i < 8; i++) {
        if (name[i] == 0) {
            for (; i < 8; i++) buf[i] = ' ';
            break;
        }
        buf[i] = name[i];
    }
    buf[8] = 0;
    tm1638_show_text(buf);
}


/* ============================================================================
 * ESCANEO DE APLICACIONES EN SD
 * ============================================================================
 *
 * mfs_mount() cachea el directorio MFS en $0264+ en RAM.
 * La funcion $BF12 (mfs_list) esta ROTA en la ROM, por eso NO la usamos.
 * En su lugar leemos directamente la cache del directorio.
 *
 * Cada entrada en $0264 ocupa 32 bytes:
 *   [0-11]:  nombre del archivo (12 bytes, null-terminated)
 *   [12-13]: cluster de inicio / reservado
 *   [14-15]: tamano del archivo en bytes (little-endian)
 *   [16-31]: padding / reservado
 * Maximo 16 entradas.
 * ============================================================================ */

#define FAT_ADDR     0x0264   /* Donde mfs_mount cachea el directorio */
#define FAT_ENTRIES  16       /* Maximo entradas en el directorio */
#define FAT_ENTRY_SZ 32       /* Cada entrada ocupa 32 bytes */

uint8_t scan_apps(app_entry_t* apps, uint8_t max_apps) {
    uint8_t count = 0;
    uint8_t result;
    uint8_t i;
    uint8_t j;
    uint8_t k;
    uint8_t* entry;
    uint16_t file_size;

    uart_print("Escaneando SD Card...\r\n");

    /* Inicializar SD */
    result = rom_sd_init();
    if (result != SD_OK) {
        uart_print("  ERROR: SD init fallo (codigo ");
        uart_print_num16(result);
        uart_print(")\r\n");
        return 0;
    }
    uart_print("  SD init OK\r\n");

    /* Montar MFS (cachea directorio en $0264) */
    result = rom_mfs_mount();
    if (result != MFS_OK) {
        uart_print("  ERROR: MFS mount fallo (codigo ");
        uart_print_num16(result);
        uart_print(")\r\n");
        return 0;
    }
    uart_print("  MFS mount OK\r\n");

    /* Debug: quitar test mfs_get_size, ya sabemos que size esta en bytes 14-15 */

    /* Leer entradas del directorio desde $0264 */
    for (i = 0; i < FAT_ENTRIES && count < max_apps; i++) {
        entry = (uint8_t*)(FAT_ADDR + i * FAT_ENTRY_SZ);

        if (entry[0] == 0) {
            continue;
        }

        /* Leer tamano REAL desde bytes 14-15 (little-endian) */
        file_size = entry[14] | ((uint16_t)entry[15] << 8);

        if (file_size == 0 || file_size > 30000) {
            continue;
        }

        /* Copiar nombre (bytes 0-11, max 12 chars, truncar en null) */
        k = 0;
        for (j = 0; j < 12 && entry[j] != 0; j++) {
            apps[count].name[k++] = entry[j];
        }
        apps[count].name[k] = '\0';
        apps[count].size = file_size;
        count++;
    }

    uart_print("  Archivos encontrados: ");
    uart_print_num16(count);
    uart_print("\r\n");

    return count;
}


/* Helper: vuelve al monitor 6502 saltando a $8000 */
void quit_to_monitor(void) {
    tm1638_clear_display();
    uart_print("\r\nVolviendo al monitor...\r\n");
    rom_delay_ms(200);
    ((void (*)(void))0x8000)();
}


/* ============================================================================
 * MOSTRAR LISTA DE APLICACIONES
 * ============================================================================ */

/* Numero de lineas de una pagina del listado */
#define LIST_LINES  12  /* 10 apps + separator + footer */

/* Muestra el listado completo (con header y todo) */
void show_app_list_full(app_entry_t* apps, uint8_t count, uint8_t selected, uint8_t scroll_offset) {
    uint8_t i;
    uint8_t display_count;
    uint8_t idx;
    uint8_t len;

    uart_print("\r\n\r\n=========================================\r\n");
    uart_print("  APP LAUNCHER - Monitor 6502\r\n");
    uart_print("=========================================\r\n");
    uart_print("  W/S=Navegar  ENTER=Ejec  Q=Salir\r\n");
    uart_print("-----------------------------------------\r\n");

    display_count = (count - scroll_offset < 10) ? (count - scroll_offset) : 10;

    for (i = 0; i < display_count; i++) {
        idx = scroll_offset + i;

        if (idx == selected) {
            uart_print(" > ");
        } else {
            uart_print("   ");
        }

        uart_print_num16(idx + 1);
        uart_print(": ");
        uart_print(apps[idx].name);

        len = 0;
        while (apps[idx].name[len] != '\0' && len < 12) len++;
        while (len < 12) {
            rom_uart_putc(' ');
            len++;
        }

        uart_print(" (");
        uart_print_num16(apps[idx].size);
        uart_print(" bytes)\r\n");
    }

    uart_print("-----------------------------------------\r\n");
    uart_print("  App ");
    uart_print_num16(selected + 1);
    uart_print("/");
    uart_print_num16(count);
    uart_print("\r\n");
}

/* Actualiza SOLO las lineas del listado (sin header), subiendo el cursor */
void show_app_list_update(app_entry_t* apps, uint8_t count, uint8_t selected, uint8_t scroll_offset) {
    uint8_t i;
    uint8_t display_count;
    uint8_t idx;
    uint8_t len;

    /* Subir cursor hasta el inicio del listado */
    display_count = (count - scroll_offset < 10) ? (count - scroll_offset) : 10;
    {
        uint8_t lines = display_count + 2;  /* apps + separator + footer */
        rom_uart_putc(0x1B);  /* ESC */
        rom_uart_putc('[');
        uart_print_num16(lines);
        rom_uart_putc('A');   /* cursor up */
    }

    for (i = 0; i < display_count; i++) {
        idx = scroll_offset + i;

        rom_uart_putc(0x1B);  /* ESC */
        rom_uart_putc('[');
        rom_uart_putc('K');   /* clear line */

        if (idx == selected) {
            uart_print(" > ");
        } else {
            uart_print("   ");
        }

        uart_print_num16(idx + 1);
        uart_print(": ");
        uart_print(apps[idx].name);

        len = 0;
        while (apps[idx].name[len] != '\0' && len < 12) len++;
        while (len < 12) {
            rom_uart_putc(' ');
            len++;
        }

        uart_print(" (");
        uart_print_num16(apps[idx].size);
        uart_print(" bytes)\r\n");
    }

    /* Separator */
    rom_uart_putc(0x1B);
    rom_uart_putc('[');
    rom_uart_putc('K');
    uart_print("-----------------------------------------\r\n");

    /* Footer */
    rom_uart_putc(0x1B);
    rom_uart_putc('[');
    rom_uart_putc('K');
    uart_print("  App ");
    uart_print_num16(selected + 1);
    uart_print("/");
    uart_print_num16(count);
    uart_print("\r\n");
}


/* ============================================================================
 * CARGAR Y EJECUTAR APLICACIÓN
 * ============================================================================ */

void launch_app(app_entry_t* app) {
    uart_print("\r\n=========================================\r\n");
    uart_print("  CARGANDO Y EJECUTANDO\r\n");
    uart_print("=========================================\r\n");
    uart_print("  Archivo: ");
    uart_print(app->name);
    uart_print("\r\n");
    uart_print("  Cargando en $0800 y ejecutando...\r\n");

    rom_delay_ms(200);

    /* rom_mfs_load_run carga el archivo a la direccion y salta a ella.
     * ZP: $F4-$F5 = name ptr, $F6-$F7 = addr */
    rom_mfs_load_run(app->name, LOAD_ADDR);

    /* Si retorna, algo fallo */
    uart_print("  ERROR: No se pudo cargar\r\n");
    uart_print("  Presione ENTER para continuar\r\n");
    while (rom_uart_getc() != UART_KEY_SELECT);
}


/* ============================================================================
 * MAIN - PUNTO DE ENTRADA
 * ============================================================================ */

int main(void) {
    app_entry_t apps[MAX_APPS];
    uint8_t app_count;
    uint8_t selected = 0;
    uint8_t scroll_offset = 0;
    uint8_t last_tm1638_key = 0;
    uint8_t key;
    char uart_char;
    uint8_t redraw_mode = 0;  /* 0=none, 1=full, 2=partial */
    uint8_t needs_tm1638_update = 1;
    uint8_t last_selected = 0;
    uint8_t last_scroll = 0;

    /* ============================================
     * INICIALIZACIÓN
     * ============================================ */

    /* Banner de bienvenida por UART */
    uart_print("\r\n\r\n");
    uart_print("=========================================\r\n");
    uart_print("  APP LAUNCHER v1.0\r\n");
    uart_print("  Monitor 6502 - Tang Nano 9K\r\n");
    uart_print("=========================================\r\n\r\n");

    /* Inicializar TM1638 */
    uart_print("Inicializando TM1638...\r\n");
    tm1638_init();
    tm1638_set_brightness(5);
    tm1638_show_text(" LAUNCH ");
    uart_print("  OK\r\n\r\n");

    /* Escanear aplicaciones en SD */
    app_count = scan_apps(apps, MAX_APPS);

    if (app_count == 0) {
        uart_print("\r\n=========================================\r\n");
        uart_print("  NO SE ENCONTRARON APLICACIONES\r\n");
        uart_print("=========================================\r\n");
        uart_print("\r\n  Posibles causas:\r\n");
        uart_print("  1. SD Card no insertada\r\n");
        uart_print("  2. SD no formateada como FAT12/16\r\n");
        uart_print("  3. No hay archivos en la raiz\r\n");
        uart_print("\r\n  Presione RESET para reintentar\r\n");

        tm1638_show_error("NO  FILE");

        while (1) {
            /* Bucle infinito - no hay apps */
            rom_delay_ms(1000);
        }
    }

    uart_print("\r\n  Controles: W/S=Subir/Bajar  ENTER=Ejecutar  Q=Salir\r\n");
    uart_print("  TM1638:  K1=Subir  K2=Bajar  K8=Ejecutar  K7=Salir\r\n\r\n");
    rom_delay_ms(500);

    /* Dibujar listado inicial completo */
    last_scroll = scroll_offset;
    last_selected = selected;
    show_app_list_full(apps, app_count, selected, scroll_offset);
    needs_tm1638_update = 1;

    /* ============================================
     * BUCLE PRINCIPAL
     * ============================================ */

    while (1) {
        /* Redibujar si es necesario */
        if (redraw_mode == 1) {
            show_app_list_full(apps, app_count, selected, scroll_offset);
            last_scroll = scroll_offset;
            last_selected = selected;
            redraw_mode = 0;
        } else if (redraw_mode == 2) {
            show_app_list_update(apps, app_count, selected, scroll_offset);
            last_selected = selected;
            redraw_mode = 0;
        }

        /* Actualizar TM1638 si es necesario */
        if (needs_tm1638_update) {
            tm1638_show_filename(apps[selected].name);
            needs_tm1638_update = 0;
        }

        /* ============================================
         * ENTRADA POR UART
         * ============================================ */

        if (rom_uart_rx_ready()) {
            uart_char = rom_uart_getc();

            /* Echo del carácter */
            /* (no hacemos echo para no ensuciar la pantalla) */

            if (uart_char == UART_KEY_UP_W || uart_char == UART_KEY_UP_W_CAPS || uart_char == UART_KEY_UP_8) {
                if (selected > 0) {
                    selected--;
                    if (selected < scroll_offset) {
                        scroll_offset = selected;
                        redraw_mode = 1;  /* full: cambio de pagina */
                    } else {
                        redraw_mode = 2;  /* partial: mismo scroll */
                    }
                    needs_tm1638_update = 1;
                }
            }
            else if (uart_char == UART_KEY_DOWN_S || uart_char == UART_KEY_DOWN_S_CAPS || uart_char == UART_KEY_DOWN_2) {
                if (selected < app_count - 1) {
                    selected++;
                    if (selected >= scroll_offset + 10) {
                        scroll_offset = selected - 9;
                        redraw_mode = 1;  /* full: cambio de pagina */
                    } else {
                        redraw_mode = 2;  /* partial: mismo scroll */
                    }
                    needs_tm1638_update = 1;
                }
            }
            else if (uart_char == UART_KEY_QUIT || uart_char == UART_KEY_QUIT_CAPS) {
                quit_to_monitor();
            }
            else if (uart_char == UART_KEY_SELECT) {
                /* Ejecutar aplicación seleccionada */
                uart_print("\r\n");
                launch_app(&apps[selected]);

                /* Si volvemos (el binario retornó o falló), continuar */
                uart_print("\r\n  Aplicacion finalizada. Reiniciando lanzador...\r\n\r\n");
                redraw_mode = 1;
                needs_tm1638_update = 1;

                /* Reescaneamos por si cambió algo */
                tm1638_show_text("RESCAN ");
                app_count = scan_apps(apps, MAX_APPS);
                if (app_count == 0) {
                    uart_print("  No hay apps disponibles\r\n");
                    tm1638_show_error("NO  FILE");
                    while (1) {
                        rom_delay_ms(1000);
                    }
                }
                if (selected >= app_count) {
                    selected = app_count - 1;
                }
                scroll_offset = 0;
            }
        }

        /* ============================================
         * ENTRADA POR TM1638
         * ============================================ */

        key = tm1638_get_key_pressed();

        if (key > 0 && key != last_tm1638_key) {
            last_tm1638_key = key;

            if (key == TM_KEY_UP) {
                if (selected > 0) {
                    selected--;
                    if (selected < scroll_offset) {
                        scroll_offset = selected;
                        redraw_mode = 1;
                    } else {
                        redraw_mode = 2;
                    }
                    needs_tm1638_update = 1;
                }
            }
            else if (key == TM_KEY_DOWN) {
                if (selected < app_count - 1) {
                    selected++;
                    if (selected >= scroll_offset + 10) {
                        scroll_offset = selected - 9;
                        redraw_mode = 1;
                    } else {
                        redraw_mode = 2;
                    }
                    needs_tm1638_update = 1;
                }
            }
            else if (key == TM_KEY_SELECT) {
                tm1638_show_text(" LOADING");
                launch_app(&apps[selected]);

                /* Si volvemos */
                tm1638_show_text("RESCAN ");
                app_count = scan_apps(apps, MAX_APPS);
                if (app_count == 0) {
                    tm1638_show_error("NO  FILE");
                    while (1) {
                        rom_delay_ms(1000);
                    }
                }
                if (selected >= app_count) {
                    selected = app_count - 1;
                }
                scroll_offset = 0;
                redraw_mode = 1;
                needs_tm1638_update = 1;
            }
            else if (key == TM_KEY_QUIT) {
                quit_to_monitor();
            }
        }
        else if (key == 0) {
            last_tm1638_key = 0;
        }

        /* Pequeña pausa para no saturar */
        rom_delay_ms(50);
    }

    /* return 0; -- No llegamos aquí */
    return 0;
}
