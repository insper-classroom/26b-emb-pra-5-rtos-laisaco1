/*
 * LED blink with FreeRTOS
 */
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

// === Definições para SSD1306 ===
ssd1306_t disp;

#define BTN_QUEUE_LEN 8

static QueueHandle_t xBtnQueue = NULL;

// === Função de callback (IRQ) ===
void btn_callback(uint gpio, uint32_t events) {
    if (events & GPIO_IRQ_EDGE_RISE) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        // Envia o GPIO do botão que gerou a interrupção
        xQueueSendFromISR(xBtnQueue, &gpio, &xHigherPriorityTaskWoken);
    }
}

void oled_display_init(void) {
    i2c_init(i2c1, 400000);
    gpio_set_function(2, GPIO_FUNC_I2C);
    gpio_set_function(3, GPIO_FUNC_I2C);
    gpio_pull_up(2);
    gpio_pull_up(3);

    disp.external_vcc = false;
    ssd1306_init(&disp, 128, 64, 0x3C, i2c1);
    ssd1306_clear(&disp);
    ssd1306_show(&disp);

    gpio_init(SSD1306_PIN_LITE);
    gpio_set_dir(SSD1306_PIN_LITE, GPIO_OUT);
    gpio_put(SSD1306_PIN_LITE, 0);
}

void btns_init(void) {
    gpio_init(BTN_PIN_R);
    gpio_set_dir(BTN_PIN_R, GPIO_IN);
    gpio_pull_up(BTN_PIN_R);
    gpio_set_irq_enabled_with_callback(BTN_PIN_R,
                                       GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
                                       true, &btn_callback);

    gpio_init(BTN_PIN_G);
    gpio_set_dir(BTN_PIN_G, GPIO_IN);
    gpio_pull_up(BTN_PIN_G);
    gpio_set_irq_enabled(BTN_PIN_G, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
                         true);

    gpio_init(BTN_PIN_B);
    gpio_set_dir(BTN_PIN_B, GPIO_IN);
    gpio_pull_up(BTN_PIN_B);
    gpio_set_irq_enabled(BTN_PIN_B, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
                         true);
}

void led_rgb_init(void) {
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

static void handle_button(uint gpio) {
    if (gpio == BTN_PIN_R) {
        gpio_put(LED_PIN_R, 0);

        ssd1306_clear(&disp);
        ssd1306_draw_string(&disp, 8, 24, 2, "RED");
        ssd1306_show(&disp);

        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_put(LED_PIN_R, 1);
    }
    else if (gpio == BTN_PIN_G) {
        gpio_put(LED_PIN_G, 0);

        ssd1306_clear(&disp);
        ssd1306_draw_string(&disp, 8, 24, 2, "GREEN");
        ssd1306_show(&disp);

        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_put(LED_PIN_G, 1);
    }
    else if (gpio == BTN_PIN_B) {
        gpio_put(LED_PIN_B, 0);

        ssd1306_clear(&disp);
        ssd1306_draw_string(&disp, 8, 24, 2, "BLUE");
        ssd1306_show(&disp);

        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_put(LED_PIN_B, 1);
    }
}

void task_1(void *p) {
    oled_display_init();
    btns_init();
    led_rgb_init();

    uint gpio_event;

    while (1) {
        if (xQueueReceive(xBtnQueue, &gpio_event, pdMS_TO_TICKS(50)) == pdTRUE) {
            handle_button(gpio_event);
        }
    }
}

int main() {
    stdio_init_all();

    xBtnQueue = xQueueCreate(BTN_QUEUE_LEN, sizeof(uint));
    if (xBtnQueue == NULL) {
        while (1) {
            // falha ao criar fila
        }
    }

    xTaskCreate(task_1, "Task 1", 4095, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true);
}
