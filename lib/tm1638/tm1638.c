/**
 * @file tm1638.c
 * @brief TM1638 7-segment display and keyboard driver for 6502-based projects.
 *
 * Este archivo contiene funciones para controlar un módulo TM1638 LED display,
 * incluyendo codificación de caracteres, control de display y lectura de teclado.
 * Compatible con CC65/C89.
 *
 * Funcionalidades principales:
 * - Codificación de caracteres hexadecimales y ASCII a formato 7-segmentos
 * - Control completo del display con common anode
 * - Lectura de teclado de 16 teclas con mapeo específico para QYF-TM1638
 * - Funciones utilitarias para limpieza y control
 *
 * @author Nelson Figueroa
 * @date 2025
 */
#include "tm1638.h"

/* ============================================================================
 * DEFINICIONES DE HARDWARE Y MACROS
 * ============================================================================ */

#define TM_CLK_BIT   0
#define TM_DIO_BIT   1
#define TM_STB_BIT   2

#define TM_CLK_MASK  (1 << TM_CLK_BIT)
#define TM_DIO_MASK  (1 << TM_DIO_BIT)
#define TM_STB_MASK  (1 << TM_STB_BIT)

volatile uint8_t tmp_salida;
static uint8_t current_brightness = 4;  /* Mantener el brillo actual (por defecto 4) */

uint8_t TM_CLK_HIGH()  {return (tmp_salida |= TM_CLK_MASK);}
uint8_t TM_CLK_LOW()   {return (tmp_salida &= ~TM_CLK_MASK);}
uint8_t TM_DIO_HIGH()  {return (tmp_salida |= TM_DIO_MASK);}
uint8_t TM_DIO_LOW()   {return (tmp_salida &= ~TM_DIO_MASK);}
uint8_t TM_STB_HIGH()  {return (tmp_salida |= TM_STB_MASK);}
uint8_t TM_STB_LOW()   {return (tmp_salida &= ~TM_STB_MASK);}

/* Funciones para configuración dinámica de DIO */
void TM_DIO_CONFIG_OUTPUT(void) {
    CONF_PORT_SALIDA &= ~TM_DIO_MASK;  /* DIO como salida (0) */
}

void TM_DIO_CONFIG_INPUT(void) {
    CONF_PORT_SALIDA |= TM_DIO_MASK;   /* DIO como entrada (1) */
}

/* ============================================================================
 * FUNCIONES BÁSICAS DE COMUNICACIÓN
 * ============================================================================ */

/**
 * @brief Genera delay preciso para comunicación TM1638
 * @param delay_counter2 Número de iteraciones de delay
 */
void tm1638_delay(volatile uint16_t delay_counter2) {
    while(delay_counter2--){
        // Bloque de NOPs sin loop interno para timing más predecible
        asm("nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop");  // 10 NOPs exactos por iteración
    }
}

/**
 * @brief Escribe un bit al TM1638
 * @param bit Valor del bit (0 o 1)
 */
void tm1638_write_bit(uint8_t bit) {
    /* Configurar DIO como salida antes de escribir */
    TM_DIO_CONFIG_OUTPUT();
    
    PORT_SALIDA=TM_CLK_LOW();
    if (bit)
        PORT_SALIDA=TM_DIO_HIGH();
    else
        PORT_SALIDA=TM_DIO_LOW();
    tm1638_delay(timing_delay);
    PORT_SALIDA=TM_CLK_HIGH();
    tm1638_delay(timing_delay);
}

/**
 * @brief Escribe un byte al TM1638
 * @param data Byte a escribir
 */
void tm1638_write_byte(uint8_t data) {
    uint8_t i;
    PORT_SALIDA=TM_CLK_LOW();
    for (i = 0; i < 8; ++i) {
        tm1638_write_bit(data & 0x01);
        data >>= 1;
    }
    PORT_SALIDA=TM_CLK_HIGH();
}

/**
 * @brief Lee un byte del TM1638
 * @return Byte leído
 */
uint8_t tm1638_read_byte(void) {
    uint8_t byte_val = 0;
    uint8_t i;
    
    /* Configurar DIO como entrada antes de leer */
    TM_DIO_CONFIG_INPUT();
    PORT_SALIDA = TM_DIO_HIGH();  /* Pull-up para lectura */
    
    for (i = 0; i < 8; i++) {
        /* Pulso de reloj bajo */
        PORT_SALIDA = TM_CLK_LOW();
        tm1638_delay(timing_delay);
        
        /* Leer bit en flanco de subida */
        PORT_SALIDA = TM_CLK_HIGH();
        tm1638_delay(timing_delay);
        
        /* Leer el estado del puerto y extraer el bit DIO */
        if (PORT_SALIDA & TM_DIO_MASK) {
            byte_val |= (1 << i);
        }
    }
    
    return byte_val;
}

/**
 * @brief Envía un comando al TM1638
 * @param cmd Comando a enviar
 */
void tm1638_send_cmd(uint8_t cmd) {
    PORT_SALIDA=TM_CLK_HIGH();
    tm1638_delay(timing_delay);
    tm1638_write_byte(cmd);
}

/* ============================================================================
 * FUNCIONES DE CODIFICACIÓN Y DISPLAY
 * ============================================================================ */

/**
 * @brief Codifica dígitos hexadecimales a formato 7-segmentos
 * @param digits Array de dígitos hex (0x0-0xF)
 * @param segments Array de salida con códigos 7-segmentos
 * @param digits_len Número de dígitos a codificar
 */
void tm1638_encode_hex(const uint8_t* digits, uint8_t* segments, uint8_t digits_len) {
    const uint8_t segment_hexa_map[16] = {
        0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F,0x77,0x7C,0x39,0x5E,0x79,0x71
    };
    uint8_t i;
    for (i = 0; i < digits_len; i++) {
        segments[i] = segment_hexa_map[digits[i]];
    }
}

/**
 * @brief Codifica string ASCII a formato 7-segmentos
 * @param chars String ASCII de entrada
 * @param segments Array de salida con códigos 7-segmentos
 * @param digits_len Número de caracteres a codificar
 */
void tm1638_encode_ascii(const char* chars, uint8_t* segments, uint8_t digits_len) {
    /* Variables C89 al inicio */
    uint8_t i;
    uint8_t src_index;
    uint8_t char_code;
    
    /* Tabla optimizada - solo caracteres comunes para ahorrar memoria */
    static const uint8_t ascii_to_7seg[128] = {
        /* Caracteres de control (0-31): no representables */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

        /* Caracteres imprimibles (32-126) */
        0x00, /* 32: (espacio) */  0x40, /* 33: ! */      0x22, /* 34: " */      0x00, /* 35: # */
        0x6D, /* 36: $ */         0x00, /* 37: % */      0x00, /* 38: & */      0x02, /* 39: ' */
        0x24, /* 40: ( */         0x2C, /* 41: ) */      0x00, /* 42: * */      0x38, /* 43: + */
        0x08, /* 44: , */         0x40, /* 45: - */      0x80, /* 46: . */      0x41, /* 47: / */
        0x3F, /* 48: 0 */         0x06, /* 49: 1 */      0x5B, /* 50: 2 */      0x4F, /* 51: 3 */
        0x66, /* 52: 4 */         0x6D, /* 53: 5 */      0x7D, /* 54: 6 */      0x07, /* 55: 7 */
        0x7F, /* 56: 8 */         0x6F, /* 57: 9 */      0x48, /* 58: : */      0x48, /* 59: ; */
        0x30, /* 60: < */         0x48, /* 61: = */      0x24, /* 62: > */      0x53, /* 63: ? */
        0x5F, /* 64: @ */         0x77, /* 65: A */      0x7C, /* 66: b */      0x39, /* 67: C */
        0x5E, /* 68: d */         0x79, /* 69: E */      0x71, /* 70: F */      0x3D, /* 71: G */
        0x76, /* 72: H */         0x06, /* 73: I */      0x0E, /* 74: J */      0x00, /* 75: K */
        0x38, /* 76: L */         0x37, /* 77: M */      0x37, /* 78: N */      0x3F, /* 79: O */
        0x73, /* 80: P */         0x67, /* 81: q */      0x73, /* 82: R */      0x6D, /* 83: S */
        0x78, /* 84: t */         0x3E, /* 85: U */      0x00, /* 86: V */      0x00, /* 87: W */
        0x00, /* 88: X */         0x6E, /* 89: Y */      0x5B, /* 90: Z */      0x39, /* 91: [ */
        0x41, /* 92: \ */         0x0F, /* 93: ] */      0x50, /* 94: ^ */      0x08, /* 95: _ */
        0x04, /* 96: ` */         0x77, /* 97: a */      0x7C, /* 98: b */      0x58, /* 99: c */
        0x5E, /* 100: d */        0x7B, /* 101: e */     0x71, /* 102: f */     0x6F, /* 103: g */
        0x74, /* 104: h */        0x04, /* 105: i */     0x0E, /* 106: j */     0x00, /* 107: k */
        0x30, /* 108: l */        0x00, /* 109: m */     0x54, /* 110: n */     0x5C, /* 111: o */
        0x73, /* 112: p */        0x67, /* 113: q */     0x50, /* 114: r */     0x6D, /* 115: s */
        0x78, /* 116: t */        0x1C, /* 117: u */     0x00, /* 118: v */     0x00, /* 119: w */
        0x00, /* 120: x */        0x6E, /* 121: y */     0x5B, /* 122: z */     0x24, /* 123: { */
        0x06, /* 124: | */        0x2C, /* 125: } */     0x40  /* 126: ~ */
    };

    /* Bucle optimizado - precalcula índice y elimina aritmética repetitiva */
    for (i = 0; i < digits_len; i++) {
        src_index = digits_len - 1 - i;  /* Calcular una sola vez */
        char_code = (uint8_t)chars[src_index];  /* Cast explícito */
        
        /* Verificación de bounds optimizada */
        if (char_code > 126) {
            segments[i] = 0x00;  /* Caracter no válido */
        } else {
            segments[i] = ascii_to_7seg[char_code];  /* Acceso directo */
        }
    }
}

/**
 * @brief Mapea datos de segmentos a formato grid para display common anode
 * @param digits Array con datos de segmentos por dígito
 * @param grids Array de salida con datos de grid
 * @param digits_len Número de dígitos a procesar
 */
void tm1638_digits_common_anode(const uint8_t* digits, uint8_t* grids, uint8_t digits_len) {
    /* Todas las variables declaradas al inicio (C89) */
    uint8_t grid;
    uint8_t digit;
    uint8_t segs;
    uint8_t grid_mask;
    uint8_t digit_mask;
    
    /* Inicialización eficiente usando asignaciones directas */
    grids[0] = 0x00; grids[1] = 0x00; grids[2] = 0x00; grids[3] = 0x00;
    grids[4] = 0x00; grids[5] = 0x00; grids[6] = 0x00; grids[7] = 0x00;

    /* Bucle principal optimizado para 6502 */
    grid_mask = 0x01;  /* Inicializar máscara de segmento */
    for (grid = 0; grid < 8; grid++) {
        digit_mask = 0x01;  /* Reinicializar máscara de dígito */
        
        for (digit = 0; digit < digits_len; digit++) {
            segs = digits[digit];
            
            /* Usar máscaras precalculadas para evitar shifts repetidos */
            if (segs & grid_mask) {
                grids[grid] |= digit_mask;
            }
            
            digit_mask <<= 1;  /* Siguiente dígito */
        }
        
        grid_mask <<= 1;  /* Siguiente segmento */
    }
}

/* ============================================================================
 * FUNCIONES SIMPLIFICADAS (8 DÍGITOS AUTOMÁTICO)
 * ============================================================================ */

/**
 * @brief Codifica dígitos hexadecimales a formato 7-segmentos (8 dígitos automático)
 * @param digits Array de 8 dígitos hex (0x0-0xF)
 * @param segments Array de salida con 8 códigos 7-segmentos
 */
void tm1638_encode_hex8(const uint8_t* digits, uint8_t* segments) {
    tm1638_encode_hex(digits, segments, 8);
}

/**
 * @brief Codifica string ASCII a formato 7-segmentos (8 caracteres automático)
 * @param chars String ASCII de 8 caracteres
 * @param segments Array de salida con 8 códigos 7-segmentos
 */
void tm1638_encode_ascii8(const char* chars, uint8_t* segments) {
    tm1638_encode_ascii(chars, segments, 8);
}

/**
 * @brief Mapea datos de segmentos a formato grid para display (8 dígitos automático)
 * @param digits Array con 8 datos de segmentos
 * @param grids Array de salida con 8 datos de grid
 */
void tm1638_digits_common_anode8(const uint8_t* digits, uint8_t* grids) {
    tm1638_digits_common_anode(digits, grids, 8);
}

/**
 * @brief Función todo-en-uno: codifica texto ASCII y lo muestra directamente
 * @param text String ASCII de 8 caracteres a mostrar
 */
void tm1638_show_text(const char* text) {
    uint8_t segments[8];
    uint8_t grids[8];
    
    tm1638_encode_ascii8(text, segments);
    tm1638_digits_common_anode8(segments, grids);
    tm1638_display(grids);
}

/**
 * @brief Función todo-en-uno: codifica hex y lo muestra directamente
 * @param hex_digits Array de 8 dígitos hex (0x0-0xF) a mostrar
 */
void tm1638_show_hex(const uint8_t* hex_digits) {
    uint8_t segments[8];
    uint8_t grids[8];
    
    tm1638_encode_hex8(hex_digits, segments);
    tm1638_digits_common_anode8(segments, grids);
    tm1638_display(grids);
}

/**
 * @brief Convierte un número entero a array de segmentos 7-seg (8 dígitos automático)
 * @param number Número a convertir (0-99999999)
 * @param segments Array de salida con 8 códigos 7-segmentos
 */
void tm1638_number_to_segments8(uint32_t number, uint8_t* segments) {
    uint8_t i;
    uint32_t temp_number = number;
    
    /* Inicializar todos los segmentos como apagados (0x00) */
    for (i = 0; i < 8; i++) {
        segments[i] = 0x00;  /* Completamente apagado */
    }
    
    /* Codificar dígitos del número desde la derecha (posición 0 es la más derecha) */
    i = 0;  /* Empezar desde el dígito más a la derecha (posición 0) */
    
    if (number == 0) {
        /* Caso especial: mostrar solo "0" en la posición más derecha */
        segments[0] = 0x3F;  /* Patrón del "0" */
    } else {
        /* Convertir cada dígito del número */
        while (temp_number > 0 && i < 8) {
            uint8_t digit = temp_number % 10;
            
            /* Tabla de patrones de dígitos 0-9 */
            const uint8_t digit_patterns[10] = {
                0x3F, 0x06, 0x5B, 0x4F, 0x66,  /* 0, 1, 2, 3, 4 */
                0x6D, 0x7D, 0x07, 0x7F, 0x6F   /* 5, 6, 7, 8, 9 */
            };
            
            segments[i] = digit_patterns[digit];
            temp_number /= 10;
            i++;
        }
    }
}

/**
 * @brief Función todo-en-uno: muestra un número entero (alineado a la derecha)
 * @param number Número a mostrar (0-99999999)
 * 
 * Esta versión mejorada garantiza que los dígitos no usados estén completamente apagados
 */
void tm1638_show_number(uint32_t number) {
    uint8_t segments[8];
    uint8_t grids[8];
    
    /* Convertir número a segmentos */
    tm1638_number_to_segments8(number, segments);
    
    /* Convertir segmentos a formato grid y mostrar */
    tm1638_digits_common_anode8(segments, grids);
    tm1638_display(grids);
}

/**
 * @brief Muestra datos en el display TM1638
 * @param disps Array de 8 bytes con datos de segmentos a mostrar
 */
void tm1638_display(const uint8_t* disps) {
    /* Variables C89 al inicio */
    uint8_t dir;
    uint8_t current_byte;

    /* Inicialización optimizada de líneas de control */
    tmp_salida |= (TM_CLK_MASK | TM_STB_MASK);  /* CLK y STB HIGH simultáneamente */
    PORT_SALIDA = tmp_salida;
    tm1638_delay(timing_delay);

    /* Comando auto-increment */
    PORT_SALIDA = TM_STB_LOW();
    tm1638_delay(timing_delay);
    tm1638_send_cmd(0x40);
    PORT_SALIDA = TM_STB_HIGH();
    tm1638_delay(timing_delay);
    
    /* Comando dirección inicial */
    PORT_SALIDA = TM_STB_LOW();
    tm1638_send_cmd(0xC0);

    /* Bucle optimizado - elimina branches y reduce accesos al array */
    for (dir = 0; dir < 8; dir++) {
        current_byte = disps[dir];  /* Una sola lectura por dígito */
        tm1638_write_byte(current_byte);  /* Byte par (segmentos) */
        tm1638_write_byte(current_byte);  /* Byte impar (LEDs) */
    }

    /* Activación final del display */
    PORT_SALIDA = TM_STB_HIGH();
    tm1638_delay(timing_delay);
    
    /* NO cambiar brillo - respetar configuración actual */
}

/**
 * @brief Limpia completamente el display TM1638 con método robusto
 * 
 * Esta función hace una limpieza exhaustiva eliminando segmentos fantasma:
 * 1. Apaga el display temporalmente
 * 2. Limpia toda la memoria (múltiples pasadas)
 * 3. Reestablece el display limpio
 */
void tm1638_clear_display(void) {
    uint8_t i;

    /* Paso 1: Temporalmente apagar el display para evitar glitches */
    PORT_SALIDA = TM_STB_LOW();
    tm1638_send_cmd(0x80);  /* Display OFF */
    PORT_SALIDA = TM_STB_HIGH();
    tm1638_delay(timing_delay * 5);  /* Delay extra para estabilizar */
    

    /* Inicialización de líneas de control */
    tmp_salida |= (TM_CLK_MASK | TM_STB_MASK);
    PORT_SALIDA = tmp_salida;
    tm1638_delay(timing_delay);

    /* Comando auto-increment para escribir desde dirección 0xC0 */
    PORT_SALIDA = TM_STB_LOW();
    tm1638_delay(timing_delay);
    tm1638_send_cmd(0x40);  /* Auto-increment mode */
    PORT_SALIDA = TM_STB_HIGH();
    tm1638_delay(timing_delay);
        
    /* Comando dirección inicial 0xC0 */
    PORT_SALIDA = TM_STB_LOW();
    tm1638_send_cmd(0xC0);  /* Address 0xC0 */

    /* Escribir 16 bytes de ceros (8 dígitos + 8 LEDs) */
    for (i = 0; i < 16; i++) {
        tm1638_write_byte(0x00);  /* Todo apagado */
    }

    /* Finalizar comunicación */
    PORT_SALIDA = TM_STB_HIGH();
    tm1638_delay(timing_delay * 2);  /* Delay extra entre pasadas */

    
    /* Paso 3: Reencender display limpio manteniendo brillo actual */
    PORT_SALIDA = TM_STB_LOW();
    tm1638_send_cmd(0x88 + current_brightness);  /* Display ON con brillo actual */
    PORT_SALIDA = TM_STB_HIGH();
    tm1638_delay(timing_delay * 3);
}

/**
 * @brief Inicialización robusta del TM1638 (elimina estados residuales)
 * 
 * Esta función debe llamarse una vez al inicio del programa para:
 * 1. Resetear completamente el TM1638
 * 2. Limpiar cualquier estado residual
 * 3. Configurar modo de operación correcto
 */
void tm1638_init(void) {
    uint8_t i;
    
    /* Paso 1: Resetear líneas de control */
    tmp_salida = 0x00;
    PORT_SALIDA = tmp_salida;
    tm1638_delay(timing_delay * 10);  /* Delay largo para reset */
    
    /* Paso 2: Inicializar líneas de control correctamente */
    tmp_salida |= (TM_CLK_MASK | TM_STB_MASK);
    PORT_SALIDA = tmp_salida;
    tm1638_delay(timing_delay * 5);
    
    /* Paso 3: Secuencia de inicialización del TM1638 */
    PORT_SALIDA = TM_STB_LOW();
    tm1638_send_cmd(0x80);  /* Display OFF para configuración limpia */
    PORT_SALIDA = TM_STB_HIGH();
    tm1638_delay(timing_delay * 2);
    
    /* Paso 4: Configurar modo de datos (auto-increment) */
    PORT_SALIDA = TM_STB_LOW();
    tm1638_send_cmd(0x40);  /* Auto-increment mode */
    PORT_SALIDA = TM_STB_HIGH();
    tm1638_delay(timing_delay * 2);
    

    PORT_SALIDA = TM_STB_LOW();
    tm1638_send_cmd(0xC0);  /* Address 0xC0 (start) */
        
    /* Escribir 16 bytes de ceros */
    for (i = 0; i < 16; i++) {
        tm1638_write_byte(0x00);
    }
        
    PORT_SALIDA = TM_STB_HIGH();
    tm1638_delay(timing_delay * 3);

    /* Paso 6: Encender display con brillo medio */
    tm1638_set_brightness(4);  /* Brillo medio por defecto */
}

/**
 * @brief Muestra datos en el display TM1638 con brillo personalizado
 * @param disps Array de 8 bytes con datos de segmentos a mostrar
 * @param brightness Nivel de brillo (0-7): 0=mínimo, 7=máximo
 */
void tm1638_display_with_brightness(const uint8_t* disps, uint8_t brightness) {
    /* Variables C89 al inicio */
    uint8_t dir;
    uint8_t current_byte;

    /* Inicialización optimizada de líneas de control */
    tmp_salida |= (TM_CLK_MASK | TM_STB_MASK);  /* CLK y STB HIGH simultáneamente */
    PORT_SALIDA = tmp_salida;
    tm1638_delay(timing_delay);

    /* Comando auto-increment */
    PORT_SALIDA = TM_STB_LOW();
    tm1638_delay(timing_delay);
    tm1638_send_cmd(0x40);
    PORT_SALIDA = TM_STB_HIGH();
    tm1638_delay(timing_delay);
    
    /* Comando dirección inicial */
    PORT_SALIDA = TM_STB_LOW();
    tm1638_send_cmd(0xC0);

    /* Bucle optimizado - elimina branches y reduce accesos al array */
    for (dir = 0; dir < 8; dir++) {
        current_byte = disps[dir];  /* Una sola lectura por dígito */
        tm1638_write_byte(current_byte);  /* Byte par (segmentos) */
        tm1638_write_byte(current_byte);  /* Byte impar (LEDs) */
    }

    /* Activación final del display con brillo personalizado */
    PORT_SALIDA = TM_STB_HIGH();
    tm1638_delay(timing_delay);
    
    /* Usar brillo especificado */
    tm1638_set_brightness(brightness);
}

/**
 * @brief Configura el brillo del display TM1638
 * @param brightness Nivel de brillo (0-7): 0=mínimo, 7=máximo
 */
void tm1638_set_brightness(uint8_t brightness) {
    uint8_t brightness_cmd;
    
    /* Limitar brillo a rango válido (0-7) */
    if (brightness > 7) {
        brightness = 7;
    }
    
    /* Guardar brillo actual */
    current_brightness = brightness;
    
    /* Comando de brillo: 0x88 + nivel (0x88 a 0x8F) */
    brightness_cmd = 0x88 + brightness;
    
    /* Enviar comando de brillo */
    PORT_SALIDA = TM_STB_LOW();
    tm1638_send_cmd(brightness_cmd);
    PORT_SALIDA = TM_STB_HIGH();
    tm1638_delay(timing_delay);
}

/* ============================================================================
 * FUNCIONES DE TECLADO
 * ============================================================================ */

/**
 * @brief Lee los datos crudos de las teclas del TM1638
 * @return Valor de 32 bits con el estado de las teclas
 */
uint32_t tm1638_read_keys(void) {
    uint32_t keys = 0;
    uint8_t i;
    uint8_t byte_val;
    
    /* Iniciar comunicación */
    PORT_SALIDA = TM_CLK_HIGH();
    PORT_SALIDA = TM_STB_HIGH();
    tm1638_delay(timing_delay);
    
    /* Comando de lectura de teclas */
    PORT_SALIDA = TM_STB_LOW();
    tm1638_delay(timing_delay);
    tm1638_send_cmd(0x42); /* Comando de lectura de teclas */
    
    /* Leer 4 bytes (32 bits total) */
    for (i = 0; i < 4; i++) {
        byte_val = tm1638_read_byte();
        keys |= ((uint32_t)byte_val << (i * 8));
    }
    
    PORT_SALIDA = TM_STB_HIGH();
    tm1638_delay(timing_delay);
    
    return keys;
}

/**
 * @brief Obtiene el número de la primera tecla presionada (QYF-TM1638)
 * @return Número de tecla (1-16) o 0 si no hay teclas presionadas
 */
uint8_t tm1638_get_key_pressed(void) {
    uint32_t keys = tm1638_read_keys();
    
    if (keys == 0) {
        return 0; /* No hay teclas presionadas */
    }
    
    /* Mapeo directo basado en valores reales del QYF-TM1638 */
    if (keys & 0x00000002) return 9;   /* Bit 1 - Tecla 9 */
    if (keys & 0x00000004) return 1;   /* Bit 2 - Tecla 1 */
    if (keys & 0x00000020) return 10;  /* Bit 5 - Tecla 10 */
    if (keys & 0x00000040) return 2;   /* Bit 6 - Tecla 2 */
    if (keys & 0x00000200) return 11;  /* Bit 9 - Tecla 11 */
    if (keys & 0x00000400) return 3;   /* Bit 10 - Tecla 3 */
    if (keys & 0x00002000) return 12;  /* Bit 13 - Tecla 12 */
    if (keys & 0x00004000) return 4;   /* Bit 14 - Tecla 4 */
    if (keys & 0x00020000) return 13;  /* Bit 17 - Tecla 13 */
    if (keys & 0x00040000) return 5;   /* Bit 18 - Tecla 5 */
    if (keys & 0x00200000) return 14;  /* Bit 21 - Tecla 14 */
    if (keys & 0x00400000) return 6;   /* Bit 22 - Tecla 6 */
    if (keys & 0x02000000) return 15;  /* Bit 25 - Tecla 15 */
    if (keys & 0x04000000) return 7;   /* Bit 26 - Tecla 7 */
    if (keys & 0x20000000) return 16;  /* Bit 29 - Tecla 16 */
    if (keys & 0x40000000) return 8;   /* Bit 30 - Tecla 8 */
    
    return 0; /* No debería llegar aquí */
}

/**
 * @brief Obtiene todas las teclas presionadas simultáneamente
 * @param pressed_keys Array de 16 elementos que se llena con 1 si la tecla está presionada
 * @return Número total de teclas presionadas
 */
uint8_t tm1638_get_all_keys_pressed(uint8_t* pressed_keys) {
    uint32_t keys = tm1638_read_keys();
    uint8_t i;
    uint8_t count = 0;
    
    /* Inicializar array */
    for (i = 0; i < 16; i++) {
        pressed_keys[i] = 0;
    }
    
    /* Mapeo directo basado en valores reales del QYF-TM1638 */
    if (keys & 0x00000002) { pressed_keys[8] = 1; count++; }   /* Bit 1 - Tecla 9 */
    if (keys & 0x00000004) { pressed_keys[0] = 1; count++; }   /* Bit 2 - Tecla 1 */
    if (keys & 0x00000020) { pressed_keys[9] = 1; count++; }   /* Bit 5 - Tecla 10 */
    if (keys & 0x00000040) { pressed_keys[1] = 1; count++; }   /* Bit 6 - Tecla 2 */
    if (keys & 0x00000200) { pressed_keys[10] = 1; count++; }  /* Bit 9 - Tecla 11 */
    if (keys & 0x00000400) { pressed_keys[2] = 1; count++; }   /* Bit 10 - Tecla 3 */
    if (keys & 0x00002000) { pressed_keys[11] = 1; count++; }  /* Bit 13 - Tecla 12 */
    if (keys & 0x00004000) { pressed_keys[3] = 1; count++; }   /* Bit 14 - Tecla 4 */
    if (keys & 0x00020000) { pressed_keys[12] = 1; count++; }  /* Bit 17 - Tecla 13 */
    if (keys & 0x00040000) { pressed_keys[4] = 1; count++; }   /* Bit 18 - Tecla 5 */
    if (keys & 0x00200000) { pressed_keys[13] = 1; count++; }  /* Bit 21 - Tecla 14 */
    if (keys & 0x00400000) { pressed_keys[5] = 1; count++; }   /* Bit 22 - Tecla 6 */
    if (keys & 0x02000000) { pressed_keys[14] = 1; count++; }  /* Bit 25 - Tecla 15 */
    if (keys & 0x04000000) { pressed_keys[6] = 1; count++; }   /* Bit 26 - Tecla 7 */
    if (keys & 0x20000000) { pressed_keys[15] = 1; count++; }  /* Bit 29 - Tecla 16 */
    if (keys & 0x40000000) { pressed_keys[7] = 1; count++; }   /* Bit 30 - Tecla 8 */
    
    return count;
}