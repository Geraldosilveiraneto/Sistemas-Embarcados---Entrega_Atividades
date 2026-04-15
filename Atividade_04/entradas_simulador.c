/*
 * Contador Binário de 4 bits com ESP-IDF (VERSÃO CORRIGIDA)
 * =========================================================
 * LED0 (GPIO4) = bit 0 (LSB)
 * LED1 (GPIO5) = bit 1
 * LED2 (GPIO6) = bit 2
 * LED3 (GPIO7) = bit 3 (MSB)
 *
 * Botão A (GPIO8): incrementa o contador pelo passo atual
 * Botão B (GPIO9): alterna o passo entre +1 e +2
 *
 * Debounce: por software usando esp_timer (sem delay)
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

/* ── Pinagem ─────────────────────────────────────────────────── */
#define LED0_PIN    GPIO_NUM_4
#define LED1_PIN    GPIO_NUM_5
#define LED2_PIN    GPIO_NUM_6
#define LED3_PIN    GPIO_NUM_7
#define BTN_A_PIN   GPIO_NUM_8
#define BTN_B_PIN   GPIO_NUM_9

/* ── Debounce ────────────────────────────────────────────────── */
#define DEBOUNCE_US 50000ULL  // 50 ms

static const char *TAG = "COUNTER";

/* ── Estado ─────────────────────────────────────────────────── */
static uint8_t g_counter = 0;
static uint8_t g_step    = 1;

/* Controle debounce */
static int64_t g_last_a = 0;
static int64_t g_last_b = 0;

/* Fila de eventos */
static QueueHandle_t gpio_evt_queue = NULL;

/* ── Atualiza LEDs ───────────────────────────────────────────── */
static void update_leds(void)
{
    gpio_set_level(LED0_PIN, (g_counter >> 0) & 1);
    gpio_set_level(LED1_PIN, (g_counter >> 1) & 1);
    gpio_set_level(LED2_PIN, (g_counter >> 2) & 1);
    gpio_set_level(LED3_PIN, (g_counter >> 3) & 1);
}

/* ── ISR dos botões ──────────────────────────────────────────── */
static void IRAM_ATTR btn_isr_handler(void *arg)
{
    gpio_num_t pin = (gpio_num_t)(uintptr_t)arg;
    int64_t now = esp_timer_get_time();

    if (pin == BTN_A_PIN) {
        if ((now - g_last_a) > DEBOUNCE_US) {
            g_last_a = now;
            xQueueSendFromISR(gpio_evt_queue, &pin, NULL);
        }
    }
    else if (pin == BTN_B_PIN) {
        if ((now - g_last_b) > DEBOUNCE_US) {
            g_last_b = now;
            xQueueSendFromISR(gpio_evt_queue, &pin, NULL);
        }
    }
}

/* ── Task que processa os botões ─────────────────────────────── */
static void gpio_task(void *arg)
{
    gpio_num_t pin;

    while (1) {
        if (xQueueReceive(gpio_evt_queue, &pin, portMAX_DELAY)) {

            if (pin == BTN_A_PIN) {
                g_counter = (g_counter + g_step) & 0x0F;
                update_leds();

                ESP_LOGI(TAG, "A | step=%d | counter=0x%X",
                         g_step, g_counter);
            }

            else if (pin == BTN_B_PIN) {
                g_step = (g_step == 1) ? 2 : 1;

                ESP_LOGI(TAG, "B | novo step=%d", g_step);
            }
        }
    }
}

/* ── Configuração dos GPIOs ──────────────────────────────────── */
static void gpio_init(void)
{
    /* LEDs como saída */
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << LED0_PIN) |
                        (1ULL << LED1_PIN) |
                        (1ULL << LED2_PIN) |
                        (1ULL << LED3_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led_cfg);

    /* Botões como entrada */
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BTN_A_PIN) |
                        (1ULL << BTN_B_PIN),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_NEGEDGE  // borda de descida
    };
    gpio_config(&btn_cfg);

    /* Cria fila */
    gpio_evt_queue = xQueueCreate(10, sizeof(gpio_num_t));

    /* Instala ISR */
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BTN_A_PIN, btn_isr_handler, (void*) BTN_A_PIN);
    gpio_isr_handler_add(BTN_B_PIN, btn_isr_handler, (void*) BTN_B_PIN);

    /* Cria task */
    xTaskCreate(gpio_task, "gpio_task", 2048, NULL, 10, NULL);
}

/* ── Main ───────────────────────────────────────────────────── */
void app_main(void)
{
    ESP_LOGI(TAG, "Contador 4 bits iniciado");

    gpio_init();
    update_leds();  // começa em 0x0
}
