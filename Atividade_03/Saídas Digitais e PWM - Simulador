//https://wokwi.com/projects/459478685805852673
/////////////////////////////////////////////////////////////
// eboço.ino

#include "driver/ledc.h"

// ─── Configurações ───────────────────────────────────────
#define DELAY_MS        200
#define FADE_STEP       255
#define LED_FREQ_HZ     1000
#define LED_DUTY_MAX    255
#define BUZZ_FREQ_MIN   500
#define BUZZ_FREQ_MAX   2000
#define BUZZ_FREQ_STEP  50
#define BUZZ_DUTY       128

// ─── Pinos (devem bater com o diagram.json) ──────────────
#define PIN_LED1   2
#define PIN_LED2   4
#define PIN_LED3   5
#define PIN_LED4   18
#define PIN_BUZZ   19

// ─── Canais LEDC ─────────────────────────────────────────
#define CH_LED1  LEDC_CHANNEL_0
#define CH_LED2  LEDC_CHANNEL_1
#define CH_LED3  LEDC_CHANNEL_2
#define CH_LED4  LEDC_CHANNEL_3
#define CH_BUZZ  LEDC_CHANNEL_4

// ─── Funções auxiliares ──────────────────────────────────
void led_set_duty(ledc_channel_t ch, uint32_t duty) {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
}

void leds_all_off() {
  led_set_duty(CH_LED1, 0);
  led_set_duty(CH_LED2, 0);
  led_set_duty(CH_LED3, 0);
  led_set_duty(CH_LED4, 0);
}

void buzzer_set_freq(uint32_t f) {
  ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_1, f);
}

void buzzer_on() {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, CH_BUZZ, BUZZ_DUTY);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, CH_BUZZ);
}

void buzzer_off() {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, CH_BUZZ, 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, CH_BUZZ);
}

// ─── Fase 1: Fading sincronizado ─────────────────────────
void fase1_fading_sincronizado() {
  // Todos acendem juntos
  for (int d = 0; d <= LED_DUTY_MAX; d += FADE_STEP) {
    led_set_duty(CH_LED1, d);
    led_set_duty(CH_LED2, d);
    led_set_duty(CH_LED3, d);
    led_set_duty(CH_LED4, d);
    delay(DELAY_MS);
  }
  // Todos apagam juntos
  for (int d = LED_DUTY_MAX; d >= 0; d -= FADE_STEP) {
    led_set_duty(CH_LED1, d);
    led_set_duty(CH_LED2, d);
    led_set_duty(CH_LED3, d);
    led_set_duty(CH_LED4, d);
    delay(DELAY_MS);
  }
  leds_all_off();
  delay(500);
}

// ─── Fase 2: Fading sequencial ───────────────────────────
void fade_single_led(ledc_channel_t ch) {
  for (int d = 0; d <= LED_DUTY_MAX; d += FADE_STEP) {
    led_set_duty(ch, d);
    delay(DELAY_MS);
  }
  for (int d = LED_DUTY_MAX; d >= 0; d -= FADE_STEP) {
    led_set_duty(ch, d);
    delay(DELAY_MS);
  }
  led_set_duty(ch, 0);
  delay(300);
}

void fase2_fading_sequencial() {
  // Ida: LED1 → LED2 → LED3 → LED4
  fade_single_led(CH_LED1);
  fade_single_led(CH_LED2);
  fade_single_led(CH_LED3);
  fade_single_led(CH_LED4);
  // Volta: LED4 → LED3 → LED2 → LED1
  fade_single_led(CH_LED4);
  fade_single_led(CH_LED3);
  fade_single_led(CH_LED2);
  fade_single_led(CH_LED1);
  leds_all_off();
  delay(500);
}

// ─── Fase 3: Teste sonoro do buzzer ──────────────────────
void fase3_teste_buzzer() {
  buzzer_on();
  // Sobe: 500 → 2000 Hz
  for (uint32_t f = BUZZ_FREQ_MIN; f <= BUZZ_FREQ_MAX; f += BUZZ_FREQ_STEP) {
    buzzer_set_freq(f);
    delay(DELAY_MS);
  }
  // Desce: 2000 → 500 Hz
  for (uint32_t f = BUZZ_FREQ_MAX; f >= BUZZ_FREQ_MIN; f -= BUZZ_FREQ_STEP) {
    buzzer_set_freq(f);
    delay(DELAY_MS);
  }
  buzzer_off();
  delay(500);
}

// ─── Setup ───────────────────────────────────────────────
void setup() {
  // Timer 0 → LEDs (1 kHz, 8 bits)
  ledc_timer_config_t lt = {
    .speed_mode      = LEDC_LOW_SPEED_MODE,
    .duty_resolution = LEDC_TIMER_8_BIT,
    .timer_num       = LEDC_TIMER_0,
    .freq_hz         = (uint32_t)LED_FREQ_HZ,
    .clk_cfg         = LEDC_AUTO_CLK,
  };
  ledc_timer_config(&lt);

  // Canais dos LEDs
  int pinos[]        = { PIN_LED1, PIN_LED2, PIN_LED3, PIN_LED4 };
  ledc_channel_t chs[] = { CH_LED1, CH_LED2, CH_LED3, CH_LED4 };
  for (int i = 0; i < 4; i++) {
    ledc_channel_config_t ch = {
      .gpio_num   = pinos[i],
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel    = chs[i],
      .intr_type  = LEDC_INTR_DISABLE,
      .timer_sel  = LEDC_TIMER_0,
      .duty       = 0,
      .hpoint     = 0,
    };
    ledc_channel_config(&ch);
  }

  // Timer 1 → Buzzer (frequência variável, 8 bits)
  ledc_timer_config_t bt = {
    .speed_mode      = LEDC_LOW_SPEED_MODE,
    .duty_resolution = LEDC_TIMER_8_BIT,
    .timer_num       = LEDC_TIMER_1,
    .freq_hz         = (uint32_t)BUZZ_FREQ_MIN,
    .clk_cfg         = LEDC_AUTO_CLK,
  };
  ledc_timer_config(&bt);

  // Canal do buzzer
  ledc_channel_config_t bc = {
    .gpio_num   = PIN_BUZZ,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel    = CH_BUZZ,
    .intr_type  = LEDC_INTR_DISABLE,
    .timer_sel  = LEDC_TIMER_1,
    .duty       = 0,
    .hpoint     = 0,
  };
  ledc_channel_config(&bc);

  // Garante tudo apagado no início
  leds_all_off();
  buzzer_off();
}

// ─── Loop principal ──────────────────────────────────────
void loop() {
  fase1_fading_sincronizado();
  fase2_fading_sequencial();
  fase3_teste_buzzer();
}

////////////////////////////////////////////////////////////////////////////////////////////
// diagrama.json

{
  "version": 1,
  "author": "Atividade 3 - PWM ESP32",
  "editor": "wokwi",
  "parts": [
    {
      "type": "board-esp32-s3-devkitc-1",
      "id": "esp",
      "top": 100,
      "left": 200,
      "attrs": {}
    },
    {
      "type": "wokwi-resistor",
      "id": "r1",
      "top": 48,
      "left": 70,
      "attrs": { "value": "220" }
    },
    {
      "type": "wokwi-led",
      "id": "led1",
      "top": 32,
      "left": 0,
      "attrs": { "color": "red" }
    },
    {
      "type": "wokwi-resistor",
      "id": "r2",
      "top": 108,
      "left": 70,
      "attrs": { "value": "220" }
    },
    {
      "type": "wokwi-led",
      "id": "led2",
      "top": 92,
      "left": 0,
      "attrs": { "color": "green" }
    },
    {
      "type": "wokwi-resistor",
      "id": "r3",
      "top": 168,
      "left": 70,
      "attrs": { "value": "220" }
    },
    {
      "type": "wokwi-led",
      "id": "led3",
      "top": 152,
      "left": 0,
      "attrs": { "color": "blue" }
    },
    {
      "type": "wokwi-resistor",
      "id": "r4",
      "top": 228,
      "left": 70,
      "attrs": { "value": "220" }
    },
    {
      "type": "wokwi-led",
      "id": "led4",
      "top": 212,
      "left": 0,
      "attrs": { "color": "yellow" }
    },
    {
      "type": "wokwi-buzzer",
      "id": "bz1",
      "top": 48,
      "left": 500,
      "attrs": {}
    }
  ],
  "connections": [
    ["esp:2",  "r1:1", "green", []],
    ["r1:2",   "led1:A", "green", []],
    ["led1:C", "esp:GND.1", "black", []],

    ["esp:4",  "r2:1", "green", []],
    ["r2:2",   "led2:A", "green", []],
    ["led2:C", "esp:GND.1", "black", []],

    ["esp:5",  "r3:1", "green", []],
    ["r3:2",   "led3:A", "green", []],
    ["led3:C", "esp:GND.1", "black", []],

    ["esp:18", "r4:1", "green", []],
    ["r4:2",   "led4:A", "green", []],
    ["led4:C", "esp:GND.1", "black", []],

    ["esp:19", "bz1:1", "orange", []],
    ["bz1:2",  "esp:GND.1", "black", []]
  ]
}



