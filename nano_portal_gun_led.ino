#include <SoftwareSerial.h>
#include <FastLED.h>

// ******************************************
//        переменные и константы аудио
// ******************************************
// Пины JQ6500 (можно изменить при необходимости)
#define JQ_TX 4   // TX Arduino → RX JQ6500 через 1кОм
#define JQ_RX 5   // RX Arduino ← TX JQ6500

#define SND_FAKE        0 
#define SND_POWER_UP    1
#define SND_IDLE        2
#define SND_BLUE_FIRE   3
#define SND_ORANGE_FIRE 4
#define SND_POWER_DOWN  5
#define SND_SONG        6

const unsigned long snd_duration_ms[] = {0, 1358, 32809, 1332, 1488, 1776, 175046};

SoftwareSerial jqSerial(JQ_RX, JQ_TX); // RX, TX


// ******************************************
//         переменные и константы LED
// ******************************************
#define LED_PIN 3     // пин
#define LED_NUM 13    // количество светодиодов (0 центр, 1-12 круг)

#define LED_IDLE_MIN 10
#define LED_IDLE_MAX 150
#define LED_SHOT_SWITCH 66

CRGB leds[LED_NUM];

const unsigned long ledFrameDelay = 15; //длительность кадра LED мс
unsigned long lastLedFrameTime = 0;

// --- Тип указателя на функцию ---
typedef void (*LedEffect)(bool start);
// --- Текущий эффект ---
LedEffect currentEffect = nullptr;

// *******************************************
//  переменные и константы кнопок и состояния
// *******************************************
// --- Пины кнопок ---
#define BLUEBTN   6
#define ORANGEBTN 7
#define SONGBTN   8

const uint8_t btn_pins[] =  {SONGBTN, BLUEBTN, ORANGEBTN};

// FSM состояния
enum State {
  STATE_OFF,
  STATE_IDLE,
  STATE_BLUE_FIRING,
  STATE_ORANGE_FIRING,
  STATE_SONG_PLAYING,
  STATE_SONG_IDLE,
  STATE_SONG_END
};
State currentState = STATE_OFF;

unsigned long startSoundTime = 0;
bool soundPlaying = false;
int current_sound = 0;

bool bluePressed = false;
bool orangePressed = false;
bool songPressed = false;

// --- Антидребезг ---
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;
bool lastBlueState = HIGH, lastOrangeState = HIGH, lastSongState = HIGH;
bool stableBlue = HIGH, stableOrange = HIGH, stableSong = HIGH;


// ******************************************
//               Звуковой блок
// ******************************************
void sendJQCommand(const uint8_t *cmd, size_t len) {
  jqSerial.write(cmd, len);
  jqSerial.flush();
}

void jqInit() {
  uint8_t cmd[] = {0x7E, 0x03, 0x09, 0x04, 0xEF};
  sendJQCommand(cmd, sizeof(cmd));
  delay(150);
}

void jqReset() {
  uint8_t cmd[] = {0x7E, 0x02, 0x0C, 0xEF};
  sendJQCommand(cmd, sizeof(cmd));
  delay(500);
}

void setVolume(uint8_t vol) {
  if (vol > 30) vol = 30;
  uint8_t cmd[] = {0x7E, 0x03, 0x06, vol, 0xEF};
  sendJQCommand(cmd, sizeof(cmd));
}

void playSound(uint8_t track) {
  if (track > 0) {
    uint8_t cmd1[] = {0x7E, 0x03, 0x11, 0x04, 0xEF};
    sendJQCommand(cmd1, sizeof(cmd1));
    delay(150);
    uint8_t cmd[] = {0x7E, 0x04, 0x03, 0x00, track, 0xEF};
    sendJQCommand(cmd, sizeof(cmd));
  }
  startSoundTime = millis();
  soundPlaying = true;
  current_sound = track;
}

void playWaitSound(uint8_t track) {
  if (track > 0) {
    uint8_t cmd1[] = {0x7E, 0x03, 0x11, 0x04, 0xEF};
    sendJQCommand(cmd1, sizeof(cmd1));
    delay(150);
    uint8_t cmd[] = {0x7E, 0x04, 0x03, 0x00, track, 0xEF};
    sendJQCommand(cmd, sizeof(cmd));
  }
  delay(snd_duration_ms[track]);
}

void playIdleSound() {
  uint8_t cmd1[] = {0x7E, 0x03, 0x11, 0x03, 0xEF};
  sendJQCommand(cmd1, sizeof(cmd1));
  delay(150);
  uint8_t cmd2[] = {0x7E, 0x04, 0x03, 0x00, SND_IDLE, 0xEF};
  sendJQCommand(cmd2, sizeof(cmd2));
  startSoundTime = millis();
  soundPlaying = true;
  current_sound = -1; // зацикленный звук
}

void updateSoundPlaying() {
  unsigned long now = millis();
  if (current_sound >= 0 && soundPlaying && (now - startSoundTime) > snd_duration_ms[current_sound]) {
    soundPlaying = false;
  }
}


// ******************************************
//                  Блок LED
// ******************************************
// простая гамма-коррекция (приблизительная, быстрая)
uint8_t gamma8(uint8_t x) {
  // степень ~2.2
  return (uint8_t)(pow((float)x / 255.0, 2.2) * 255.0 + 0.5);
}

uint8_t idleBright = 0;
int8_t idleIncBright = 1;
void idleLight(bool start = false) {
  if (start) {    
    idleBright = 15;
    idleIncBright = abs(idleIncBright);
  } 

    // плавный реверс направления
  if (idleBright >= LED_IDLE_MAX || idleBright <= LED_IDLE_MIN) {
    idleIncBright = -idleIncBright;
  }

  idleBright += idleIncBright;

  uint8_t corrected = gamma8(idleBright);

  for (uint8_t i = 0; i < LED_NUM; i++) {
    leds[i].setRGB(0, corrected, 0);
  }
  
  FastLED.show();
}

uint8_t songLEDColorIndex = 0;
const uint8_t songLEDSpeed = 3;
void songLight(bool start = false) {
  if (start) {
    songLEDColorIndex = 0;
  }  

  fill_palette(leds, LED_NUM, songLEDColorIndex, 255 / LED_NUM,
               RainbowColors_p, 255, LINEARBLEND);

  FastLED.show();
  songLEDColorIndex += songLEDSpeed;
}

uint8_t shotLEDFrame = 0;
void shotLight(uint32_t color, bool start = false) {
  if (start) {
    shotLEDFrame = 0;
  }

  if (shotLEDFrame <= LED_SHOT_SWITCH) {
    leds[0] = color;
    for (uint8_t i = 1; i < LED_NUM; i++) leds[i] = 0x00000;
  } else {
    leds[0] = 0x00000;
    for (uint8_t i = 1; i < LED_NUM; i++) leds[i] = color;
  }

  FastLED.show();
  shotLEDFrame++;
}

void blueLight(bool start = false) {
  shotLight(0x0000FF, start);
}

void orangeLight(bool start = false) {
  shotLight(0xFF8000, start);
}

void updateLed(bool start = false) {
  unsigned long now = millis();
  if (((now - lastLedFrameTime) > ledFrameDelay) || start) {
    lastLedFrameTime = now;
    if (currentEffect) {
      currentEffect(start);
    }
  }
}


// ******************************************
//            Блок обработки кнопок
// ******************************************

void setState(State newState) {
  currentState = newState;
  switch (currentState) {
    case STATE_IDLE:          currentEffect = idleLight; break;
    case STATE_SONG_PLAYING:
    case STATE_SONG_IDLE:
    case STATE_SONG_END:      currentEffect = songLight; break;
    case STATE_BLUE_FIRING:   currentEffect = blueLight; break;
    case STATE_ORANGE_FIRING: currentEffect = orangeLight; break;
    default:                  currentEffect = nullptr; break; // ничего не делает
  }
  updateLed(true); // Запускаем новый эффект с начальной инициализацией
}


bool downButton(bool btn, bool &trigger, State newState) {
  if (btn && !trigger) {
    trigger = true;
    setState(newState);
    return true;
  }
  return false;
}

bool waitUpButton(bool btn, bool &trigger, uint8_t sound, State newState) {
  if (!btn && trigger) {
    trigger = false;
    playSound(sound);
    return true;
  } else if (!trigger && !soundPlaying) {
    setState(newState);
  }
  return false;
}


// ******************************************
//             Основная программа
// ******************************************
void setup() {
  jqSerial.begin(9600);

  for (uint8_t i = 0; i < sizeof(btn_pins); i++) {
    pinMode(btn_pins[i], INPUT_PULLUP);
  }

  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, LED_NUM);
  FastLED.setBrightness(50);

  jqReset();
  setVolume(25);
  delay(1000);
}

void loop() {
  updateLed();
  updateSoundPlaying();

  bool rawBlue    = digitalRead(BLUEBTN);
  bool rawOrange  = digitalRead(ORANGEBTN);
  bool rawSong    = digitalRead(SONGBTN);

  unsigned long now = millis();
  if (rawBlue != lastBlueState || rawOrange != lastOrangeState || rawSong != lastSongState)
    lastDebounceTime = now;

  if ((now - lastDebounceTime) > debounceDelay) {
    stableBlue   = rawBlue;
    stableOrange = rawOrange;
    stableSong   = rawSong;
  }

  lastBlueState = rawBlue;
  lastOrangeState = rawOrange;
  lastSongState = rawSong;

  bool blueOn   = stableBlue == LOW;
  bool orangeOn = stableOrange == LOW;
  bool songOn   = stableSong == LOW;

  switch (currentState) {
    case STATE_OFF:
      playWaitSound(SND_POWER_UP);
      setState(STATE_IDLE);
      break;

    case STATE_IDLE:
      if (downButton(blueOn, bluePressed, STATE_BLUE_FIRING)) break;
      if (downButton(orangeOn, orangePressed, STATE_ORANGE_FIRING)) break;
      if (downButton(songOn, songPressed, STATE_SONG_PLAYING)) break;
      if (!soundPlaying) playIdleSound();
      break;

    case STATE_BLUE_FIRING:
      waitUpButton(blueOn, bluePressed, SND_BLUE_FIRE, STATE_IDLE);
      break;

    case STATE_ORANGE_FIRING:
      waitUpButton(orangeOn, orangePressed, SND_ORANGE_FIRE, STATE_IDLE);
      break;

    case STATE_SONG_PLAYING:
      if (waitUpButton(songOn, songPressed, SND_SONG, STATE_SONG_IDLE))
        setState(STATE_SONG_IDLE);
      break;

    case STATE_SONG_IDLE:
      if (!soundPlaying)
        songOn = true;
      downButton(songOn, songPressed, STATE_SONG_END);
      break;

    case STATE_SONG_END:
      if (!soundPlaying)
        songPressed = false;
      waitUpButton(songOn, songPressed, SND_FAKE, STATE_IDLE);
      break;
  }
}
