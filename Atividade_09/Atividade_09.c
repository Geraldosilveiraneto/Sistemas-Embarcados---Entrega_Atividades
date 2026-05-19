#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

/* ─── PINOS ─────────────────────────────────────────────── */
#define LED_PIN      GPIO_NUM_38

/* BOTÃO INTERNO BOOT DA PLACA ESP32-S3 */
#define BUTTON_PIN   GPIO_NUM_0

/* POTENCIÔMETRO NO GPIO4 */
#define ADC_UNIT_USED ADC_UNIT_1
#define ADC_CHANNEL   ADC_CHANNEL_3   // GPIO4 no ESP32-S3

/* MPU6050 */
#define I2C_SDA_PIN  GPIO_NUM_2
#define I2C_SCL_PIN  GPIO_NUM_42

/* ─── LEDC PWM ──────────────────────────────────────────── */
#define LEDC_TIMER_N    LEDC_TIMER_0
#define LEDC_MODE_N     LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL_N  LEDC_CHANNEL_0
#define LEDC_DUTY_RES   LEDC_TIMER_13_BIT
#define LEDC_FREQ_HZ    5000
#define DUTY_MAX        8191

/* ─── ADC ───────────────────────────────────────────────── */
#define ADC_MAX_RAW  4095
#define VREF_MV      3300

/* ─── I2C / MPU6050 ─────────────────────────────────────── */
#define I2C_PORT_NUM   I2C_NUM_0
#define I2C_FREQ_HZ    100000
#define MPU6050_ADDR   0x68
#define MPU_REG_PWR    0x6B
#define MPU_REG_ACCEL  0x3B
#define ACCEL_SENS     16384.0f

/* ─── STRUCT IMU ────────────────────────────────────────── */
typedef struct {
    float x, y, z;
} imu_data_t;

/* ─── ESTADO GLOBAL ─────────────────────────────────────── */
static volatile bool hold_mode = false;
static volatile int  last_raw  = 0;
static volatile int  last_duty = 0;

static imu_data_t imu_data = {0.0f, 0.0f, 0.0f};

/* ─── HANDLES FREERTOS ──────────────────────────────────── */
static QueueHandle_t pot_queue = NULL;
static SemaphoreHandle_t button_sem = NULL;
static SemaphoreHandle_t imu_mutex = NULL;
static adc_oneshot_unit_handle_t adc_handle = NULL;

/* ══════════════════════════════════════════════════════════
   TASK: POTENCIÔMETRO
   ══════════════════════════════════════════════════════════ */
void pot_task(void *arg)
{
    int adc_raw = 0;
    int duty = 0;

    while (1) {
        esp_err_t ret = adc_oneshot_read(adc_handle, ADC_CHANNEL, &adc_raw);

        if (ret == ESP_OK) {
            if (adc_raw < 0) {
                adc_raw = 0;
            }

            if (adc_raw > ADC_MAX_RAW) {
                adc_raw = ADC_MAX_RAW;
            }

            duty = (adc_raw * DUTY_MAX) / ADC_MAX_RAW;

            last_raw = adc_raw;
            last_duty = duty;

            xQueueOverwrite(pot_queue, &duty);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ══════════════════════════════════════════════════════════
   TASK: LED
   ══════════════════════════════════════════════════════════ */
void led_task(void *arg)
{
    int duty = 0;

    while (1) {
        if (xSemaphoreTake(button_sem, 0) == pdTRUE) {
            hold_mode = !hold_mode;

            printf("[LED] Modo %s\n",
                   hold_mode ? "HOLD ativado." : "LIVE restaurado.");
        }

        if (!hold_mode) {
            if (xQueueReceive(pot_queue, &duty, pdMS_TO_TICKS(50)) == pdTRUE) {
                ledc_set_duty(LEDC_MODE_N, LEDC_CHANNEL_N, duty);
                ledc_update_duty(LEDC_MODE_N, LEDC_CHANNEL_N);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ══════════════════════════════════════════════════════════
   TASK: BOTÃO INTERNO BOOT
   Usa GPIO0
   Aperta BOOT uma vez: trava LED
   Aperta BOOT novamente: libera LED
   ══════════════════════════════════════════════════════════ */
void button_task(void *arg)
{
    int last_state = 1;
    int current_state = 1;

    while (1) {
        current_state = gpio_get_level(BUTTON_PIN);

        if (last_state == 1 && current_state == 0) {
            xSemaphoreGive(button_sem);

            printf("[BUTTON] Botao BOOT pressionado\n");

            vTaskDelay(pdMS_TO_TICKS(300));
        }

        last_state = current_state;

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ══════════════════════════════════════════════════════════
   HELPERS I2C / MPU6050
   ══════════════════════════════════════════════════════════ */
static esp_err_t mpu_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};

    return i2c_master_write_to_device(
        I2C_PORT_NUM,
        MPU6050_ADDR,
        buf,
        2,
        pdMS_TO_TICKS(10)
    );
}

static esp_err_t mpu_read(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(
        I2C_PORT_NUM,
        MPU6050_ADDR,
        &reg,
        1,
        data,
        len,
        pdMS_TO_TICKS(10)
    );
}

/* ══════════════════════════════════════════════════════════
   TASK: IMU MPU6050
   ══════════════════════════════════════════════════════════ */
void imu_task(void *arg)
{
    uint8_t buf[6];
    int16_t rx, ry, rz;

    while (1) {
        if (mpu_read(MPU_REG_ACCEL, buf, 6) == ESP_OK) {
            rx = (int16_t)((buf[0] << 8) | buf[1]);
            ry = (int16_t)((buf[2] << 8) | buf[3]);
            rz = (int16_t)((buf[4] << 8) | buf[5]);

            if (xSemaphoreTake(imu_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                imu_data.x = rx / ACCEL_SENS;
                imu_data.y = ry / ACCEL_SENS;
                imu_data.z = rz / ACCEL_SENS;

                xSemaphoreGive(imu_mutex);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ══════════════════════════════════════════════════════════
   TASK: CONSOLE
   ══════════════════════════════════════════════════════════ */
void console_task(void *arg)
{
    imu_data_t local = {0.0f, 0.0f, 0.0f};

    while (1) {
        int raw = last_raw;
        int duty = last_duty;
        int pct = (duty * 100) / DUTY_MAX;

        uint32_t vmv = ((uint32_t)raw * VREF_MV) / ADC_MAX_RAW;

        if (xSemaphoreTake(imu_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            local = imu_data;
            xSemaphoreGive(imu_mutex);
        }

        printf("=====================================================\n");
        printf("STATUS: [%s] | POT: %4d (%4lu mV) | LED: %3d%%\n",
               hold_mode ? "HOLD" : "LIVE",
               raw,
               vmv,
               pct);

        printf("IMU ACCEL (g): X: %6.2f | Y: %6.2f | Z: %6.2f\n",
               local.x,
               local.y,
               local.z);

        printf("=====================================================\n\n");

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ══════════════════════════════════════════════════════════
   APP MAIN
   ══════════════════════════════════════════════════════════ */
void app_main(void)
{
    /* ── LED PWM ── */
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE_N,
        .timer_num = LEDC_TIMER_N,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_ch = {
        .speed_mode = LEDC_MODE_N,
        .channel = LEDC_CHANNEL_N,
        .timer_sel = LEDC_TIMER_N,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = LED_PIN,
        .duty = 0,
        .hpoint = 0,
    };

    ledc_channel_config(&ledc_ch);

    /* ── ADC POTENCIÔMETRO GPIO4 ── */
    adc_oneshot_unit_init_cfg_t adc_unit_cfg = {
        .unit_id = ADC_UNIT_USED,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    adc_oneshot_new_unit(&adc_unit_cfg, &adc_handle);

    adc_oneshot_chan_cfg_t adc_ch_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };

    adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &adc_ch_cfg);

    /* ── I2C MPU6050 ── */
    i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };

    i2c_param_config(I2C_PORT_NUM, &i2c_cfg);
    i2c_driver_install(I2C_PORT_NUM, I2C_MODE_MASTER, 0, 0, 0);

    mpu_write(MPU_REG_PWR, 0x00);

    /* ── BOTÃO INTERNO BOOT GPIO0 COM PULL-UP INTERNO ── */
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&btn_cfg);

    /* ── FILA, SEMÁFORO E MUTEX ── */
    pot_queue = xQueueCreate(1, sizeof(int));
    button_sem = xSemaphoreCreateBinary();
    imu_mutex = xSemaphoreCreateMutex();

    /* ── 5 TAREFAS ── */
    xTaskCreate(button_task,  "Button",  2048, NULL, 10, NULL);
    xTaskCreate(imu_task,     "IMU",     4096, NULL,  7, NULL);
    xTaskCreate(pot_task,     "POT",     2048, NULL,  5, NULL);
    xTaskCreate(led_task,     "LED",     2048, NULL,  5, NULL);
    xTaskCreate(console_task, "Console", 4096, NULL,  3, NULL);

    printf("Sistema iniciado. 5 tarefas ativas.\n");
    printf("LED no GPIO38.\n");
    printf("Botao interno BOOT no GPIO0.\n");
    printf("Potenciometro no GPIO4: 3.3V / GPIO4 / GND.\n");
    printf("MPU6050: SDA GPIO2 | SCL GPIO42.\n");
    printf("Aperte BOOT uma vez para HOLD e outra vez para LIVE.\n");
}
