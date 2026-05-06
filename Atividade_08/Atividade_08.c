#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"

/* ─── Pinos ─────────────────────────────────────────────── */
#define LED_PIN     GPIO_NUM_35
#define BUTTON_PIN  GPIO_NUM_47
#define ADC_CHANNEL ADC_CHANNEL_3  // GPIO34

/* ─── LEDC ──────────────────────────────────────────────── */
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL    LEDC_CHANNEL_0
#define LEDC_DUTY_RES   LEDC_TIMER_12_BIT   // 0 – 4095 (casa com ADC)
#define LEDC_FREQUENCY  5000                // Hz

/* ─── ADC ───────────────────────────────────────────────── */
#define ADC_MAX_RAW     4095
#define VREF_MV         3300   // tensão de referência em mV

/* ─── Estado global ─────────────────────────────────────── */
static volatile bool hold_mode    = false;
static volatile int  frozen_duty  = 0;

/* ─── Handles ───────────────────────────────────────────── */
static TaskHandle_t       button_task_handle = NULL;
static adc_oneshot_unit_handle_t adc_handle  = NULL;

/* ══════════════════════════════════════════════════════════
   ISR do botão — notifica a task
   ══════════════════════════════════════════════════════════ */
void IRAM_ATTR button_isr_handler(void *arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(button_task_handle, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* ══════════════════════════════════════════════════════════
   Task do botão — alterna HOLD / LIVE
   ══════════════════════════════════════════════════════════ */
void button_task(void *arg)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   // aguarda ISR

        vTaskDelay(pdMS_TO_TICKS(50));              // debounce

        if (gpio_get_level(BUTTON_PIN) == 0) {
            hold_mode = !hold_mode;

            if (hold_mode) {
                printf("[BOTAO] Modo HOLD ativado. Brilho congelado.\n");
            } else {
                printf("[BOTAO] Modo LIVE restaurado.\n");
            }

            /* aguarda soltar o botão antes de aceitar novo acionamento */
            while (gpio_get_level(BUTTON_PIN) == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }

        ulTaskNotifyTake(pdTRUE, 0);               // limpa notificações extras
    }
}

/* ══════════════════════════════════════════════════════════
   Task de leitura ADC + controle PWM + monitoramento
   ══════════════════════════════════════════════════════════ */
void adc_pwm_task(void *arg)
{
    int  adc_raw   = 0;
    int  duty      = 0;
    uint32_t voltage_mv = 0;

    while (1) {
        if (!hold_mode) {
            /* ── leitura do ADC ── */
            adc_oneshot_read(adc_handle, ADC_CHANNEL, &adc_raw);

            /* ── escalonamento: ADC 12-bit → duty LEDC 12-bit (1:1) ── */
            duty = adc_raw;
            frozen_duty = duty;

            /* ── aplica PWM ── */
            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
        }
        /* em HOLD: mantém o último duty sem atualizar */

        /* ── tensão: V(mV) = (raw * 3300) / 4095 ── */
        voltage_mv = ((uint32_t)frozen_duty * VREF_MV) / ADC_MAX_RAW;

        printf("[%s] ADC_RAW: %4d | Tensao: %4lu mV | Duty: %4d/4095\n",
               hold_mode ? "HOLD" : "LIVE",
               frozen_duty,
               voltage_mv,
               frozen_duty);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ══════════════════════════════════════════════════════════
   app_main
   ══════════════════════════════════════════════════════════ */
void app_main(void)
{
    /* ── Configuração LEDC (PWM) ── */
    ledc_timer_config_t ledc_timer = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz         = LEDC_FREQUENCY,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHANNEL,
        .timer_sel  = LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = LED_PIN,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&ledc_channel);

    /* ── Configuração ADC ── */
    adc_oneshot_unit_init_cfg_t adc_unit_cfg = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    adc_oneshot_new_unit(&adc_unit_cfg, &adc_handle);

    adc_oneshot_chan_cfg_t adc_chan_cfg = {
        .bitwidth = ADC_BITWIDTH_12,        // resolução 12-bit → 0–4095
        .atten    = ADC_ATTEN_DB_12,        // faixa 0–3,3 V
    };
    adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &adc_chan_cfg);

    /* ── Configuração do botão ── */
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&btn_cfg);

    /* ── Tasks e ISR ── */
    xTaskCreate(button_task,  "Button Task",  2048, NULL, 10, &button_task_handle);
    xTaskCreate(adc_pwm_task, "ADC PWM Task", 4096, NULL,  5, NULL);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, button_isr_handler, NULL);

    printf("Sistema Iniciado. Modo LIVE. Gire o potenciometro.\n");
}
