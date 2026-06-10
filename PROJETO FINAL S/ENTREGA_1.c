#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

/* =========================================================
   PINAGEM
   ========================================================= */

#define JOY_X_CHANNEL   ADC_CHANNEL_3    // GPIO4
#define JOY_Y_CHANNEL   ADC_CHANNEL_4    // GPIO5
#define ADC_UNIT_USED   ADC_UNIT_1

#define SERVO1_PIN      GPIO_NUM_42      // Servo eixo X
#define SERVO2_PIN      GPIO_NUM_41      // Servo eixo Y
#define PWM_FREQ_HZ     50

#define LED_PIN         GPIO_NUM_48

/* =========================================================
   VARIÁVEIS COMPARTILHADAS
   ========================================================= */

static SemaphoreHandle_t mutex = NULL;
static adc_oneshot_unit_handle_t adc_handle = NULL;

static int joy_x = 2048;
static int joy_y = 2048;

/* =========================================================
   FUNÇÕES AUXILIARES
   ========================================================= */

static int adc_para_angulo(int adc)
{
    if (adc < 0)    adc = 0;
    if (adc > 4095) adc = 4095;
    return (adc * 180) / 4095;
}

static uint32_t angulo_para_duty(int graus)
{
    if (graus < 0)   graus = 0;
    if (graus > 180) graus = 180;
    int pulso_us = 500 + (graus * 2000) / 180;
    return ((uint32_t)pulso_us * 16383) / 20000;
}

static void set_servo1(int graus)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, angulo_para_duty(graus));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void set_servo2(int graus)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, angulo_para_duty(graus));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

/* =========================================================
   INICIALIZAÇÕES
   ========================================================= */

void led_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(LED_PIN, 0);
    printf("✓ LED inicializado (GPIO%d)\n", LED_PIN);
}

void adc_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = ADC_UNIT_USED,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t ch_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten    = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, JOY_X_CHANNEL, &ch_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, JOY_Y_CHANNEL, &ch_cfg));
    printf("✓ ADC inicializado — GPIO4 e GPIO5\n");
}

void servo_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_14_BIT,
        .freq_hz         = PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t ch1 = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = SERVO1_PIN,
        .duty       = angulo_para_duty(90),
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch1));

    ledc_channel_config_t ch2 = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_1,
        .timer_sel  = LEDC_TIMER_0,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = SERVO2_PIN,
        .duty       = angulo_para_duty(90),
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch2));

    printf("✓ Servos inicializados — GPIO%d e GPIO%d\n", SERVO1_PIN, SERVO2_PIN);
}

/* =========================================================
   TASK 1 — LEITURA DO JOYSTICK
   ========================================================= */

void task_joystick(void *arg)
{
    int lx = 0;   // CORRIGIDO: inicializado com 0
    int ly = 0;   // CORRIGIDO: inicializado com 0

    while (1) {
        adc_oneshot_read(adc_handle, JOY_X_CHANNEL, &lx);
        adc_oneshot_read(adc_handle, JOY_Y_CHANNEL, &ly);

        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            joy_x = lx;
            joy_y = ly;
            xSemaphoreGive(mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* =========================================================
   TASK 2 — CONTROLE DOS SERVOS
   ========================================================= */

void task_servos(void *arg)
{
    int lx = 2048;   // CORRIGIDO: inicializado no centro
    int ly = 2048;   // CORRIGIDO: inicializado no centro

    while (1) {
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            lx = joy_x;
            ly = joy_y;
            xSemaphoreGive(mutex);
        }

        set_servo1(adc_para_angulo(lx));
        set_servo2(adc_para_angulo(ly));

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* =========================================================
   TASK 3 — CONSOLE / DEBUG
   ========================================================= */

void task_console(void *arg)
{
    int lx = 2048;   // CORRIGIDO: inicializado no centro
    int ly = 2048;   // CORRIGIDO: inicializado no centro

    while (1) {
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            lx = joy_x;
            ly = joy_y;
            xSemaphoreGive(mutex);
        }

        printf("JOY X: %4d -> SERVO1: %3d graus  |  "
               "JOY Y: %4d -> SERVO2: %3d graus\n",
               lx, adc_para_angulo(lx),
               ly, adc_para_angulo(ly));

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/* =========================================================
   APP MAIN
   ========================================================= */

void app_main(void)
{
    printf("\n╔══════════════════════════════════════════╗\n");
    printf("║   FASE 1 — JOYSTICK + SERVO — ESP32-S3   ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    mutex = xSemaphoreCreateMutex();

    led_init();
    adc_init();
    servo_init();

    gpio_set_level(LED_PIN, 1);
    printf("\n✓ Sistema pronto! Mova o joystick.\n\n");

    xTaskCreate(task_joystick, "Task_Joystick", 2048, NULL, 5, NULL);
    xTaskCreate(task_servos,   "Task_Servos",   2048, NULL, 5, NULL);
    xTaskCreate(task_console,  "Task_Console",  2048, NULL, 3, NULL);
}
