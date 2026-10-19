#include <FreeRTOS.h>
#include <queue.h>
#include <semphr.h>
#include <stdio.h>
#include <task.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "pins.h"
#include "ssd1306.h"

/*
 * Troque estes GPIOs se você conectou o sensor em outros pinos.
 * GPIO 16 e 17 não são usados pelo OLED, pelos botões ou pelo LED RGB
 * da PicoDock descritos no guia.
 */
#define ULTRASONIC_TRIG_PIN 16
#define ULTRASONIC_ECHO_PIN 17

#define OLED_WIDTH 128
#define OLED_HEIGHT 32

#define TRIGGER_PERIOD_MS 100
#define ECHO_TIMEOUT_MS 40
#define DISPLAY_TIMEOUT_MS 60

#define MAX_DISTANCE_CM 400

/*
 * A fila leva o timestamp e a borda detectada pelo callback.
 * O timestamp é obtido com to_us_since_boot().
 */
typedef struct {
    uint64_t time_us;
    bool rising_edge;
} echo_event_t;

/* Variáveis globais: somente recursos do FreeRTOS. */
static QueueHandle_t xQueueTime = NULL;
static QueueHandle_t xQueueDistance = NULL;
static SemaphoreHandle_t xSemaphoreTrigger = NULL;

/* Callback da interrupção do pino ECHO. */
void pin_callback(uint gpio, uint32_t events) {
    if (gpio != ULTRASONIC_ECHO_PIN) {
        return;
    }

    BaseType_t higher_priority_task_woken = pdFALSE;
    echo_event_t event;

    event.time_us = to_us_since_boot(get_absolute_time());

    if (events & GPIO_IRQ_EDGE_RISE) {
        event.rising_edge = true;
    } else if (events & GPIO_IRQ_EDGE_FALL) {
        event.rising_edge = false;
    } else {
        return;
    }

    xQueueSendFromISR(
        xQueueTime,
        &event,
        &higher_priority_task_woken
    );

    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/* Inicializa o OLED, usando a ligação do seu exemplo nos GPIOs 2 e 3. */
static void oled_display_init(ssd1306_t *display) {
    i2c_init(i2c1, 400000);

    gpio_set_function(2, GPIO_FUNC_I2C);
    gpio_set_function(3, GPIO_FUNC_I2C);
    gpio_pull_up(2);
    gpio_pull_up(3);

    display->external_vcc = false;
    ssd1306_init(display, OLED_WIDTH, OLED_HEIGHT, 0x3C, i2c1);
    ssd1306_clear(display);
    ssd1306_show(display);

    gpio_init(SSD1306_PIN_LITE);
    gpio_set_dir(SSD1306_PIN_LITE, GPIO_OUT);
    gpio_put(SSD1306_PIN_LITE, 0);
}

/* Inicializa os canais do LED RGB. Neste exemplo, nível alto significa apagado. */
static void led_rgb_init(void) {
    gpio_init(LED_PIN_R);
    gpio_set_dir(LED_PIN_R, GPIO_OUT);
    gpio_put(LED_PIN_R, 1);

    gpio_init(LED_PIN_G);
    gpio_set_dir(LED_PIN_G, GPIO_OUT);
    gpio_put(LED_PIN_G, 1);

    gpio_init(LED_PIN_B);
    gpio_set_dir(LED_PIN_B, GPIO_OUT);
    gpio_put(LED_PIN_B, 1);
}

static void led_set_red(void) {
    gpio_put(LED_PIN_R, 0);
    gpio_put(LED_PIN_G, 1);
    gpio_put(LED_PIN_B, 1);
}

static void led_set_green(void) {
    gpio_put(LED_PIN_R, 1);
    gpio_put(LED_PIN_G, 0);
    gpio_put(LED_PIN_B, 1);
}

static void led_set_yellow(void) {
    gpio_put(LED_PIN_R, 0);
    gpio_put(LED_PIN_G, 0);
    gpio_put(LED_PIN_B, 1);
}

/* Prepara os pinos do HC-SR04 e habilita interrupções no ECHO. */
static void ultrasonic_init(void) {
    gpio_init(ULTRASONIC_TRIG_PIN);
    gpio_set_dir(ULTRASONIC_TRIG_PIN, GPIO_OUT);
    gpio_put(ULTRASONIC_TRIG_PIN, 0);

    gpio_init(ULTRASONIC_ECHO_PIN);
    gpio_set_dir(ULTRASONIC_ECHO_PIN, GPIO_IN);
    /* Não habilite pull-up no ECHO do HC-SR04. */

    gpio_set_irq_enabled_with_callback(
        ULTRASONIC_ECHO_PIN,
        GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
        true,
        &pin_callback
    );
}

/* Gera um pulso no TRIG e avisa o OLED que uma leitura foi iniciada. */
static void trigger_task(void *parameters) {
    (void)parameters;

    while (true) {
        gpio_put(ULTRASONIC_TRIG_PIN, 1);

        /*
         * O material do laboratório libera vTaskDelay para gerar
         * o pulso do trigger; 1 tick é tipicamente 1 ms.
         */
        vTaskDelay(pdMS_TO_TICKS(1));

        gpio_put(ULTRASONIC_TRIG_PIN, 0);

        xSemaphoreGive(xSemaphoreTrigger);

        vTaskDelay(pdMS_TO_TICKS(TRIGGER_PERIOD_MS));
    }
}

/* Recebe as bordas do ECHO, calcula a distância e envia o valor em cm. */
static void echo_task(void *parameters) {
    (void)parameters;

    echo_event_t event;
    uint64_t start_time_us = 0;

    while (true) {
        /*
         * Aguarda uma borda de subida. Bordas de descida que sobrarem
         * de uma leitura anterior são ignoradas.
         */
        if (xQueueReceive(xQueueTime, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!event.rising_edge) {
            continue;
        }

        start_time_us = event.time_us;

        /*
         * Depois da subida, aguarda a descida do ECHO.
         * Uma leitura inválida não envia distância; o oled_task detecta
         * isso pelo timeout de xQueueDistance.
         */
        while (true) {
            if (xQueueReceive(
                    xQueueTime,
                    &event,
                    pdMS_TO_TICKS(ECHO_TIMEOUT_MS)
                ) != pdTRUE) {
                break;
            }

            if (event.rising_edge) {
                /* Se aparecer outra subida, ela passa a ser o início. */
                start_time_us = event.time_us;
                continue;
            }

            if (event.time_us > start_time_us) {
                uint64_t duration_us = event.time_us - start_time_us;
                uint32_t distance_cm = (uint32_t)(duration_us / 58);

                if (distance_cm <= MAX_DISTANCE_CM) {
                    /*
                     * A fila tem comprimento 1: cada nova leitura
                     * substitui qualquer valor antigo ainda não lido.
                     */
                    xQueueOverwrite(xQueueDistance, &distance_cm);
                }
            }

            break;
        }
    }
}

/* Mostra distância ou falha no OLED e atualiza o LED RGB. */
static void oled_task(void *parameters) {
    (void)parameters;

    ssd1306_t display;
    char distance_text[20];

    oled_display_init(&display);
    led_rgb_init();

    while (true) {
        uint32_t distance_cm = 0;

        /* Espera o sinal de que o trigger iniciou uma nova leitura. */
        if (xSemaphoreTake(xSemaphoreTrigger, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        ssd1306_clear(&display);

        /*
         * Se a distância não chegar no prazo, considera falha no sensor.
         */
        if (xQueueReceive(
                xQueueDistance,
                &distance_cm,
                pdMS_TO_TICKS(DISPLAY_TIMEOUT_MS)
            ) == pdTRUE) {
            snprintf(
                distance_text,
                sizeof(distance_text),
                "%lu cm",
                (unsigned long)distance_cm
            );

            ssd1306_draw_string(&display, 0, 0, 1, "DISTANCIA:");
            ssd1306_draw_string(&display, 0, 10, 1, distance_text);

            /* Barra de 0 a 400 cm; a largura máxima é 118 pixels. */
            uint32_t bar_width =
                (distance_cm * 116) / MAX_DISTANCE_CM;

            ssd1306_draw_empty_square(&display, 5, 22, 118, 8);

            if (bar_width > 0) {
                ssd1306_draw_square(
                    &display,
                    6,
                    23,
                    bar_width,
                    6
                );
            }

            if (distance_cm <= 100) {
                led_set_green();
            } else {
                led_set_yellow();
            }
        } else {
            ssd1306_draw_string(&display, 0, 0, 1, "FALHA NO SENSOR");
            ssd1306_draw_string(&display, 0, 12, 1, "Verifique o ECHO");

            led_set_red();
        }

        ssd1306_show(&display);
    }
}

int main(void) {
    stdio_init_all();

    xQueueTime = xQueueCreate(8, sizeof(echo_event_t));
    xQueueDistance = xQueueCreate(1, sizeof(uint32_t));
    xSemaphoreTrigger = xSemaphoreCreateBinary();

    if (xQueueTime == NULL ||
        xQueueDistance == NULL ||
        xSemaphoreTrigger == NULL) {
        while (true) {
            tight_loop_contents();
        }
    }

    ultrasonic_init();

    if (xTaskCreate(trigger_task, "trigger_task", 512, NULL, 2, NULL) != pdPASS ||
        xTaskCreate(echo_task, "echo_task", 768, NULL, 2, NULL) != pdPASS ||
        xTaskCreate(oled_task, "oled_task", 1024, NULL, 1, NULL) != pdPASS) {
        while (true) {
            tight_loop_contents();
        }
    }

    vTaskStartScheduler();

    while (true) {
        tight_loop_contents();
    }
}