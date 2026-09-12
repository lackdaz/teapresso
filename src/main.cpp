#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <SoftwareSerial.h>
#include "colors.h"

// ---------------------------------------------------------------- hardware

#define LED_PIN D4
#define LED_NUM 7
#define LED_STEP_MS 50
#define LED_SWEEP_MS (LED_NUM * LED_STEP_MS)

#define SSR_PIN D7

// VC-02 offline voice module. One-way: its TX into our RX. 9600 is slow
// enough for SoftwareSerial to be reliable, which keeps the hardware UART
// free for the Pi.
#define VC_RX_PIN D5
#define VC_TX_PIN D6
#define VC_BAUD 9600

// Frame: A5 5A <id> <~id>. IDs stay in 01-7F so an id byte can never be
// mistaken for a header byte and resync is always unambiguous.
#define VC_H1 0xA5
#define VC_H2 0x5A

#define CMD_TEAPRESSO 0x01
#define CMD_BREW 0x02
#define CMD_RINSE 0x03
#define CMD_CAFIZA 0x04
#define CMD_WAKE 0x10

// Command frames are ignored unless a wake arrived this recently. The
// module gates on its wake word internally; this is a second gate, because
// a false recognition here costs hot water.
#define WAKE_WINDOW_MS 10000

// Hard ceilings. These bound what any command can ask for, whatever the
// recipe file or a driving agent says. Pump datasheet allows 90 s
// continuous; 30 is a deliberately conservative single-pulse limit.
#define PUMP_MAX_S 30
#define WAIT_MAX_S 120
#define CYCLES_MAX 12

#define RINSE_CYCLES 3
#define CAFIZA_CYCLES 5
#define FLUSH_B 2
#define FLUSH_W 5

#define CMD_MAX 32

// ---------------------------------------------------------------- protocol
//
//   grammar:  verb  |  verb:args
//
//   ACK   <cmd>   command accepted, starting
//   STEP  <cmd>   progress within a multi-cycle command
//   DONE  <cmd>   command finished
//   ABORT <cmd>   multi-cycle command stopped early
//   NAK   <why>   rejected, nothing was done
//
// Exactly one ACK and one DONE/ABORT per accepted command. STEP lines are
// progress only and can be ignored by a parser.

Adafruit_NeoPixel leds = Adafruit_NeoPixel(LED_NUM, LED_PIN, NEO_GRB + NEO_KHZ800);
SoftwareSerial vc(VC_RX_PIN, VC_TX_PIN);

static uint8_t vc_frame[4];
static uint8_t vc_idx = 0;
static unsigned long woke_at = 0;
static bool vc_armed = false;

// Voice IDs map to text commands, so voice inherits every range check and
// every reply that the serial path has. There is one path to the pump.
// needs_wake is per-row: flipping a row re-gates just that command.
struct VoiceCmd
{
    uint8_t id;
    bool needs_wake;
    const char *cmd;
};

static const VoiceCmd VOICE[] = {
    {CMD_WAKE, false, "wake"},
    {CMD_TEAPRESSO, false, "easy:5"},
    {CMD_BREW, false, "more:1"},
    {CMD_RINSE, false, "rinse"},
    {CMD_CAFIZA, false, "cafiza"},
};

static char cmd[CMD_MAX];
static uint8_t cmd_len = 0;
static unsigned long cmd_last_rx = 0;

// A partial line with no newline behind it is a fragment left by a sender
// that went away. Discard it rather than let the next command concatenate
// onto it.
#define CMD_STALE_MS 2000

void led_set(uint8_t R, uint8_t G, uint8_t B);
void go_(const Colour &c);
bool cycle(int b, int w, bool quiet = false);
bool easy_recipe(uint8_t cycles);
bool brew_more(uint8_t cycles);
bool rinse_cycle();
bool cafiza_cycle();
void print_help();
void handle(const char *line);
void vc_poll();
void vc_command(uint8_t id);

// -------------------------------------------------------------------- leds

void led_set(uint8_t R, uint8_t G, uint8_t B)
{
    for (int i = 0; i < LED_NUM; i++)
    {
        leds.setPixelColor(i, leds.Color(R, G, B));
        leds.show();
        delay(LED_STEP_MS);
    }
}

void go_(const Colour &c)
{
    led_set(c.r, c.g, c.b);
}

// ------------------------------------------------------------------- setup

void setup()
{
    leds.begin();
    leds.clear();
    leds.show();

    digitalWrite(SSR_PIN, LOW);
    pinMode(SSR_PIN, OUTPUT);
    digitalWrite(SSR_PIN, LOW);

    Serial.begin(115200);
    vc.begin(VC_BAUD);
    delay(100);

    go_(PURPLE);
    go_(BLACK);

    print_help();
}

// -------------------------------------------------------------------- brew

// One cycle: pump for b seconds, then wait w seconds (0 = no wait).
// Range checks live here and nowhere else, so every path inherits them.
// Returns false without touching the pump if either value is out of range.
bool cycle(int b, int w, bool quiet)
{
    if (b < 1 || b > PUMP_MAX_S)
    {
        Serial.printf("NAK brew-range %d (1-%d)\n", b, PUMP_MAX_S);
        return false;
    }
    if (w < 0 || w > WAIT_MAX_S)
    {
        Serial.printf("NAK wait-range %d (0-%d)\n", w, WAIT_MAX_S);
        return false;
    }

    Serial.printf("%s brew:%d,%d\n", quiet ? "STEP" : "ACK", b, w);

    // Anything sent during the previous step is stale by now. Drop it
    // rather than let fragments be misparsed as the next command.
    while (Serial.available())
        Serial.read();

    digitalWrite(SSR_PIN, HIGH);
    go_(GREEN);
    delay(b * 1000 - LED_SWEEP_MS);

    digitalWrite(SSR_PIN, LOW);

    if (w)
    {
        go_(BLUE);
        delay(w * 1000 - LED_SWEEP_MS);
    }
    go_(BLACK);

    if (!quiet)
        Serial.printf("DONE brew:%d,%d\n", b, w);
    return true;
}

// C1 and C2 ramp up; cycle 3 onward repeat CX.
// No wait after the final pulse of a sequence.
bool easy_recipe(uint8_t cycles)
{
    if (!cycle(C1_B, cycles > 1 ? C1_W : 0, true))
        return false;
    if (cycles < 2)
        return true;
    if (!cycle(C2_B, cycles > 2 ? C2_W : 0, true))
        return false;

    for (uint8_t n = 3; n <= cycles; n++)
        if (!cycle(CX_B, n < cycles ? CX_W : 0, true))
            return false;

    return true;
}

// Extra CX pulls on the end of a finished brew.
bool brew_more(uint8_t cycles)
{
    for (uint8_t n = 1; n <= cycles; n++)
        if (!cycle(CX_B, n < cycles ? CX_W : 0, true))
            return false;

    return true;
}

// ------------------------------------------------------------------ flush

// n identical pulses with a gap between them, no wait after the last.
// Unvalved brew path, so the group drains through the gaps.
static bool pulse_n(uint8_t n, int b, int w)
{
    for (uint8_t i = 1; i <= n; i++)
        if (!cycle(b, i < n ? w : 0, true))
            return false;

    return true;
}

bool rinse_cycle()
{
    return pulse_n(RINSE_CYCLES, FLUSH_B, FLUSH_W);
}

bool cafiza_cycle()
{
    return pulse_n(CAFIZA_CYCLES, FLUSH_B, FLUSH_W);
}

// -------------------------------------------------------------------- help

void print_help()
{
    Serial.println();
    Serial.println(F("  teapresso"));
    Serial.println(F("  ---------"));
    Serial.printf("  C1 %2ds/%2ds    C2 %2ds/%2ds    CX %2ds/%2ds\n",
                  C1_B, C1_W, C2_B, C2_W, CX_B, CX_W);
    Serial.printf("  limits: pump 1-%ds, wait 0-%ds, cycles 1-%d\n",
                  PUMP_MAX_S, WAIT_MAX_S, CYCLES_MAX);
    Serial.println();
    Serial.println(F("  brew:B,W   one cycle - pump B s, then wait W s"));
    Serial.println(F("  easy[:n]   run profile, n cycles (default 5)"));
    Serial.println(F("  more[:n]   n extra CX pulls (default 1)"));
    Serial.printf("  rinse      %dx %ds pulses, %ds apart\n",
                  RINSE_CYCLES, FLUSH_B, FLUSH_W);
    Serial.printf("  cafiza     %dx %ds pulses, %ds apart\n",
                  CAFIZA_CYCLES, FLUSH_B, FLUSH_W);
    Serial.println(F("  wake       listening indicator on"));
    Serial.println(F("  off        pump off now"));
    Serial.println(F("  test       led sweep"));
    Serial.println(F("  reset      restart board"));
    Serial.println(F("  h          this"));
    Serial.println();
    Serial.println(F("  ACK accepted / STEP progress / DONE finished / NAK rejected"));
    Serial.println();
}

// ---------------------------------------------------------------- commands

// True if line is exactly verb, or verb followed by ':'.
// Sets *arg to the text after the colon, or nullptr if there was none.
static bool verb_is(const char *line, const char *verb, const char **arg)
{
    size_t n = strlen(verb);
    if (strncmp(line, verb, n))
        return false;
    if (line[n] == '\0')
    {
        *arg = nullptr;
        return true;
    }
    if (line[n] == ':')
    {
        *arg = line + n + 1;
        return true;
    }
    return false;
}

// Parses arg as an int, or yields fallback when there was no arg.
// False means the arg was present but not a number.
static bool arg_int(const char *arg, int fallback, int *out)
{
    if (!arg)
    {
        *out = fallback;
        return true;
    }
    return sscanf(arg, "%d", out) == 1;
}

void handle(const char *line)
{
    const char *arg;

    if (verb_is(line, "brew", &arg))
    {
        int b, w;
        if (!arg || sscanf(arg, "%d,%d", &b, &w) != 2)
        {
            Serial.println(F("NAK bad-args (want brew:B,W)"));
            return;
        }
        cycle(b, w);
    }
    else if (verb_is(line, "easy", &arg))
    {
        int n;
        if (!arg_int(arg, 5, &n))
        {
            Serial.println(F("NAK bad-args"));
            return;
        }
        if (n < 1 || n > CYCLES_MAX)
        {
            Serial.printf("NAK cycles-range %d (1-%d)\n", n, CYCLES_MAX);
            return;
        }
        Serial.printf("ACK easy:%d\n", n);
        bool ok = easy_recipe(n);
        Serial.printf("%s easy:%d\n", ok ? "DONE" : "ABORT", n);
    }
    else if (verb_is(line, "more", &arg))
    {
        int n;
        if (!arg_int(arg, 1, &n))
        {
            Serial.println(F("NAK bad-args"));
            return;
        }
        if (n < 1 || n > CYCLES_MAX)
        {
            Serial.printf("NAK cycles-range %d (1-%d)\n", n, CYCLES_MAX);
            return;
        }
        Serial.printf("ACK more:%d\n", n);
        bool ok = brew_more(n);
        Serial.printf("%s more:%d\n", ok ? "DONE" : "ABORT", n);
    }
    else if (!strcmp(line, "rinse"))
    {
        Serial.println(F("ACK rinse"));
        bool ok = rinse_cycle();
        Serial.printf("%s rinse\n", ok ? "DONE" : "ABORT");
    }
    else if (!strcmp(line, "cafiza"))
    {
        Serial.println(F("ACK cafiza"));
        bool ok = cafiza_cycle();
        Serial.printf("%s cafiza\n", ok ? "DONE" : "ABORT");
    }
    else if (!strcmp(line, "wake"))
    {
        woke_at = millis();
        vc_armed = true;
        go_(BLUE);
        Serial.println(F("DONE wake"));
    }
    else if (!strcmp(line, "off"))
    {
        digitalWrite(SSR_PIN, LOW);
        go_(BLACK);
        Serial.println(F("DONE off"));
    }
    else if (!strcmp(line, "test"))
    {
        Serial.println(F("ACK test"));
        go_(GREEN);
        go_(BLUE);
        go_(BLACK);
        Serial.println(F("DONE test"));
    }
    else if (!strcmp(line, "reset"))
    {
        Serial.println(F("ACK reset"));
        Serial.flush();
        digitalWrite(SSR_PIN, LOW);
        delay(50);
        ESP.restart();
    }
    else if (!strcmp(line, "h"))
    {
        print_help();
    }
    else
    {
        Serial.printf("NAK unknown %s\n", line);
    }
}

// ------------------------------------------------------------------- voice

// Voice commands are turned into the same text commands the Pi sends, so
// they inherit every range check and every reply. There is no second path
// to the pump.
void vc_command(uint8_t id)
{
    for (const VoiceCmd &v : VOICE)
    {
        if (v.id != id)
            continue;

        if (v.needs_wake)
        {
            // A flag rather than arithmetic on woke_at: millis() starts
            // near zero, so a sentinel of 0 would leave the gate open for
            // the first seconds after boot.
            if (!vc_armed || millis() - woke_at > WAKE_WINDOW_MS)
            {
                Serial.printf("VOICE ignored 0x%02X (needs wake)\n", id);
                vc_armed = false;
                return;
            }
            vc_armed = false; // one command per wake
        }

        Serial.printf("VOICE 0x%02X -> %s\n", id, v.cmd);
        handle(v.cmd);
        return;
    }

    Serial.printf("VOICE unknown 0x%02X\n", id);
}

void vc_poll()
{
    while (vc.available())
    {
        uint8_t b = vc.read();

        // Guards before the store: a stray byte can never advance the
        // state machine, so a corrupt frame costs one command rather than
        // desynchronising permanently.
        if (vc_idx == 0 && b != VC_H1)
            continue;
        if (vc_idx == 1 && b != VC_H2)
        {
            vc_idx = 0;
            continue;
        }

        vc_frame[vc_idx++] = b;

        if (vc_idx == 4)
        {
            vc_idx = 0;
            uint8_t id = vc_frame[2];
            if ((uint8_t)~id == vc_frame[3])
                vc_command(id);
            else
                Serial.println(F("VOICE checksum fail"));
        }
    }
}

// -------------------------------------------------------------------- loop

void loop()
{
    // Drop a partial line that has gone quiet, so the next command cannot
    // be prefixed by a leftover fragment.
    if (cmd_len && millis() - cmd_last_rx > CMD_STALE_MS)
        cmd_len = 0;

    // Clear the listening indicator when the wake window lapses.
    if (vc_armed && millis() - woke_at > WAKE_WINDOW_MS)
    {
        vc_armed = false;
        go_(BLACK);
    }

    vc_poll();

    while (Serial.available())
    {
        char c = Serial.read();
        cmd_last_rx = millis();

        if (c == '\r')
            continue;

        if (c == '\n')
        {
            cmd[cmd_len] = '\0';

            // Copy out before resetting: handle() blocks for the length of
            // a brew, and loop() must not be writing into the buffer that
            // handle() is still reading from.
            char line[CMD_MAX];
            strcpy(line, cmd);
            cmd_len = 0;

            if (line[0])
                handle(line);
        }
        else if (cmd_len < CMD_MAX - 1)
        {
            cmd[cmd_len++] = c;
        }
    }
}