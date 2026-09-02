#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "colors.h"

#define PIN D4
#define LED_NUM 7
#define LED_STEP_MS 50
#define LED_SWEEP_MS (LED_NUM * LED_STEP_MS)

Adafruit_NeoPixel leds = Adafruit_NeoPixel(LED_NUM, PIN, NEO_GRB + NEO_KHZ800);

void brew_on(int delay_ms);
void brew_off(int delay_ms);
void brew_more(uint8_t cycles);
void easy_recipe(uint8_t cycles);
void led_set(uint8_t R, uint8_t G, uint8_t B);
void go_green();
void go_black();
void go_(const Colour &c);

void setup()
{
    leds.begin();

    pinMode(D7, OUTPUT);
    digitalWrite(D7, LOW);

    Serial.begin(115000);
    Serial.println("ready - send 1 to brew");

    go_(PURPLE);
    go_(BLACK);
}

void led_set(uint8_t R, uint8_t G, uint8_t B)
{
    for (int i = 0; i < LED_NUM; i++)
    {
        leds.setPixelColor(i, leds.Color(R, G, B));
        leds.show();
        delay(LED_STEP_MS);
    }
}

void loop()
{
    if (Serial.available())
    {
        char c = Serial.read();
        if (c == '1')
        {
            easy_recipe(3);
            Serial.println("got 1");
        }
        else if (c == '2')
        {
            brew_more(1);
            Serial.flush();
        }
        else if (c == '3')
        {
            go_green();
            go_black();
            Serial.flush();
        }
        else if (c == '0')
        {
            Serial.println("restarting...");
            Serial.flush();
            digitalWrite(D7, LOW);
            delay(50);
            ESP.restart();
        }
    }
}

void go_green()
{
    led_set(0, 10, 0); // green
}

void go_black()
{
    led_set(0, 0, 0);
}

void go_(const Colour &c)
{
    led_set(c.r, c.g, c.b);
}

void easy_recipe(uint8_t cycles)
{
    brew_on(C1_B * 1000);
    brew_off(C1_W * 1000);
    brew_on(C2_B * 1000);
    brew_off(C2_W * 1000);

    for (uint8_t n = 3; n <= cycles; n++)
    {
        brew_on(CX_B * 1000);
        if (n < cycles)
            brew_off(CX_W * 1000);
    }

    digitalWrite(D7, LOW);
    go_(BLACK);
}

void brew_on(int delay_ms)
{
    digitalWrite(D7, HIGH);
    go_(GREEN);
    delay(delay_ms - LED_SWEEP_MS);
}

void brew_off(int delay_ms)
{
    digitalWrite(D7, LOW);
    go_(BLUE);
    delay(delay_ms - LED_SWEEP_MS);
    go_black();
}

void brew_more(uint8_t cycles)
{
    for (uint8_t n = 1; n <= cycles; n++)
    {
        brew_on(CX_B * 1000);
        if (n < cycles)
            brew_off(CX_W * 1000);
    }
};
