#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"

#define LED_PIN    GPIO_NUM_4
#define BUTTON_PIN GPIO_NUM_0 

TaskHandle_t button_task_handle = NULL;
TimerHandle_t auto_off_timer    = NULL;
bool led_state = false;

void auto_off_callback(TimerHandle_t xTimer) {
    led_state = false;
    gpio_set_level(LED_PIN, 0);
    printf("Temporizador: LED desligado apos 10s de inatividade.\n");
}

void IRAM_ATTR button_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(button_task_handle, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void button_task(void* arg) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(50));

        if (gpio_get_level(BUTTON_PIN) == 0) {
            int  hold_time  = 50;
            bool forced_off = false;

            led_state = true;
            gpio_set_level(LED_PIN, 1);
            xTimerStart(auto_off_timer, 0);
            printf("Botao pressionado: LED ligado. Temporizador de 10s iniciado.\n");

            while (gpio_get_level(BUTTON_PIN) == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
                hold_time += 10;

                if (hold_time >= 2000) {
                    forced_off = true;
                    led_state  = false;
                    gpio_set_level(LED_PIN, 0);
                    xTimerStop(auto_off_timer, 0);
                    printf("Desligamento forcado: Botao mantido por 2s. LED apagado.\n");

                    while (gpio_get_level(BUTTON_PIN) == 0) {
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }
                    break;
                }
            }

            if (!forced_off) {
                printf("Clique normal detectado. LED ficara aceso por 10s.\n");
            }
        }

        ulTaskNotifyTake(pdTRUE, 0);
    }
}

void app_main(void) {

    // Configuração do LED
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_cfg);

    // ✅ LED já inicia LIGADO para confirmar que o hardware funciona
    led_state = true;
    gpio_set_level(LED_PIN, 1);

    // Configuração do Botão com Pull-up Interno
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&btn_cfg);

    auto_off_timer = xTimerCreate(
        "AutoOffTimer",
        pdMS_TO_TICKS(10000),
        pdFALSE,
        (void*)0,
        auto_off_callback
    );

    xTaskCreate(button_task, "Button Task", 2048, NULL, 10, &button_task_handle);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, button_isr_handler, NULL);

    printf("Sistema Iniciado. LED ja esta LIGADO.\n");
}
