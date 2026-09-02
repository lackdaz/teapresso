#include <Arduino.h>

#include <Adafruit_NeoPixel.h>

#define PIN D4
#define LED_NUM 7

Adafruit_NeoPixel leds = Adafruit_NeoPixel(LED_NUM, PIN, NEO_GRB + NEO_KHZ800);

void brew_on(int delay);
void brew_off(int delay);
void easy_recipe();
void go_green();
void go_blue();
void go_red();
void go_black();

void setup()
{
    Serial.begin(115200);
    leds.begin(); // This initializes the NeoPixel library.
    leds.clear();
    leds.show();

    pinMode(D7, OUTPUT);
    delay(1000);
    digitalWrite(D7, HIGH);
    delay(5000);
    digitalWrite(D7, LOW);
}

void led_set(uint8 R, uint8 G, uint8 B)
{
    for (int i = 0; i < LED_NUM; i++)
    {
        leds.setPixelColor(i, leds.Color(R, G, B));
        leds.show();
        delay(50);
    }
}
void loop()
{

    if (Serial.available())
    {
        char c = Serial.read();
        if (c == '1')
        {
            easy_recipe();
            Serial.println("ready - send 1 to brew");
        }
    }
}

void easy_recipe()
{
    brew_on(5000);
    brew_off(20000); // C1
    brew_on(10000);
    brew_off(30000); // C2
    brew_on(15000);
    brew_off(30000); // C3
    brew_on(15000);
    brew_off(30000); // C4
    brew_on(15000);
    brew_off(30000); // C5
};

void go_green()
{
    led_set(0, 10, 0); // green
}

void go_red()
{
    led_set(10, 0, 0); // red
}

void go_blue()
{
    led_set(0, 0, 10); // blue
}

void go_black()
{
    led_set(0, 0, 0);
}

void brew_on(int delay_ms)
{
    go_green();
    digitalWrite(D7, HIGH);
    delay(delay_ms);
    go_black();
}

void brew_off(int delay_ms)
{
    go_red();
    digitalWrite(D7, LOW);
    delay(delay_ms);
    go_black();
}
