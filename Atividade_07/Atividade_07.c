#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

/* ── Pinagem ─────────────────────────────────────────── */
#define PIN_LED     GPIO_NUM_4
#define PIN_BTN     GPIO_NUM_15

/* ── Constantes de tempo ─────────────────────────────── */
#define MS_DEBOUNCE   50u
#define MS_AUTO_OFF   10000u

/* ── Utilitário: tempo atual em ms ───────────────────── */
static inline uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/* ── Inicialização dos periféricos ───────────────────── */
static void peripherals_init(void)
{
    // LED
    gpio_reset_pin(PIN_LED);
    gpio_set_direction(PIN_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_LED, 0);

    // Botão com pull-up interno
    gpio_reset_pin(PIN_BTN);
    gpio_set_direction(PIN_BTN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PIN_BTN, GPIO_PULLUP_ONLY);
}

/* ── Aplicação principal ─────────────────────────────── */
void app_main(void)
{
    peripherals_init();

    bool     led_on          = false;
    int      btn_stable      = 1;   // nível estabilizado após debounce
    int      btn_prev        = 1;   // leitura do ciclo anterior
    uint32_t t_debounce      = 0;   // timestamp da última mudança de leitura
    uint32_t t_led_on        = 0;   // timestamp em que o LED foi ligado

    for (;;) {
        const uint32_t t = now_ms();
        const int      raw = gpio_get_level(PIN_BTN);

        /* ── Debounce ──────────────────────────────────── */
        if (raw != btn_prev)
            t_debounce = t;

        if ((t - t_debounce) > MS_DEBOUNCE && raw != btn_stable) {
            btn_stable = raw;

            // Nível baixo = botão pressionado (pull-up)
            if (btn_stable == 0) {
                led_on = !led_on;
                gpio_set_level(PIN_LED, (int)led_on);

                if (led_on) {
                    t_led_on = t;
                    printf("[LED] Ligado — auto-off em %u s.\n",
                           MS_AUTO_OFF / 1000u);
                } else {
                    printf("[LED] Desligado pelo botão.\n");
                }
            }
        }

        btn_prev = raw;

        /* ── Auto-desligamento ─────────────────────────── */
        if (led_on && (t - t_led_on) >= MS_AUTO_OFF) {
            led_on = false;
            gpio_set_level(PIN_LED, 0);
            printf("[LED] Auto-off atingido.\n");
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
