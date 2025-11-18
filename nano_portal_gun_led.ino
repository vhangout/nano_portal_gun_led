#include <avr/pgmspace.h>
#include <SoftwareSerial.h>
#include <FastLED.h>

// --- Пины JQ6500 --- 
// можно изменить при необходимости
#define JQ_TX 4   // TX Arduino → RX JQ6500 через 1кОм
#define JQ_RX 5   // RX Arduino ← TX JQ6500

// --- Пин LED ---
#define LED_PIN 3

// --- Пины кнопок ---
#define BLUEBTN   6
#define ORANGEBTN 7
#define SPEECHBTN 8
#define SONGBTN   9

// ******************************************
//        переменные и константы аудио
// ******************************************
#define JQ_VOLUME 30 // громкость JQ6500 <0-30>

#define SND_FAKE        0 
#define SND_POWER_UP    1
#define SND_BLUE_FIRE   2
#define SND_ORANGE_FIRE 3
#define SND_SONG        4

const unsigned long snd_duration_ms[] = {0, 1358, 1332, 1488, 175046, 5485, 4048, 2403, 3056, 2089, 3134, 1880, 4440, 4702, 3892, 3056, 3683, 5433, 1854, 1436};
uint8_t snd_speech_sounds[] = {5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
uint8_t snd_speech_size = sizeof(snd_speech_sounds);
uint8_t snd_speech_current = 0;

SoftwareSerial jqSerial(JQ_RX, JQ_TX); // RX, TX


// ******************************************
//         переменные и константы LED
// ******************************************
#define LED_NUM 14    // количество светодиодов (0-1 центр, 2-13 круг)
#define LED_RING_MIN 2
#define LED_RING_MAX 13

#define LED_IDLE_MIN 10
#define LED_IDLE_MAX 150
#define LED_SHOT_SWITCH 66

#define COLOR_BLACK 0x00000
#define COLOR_BLUE 0x0000FF
#define COLOR_ORANGE 0xFF8000
#define COLOR_RED 0x7F0000

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
const uint8_t btn_pins[] =  {SONGBTN, BLUEBTN, ORANGEBTN, SPEECHBTN};

// FSM состояния
enum State {
  STATE_OFF,
  STATE_IDLE,
  STATE_BLUE_FIRING,
  STATE_ORANGE_FIRING,
  STATE_SPEECH_PLAYING,
  STATE_SONG_PLAYING  
};
State currentState = STATE_OFF;

unsigned long startSoundTime = 0;
bool soundPlaying = false;
int current_sound = 0;

bool bluePressed = false;
bool orangePressed = false;
bool songPressed = false;
bool speechPressed = false;

// --- Антидребезг ---
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;
bool lastBlueState = HIGH, lastOrangeState = HIGH, lastSongState = HIGH, lastSpeechState = HIGH;
bool stableBlue = HIGH, stableOrange = HIGH, stableSong = HIGH, stableSpeech = HIGH;


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
  delay(1200); //пауза нужна для ожидания инициализации JQ6500
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

void stopSound() {
  uint8_t cmd1[] = {0x7E, 0x02, 0x0E, 0xEF};
  sendJQCommand(cmd1, sizeof(cmd1));      
  startSoundTime = millis();
  soundPlaying = true;
  current_sound = -1; // зацикленный звук
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

// void playIdleSound() {
//   uint8_t cmd1[] = {0x7E, 0x03, 0x11, 0x03, 0xEF};
//   sendJQCommand(cmd1, sizeof(cmd1));
//   delay(150);
//   uint8_t cmd2[] = {0x7E, 0x04, 0x03, 0x00, SND_IDLE, 0xEF};
//   sendJQCommand(cmd2, sizeof(cmd2));
//   startSoundTime = millis();
//   soundPlaying = true;
//   current_sound = -1; // зацикленный звук
// }

void speech_shuffle() {
  for (uint16_t i = snd_speech_size - 1; i > 0; i--) {
    uint16_t j = random(i + 1);  // случайный индекс от 0 до i включительно

    // обмен элементов
    uint8_t temp = snd_speech_sounds[i];
    snd_speech_sounds[i] = snd_speech_sounds[j];
    snd_speech_sounds[j] = temp;
  }
  uint8_t snd_speech_current = 0;
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

void setCenterPixel(uint32_t color) {
  leds[0] = color;
  leds[1] = color;
}

void setRingPixels(uint32_t color) {
  for (uint8_t i = LED_RING_MIN; i <= LED_RING_MAX; i++) leds[i] = color;
}


// таблица гамма-коррекции sRGB
const uint8_t gamma_table[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
  0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
  0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07,
  0x08, 0x08, 0x08, 0x08, 0x09, 0x09, 0x09, 0x0A, 0x0A, 0x0A, 0x0B, 0x0B, 0x0C, 0x0C, 0x0C, 0x0D,
  0x0D, 0x0D, 0x0E, 0x0E, 0x0F, 0x0F, 0x10, 0x10, 0x11, 0x11, 0x11, 0x12, 0x12, 0x13, 0x13, 0x14,
  0x14, 0x15, 0x16, 0x16, 0x17, 0x17, 0x18, 0x18, 0x19, 0x19, 0x1A, 0x1B, 0x1B, 0x1C, 0x1D, 0x1D,
  0x1E, 0x1E, 0x1F, 0x20, 0x20, 0x21, 0x22, 0x23, 0x23, 0x24, 0x25, 0x25, 0x26, 0x27, 0x28, 0x29,
  0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x33, 0x34, 0x35, 0x36,
  0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
  0x47, 0x48, 0x49, 0x4A, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x54, 0x55, 0x56, 0x57, 0x58,
  0x5A, 0x5B, 0x5C, 0x5D, 0x5F, 0x60, 0x61, 0x63, 0x64, 0x65, 0x67, 0x68, 0x69, 0x6B, 0x6C, 0x6D,
  0x6F, 0x70, 0x72, 0x73, 0x74, 0x76, 0x77, 0x79, 0x7A, 0x7C, 0x7D, 0x7F, 0x80, 0x82, 0x83, 0x85,
  0x86, 0x88, 0x8A, 0x8B, 0x8D, 0x8E, 0x90, 0x92, 0x93, 0x95, 0x97, 0x98, 0x9A, 0x9C, 0x9D, 0x9F,
  0xA1, 0xA3, 0xA4, 0xA6, 0xA8, 0xAA, 0xAB, 0xAD, 0xAF, 0xB1, 0xB3, 0xB5, 0xB7, 0xB8, 0xBA, 0xBC,
  0xBE, 0xC0, 0xC2, 0xC4, 0xC6, 0xC8, 0xCA, 0xCC, 0xCE, 0xD0, 0xD2, 0xD4, 0xD6, 0xD8, 0xDA, 0xDC,
  0xDE, 0xE0, 0xE2, 0xE5, 0xE7, 0xE9, 0xEB, 0xED, 0xEF, 0xF2, 0xF4, 0xF6, 0xF8, 0xFA, 0xFD, 0xFF
};

uint32_t idleRingColor = COLOR_BLACK;
uint8_t idleBright = 0;
int8_t idleIncBright = 1;
void idleLight(bool start = false) {
  if (start) {    
    idleBright = 15;
    idleIncBright = 1;
	setRingPixels(idleRingColor);	
  } 

    // плавный реверс направления
  if (idleBright >= LED_IDLE_MAX || idleBright <= LED_IDLE_MIN) {
    idleIncBright = -idleIncBright;
  }

  leds[0].setRGB(0, pgm_read_byte(&gamma_table[idleBright]), 0);
  leds[1].setRGB(0, pgm_read_byte(&gamma_table[idleBright]), 0);
  
  FastLED.show();
  idleBright += idleIncBright;
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

uint8_t pairIndex = 0;
int pairIndexSpeed = 0;
const int pairIndexDelay = 10; // скорость перемещения пар пикселов
uint8_t colorOffset = 0;
const uint8_t pairsLED[][2] = {{7, 8}, {6, 9},{5, 10},{4, 11},{3, 12},{2, 13}};
const uint8_t totalPairs = 6;
const uint8_t pairDelay = 3;   // шаг цвета (аналог скорости)
void pairedLight(bool start = false) {
  if (start) {
    pairIndex = 0;
    pairIndexSpeed = 0;
	setCenterPixel(COLOR_BLACK);
  }
  pairIndexSpeed++;
  if (pairIndexSpeed >= pairIndexDelay) {
    FastLED.clear();

    CRGB color = ColorFromPalette(RainbowColors_p, colorOffset, 255, LINEARBLEND);
    leds[pairsLED[pairIndex][0]] = color;
    leds[pairsLED[pairIndex][1]] = color;
    FastLED.show();

    colorOffset += pairDelay;
    pairIndex++;
    if (pairIndex >= totalPairs) {
      pairIndex = 0;  // начинаем с первой пары заново
    }
    pairIndexSpeed = 0;
  }
}

uint8_t shotLEDFrame = 0;
void shotLight(uint32_t color, bool start = false) {
  if (start) {
    shotLEDFrame = 0;
  }

  if (shotLEDFrame <= LED_SHOT_SWITCH) {
    setCenterPixel(color);
    setRingPixels(COLOR_BLACK);
  } else {
    setCenterPixel(COLOR_BLACK);
	setRingPixels(color);
  }

  FastLED.show();
  shotLEDFrame++;
}

void blueLight(bool start = false) {
  shotLight(COLOR_BLUE, start);
}

void orangeLight(bool start = false) {
  shotLight(COLOR_ORANGE, start);
}

void updateLED(bool start = false) {
  unsigned long now = millis();
  if (((now - lastLedFrameTime) > ledFrameDelay) || start) {
    lastLedFrameTime = now;
    if (currentEffect) {
      currentEffect(start);
    }
  }
}

void updateLEDEffect() {  
  switch (currentState) {
    case STATE_IDLE:           currentEffect = idleLight; break;
    case STATE_SONG_PLAYING:   
        currentEffect = songLight; 
        idleRingColor = COLOR_BLACK;
        break;
    case STATE_BLUE_FIRING:
        currentEffect = blueLight; 
        idleRingColor = COLOR_BLUE;
        break;
    case STATE_ORANGE_FIRING:
        currentEffect = orangeLight; 
		idleRingColor = COLOR_ORANGE;
		break;
    case STATE_SPEECH_PLAYING: 
		currentEffect = pairedLight;
		idleRingColor = COLOR_BLACK;
		break;
    default: 
	    currentEffect = nullptr; 
		idleRingColor = COLOR_BLACK;
		break; // ничего не делает
  }
  updateLED(true); // Запускаем новый эффект с начальной инициализацией
}


// ******************************************
//            Блок обработки кнопок
// ******************************************

bool downButton(bool btn, bool &trigger, State newState) {
  if (btn && !trigger) {
    trigger = true;
    currentState = newState;
    return true;
  }
  return false;
}

void waitUpButton(bool btn, bool &trigger, uint8_t sound, State newState) {
  if (!btn && trigger) {
    trigger = false;
    updateLEDEffect();
    playSound(sound);
  } 
  else if (!trigger && !soundPlaying) {      
      currentState = newState;
      soundPlaying = false;
  }
}

void waitPressedButton(bool btn, bool &trigger, uint8_t sound, State newState) {
  if (btn && trigger) {
    trigger = false;
    updateLEDEffect();
    playSound(sound);
  }
  else if (!btn || !soundPlaying) {    
    currentState = newState;
    soundPlaying = false;    
  }
}


// ******************************************
//             Основная программа
// ******************************************
void setup() {
  jqSerial.begin(9600);

  randomSeed(analogRead(A0)); // Инициализация генератора случайных чисел
  speech_shuffle();

  for (uint8_t i = 0; i < sizeof(btn_pins); i++) {
    pinMode(btn_pins[i], INPUT_PULLUP);
  }

  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, LED_NUM);
  FastLED.showColor(COLOR_RED);
    
  delay(1000);
  setVolume(JQ_VOLUME);
}

void loop() {
  updateLED();
  updateSoundPlaying();

  bool rawBlue    = digitalRead(BLUEBTN);
  bool rawOrange  = digitalRead(ORANGEBTN);
  bool rawSong    = digitalRead(SONGBTN);
  bool rawSpeech  = digitalRead(SPEECHBTN);  

  unsigned long now = millis();
  if (rawBlue != lastBlueState || rawOrange != lastOrangeState || rawSong != lastSongState || rawSpeech != lastSpeechState)
    lastDebounceTime = now;

  if ((now - lastDebounceTime) > debounceDelay) {
    stableBlue   = rawBlue;
    stableOrange = rawOrange;
    stableSong   = rawSong;
    stableSpeech = rawSpeech;
  }

  lastBlueState = rawBlue;
  lastOrangeState = rawOrange;
  lastSongState = rawSong;
  lastSpeechState = rawSpeech;

  bool blueOn   = stableBlue == LOW;
  bool orangeOn = stableOrange == LOW;
  bool songOn   = stableSong == LOW;
  bool speechOn = stableSpeech == LOW;

  switch (currentState) {
    case STATE_OFF:
      playWaitSound(SND_POWER_UP);
	  idleRingColor = COLOR_BLACK;
      currentState = STATE_IDLE;
      break;

    case STATE_IDLE:
      if (downButton(songOn, songPressed, STATE_SONG_PLAYING)) break;
      if (downButton(blueOn, bluePressed, STATE_BLUE_FIRING)) break;
      if (downButton(orangeOn, orangePressed, STATE_ORANGE_FIRING)) break;
      if (downButton(speechOn, speechPressed, STATE_SPEECH_PLAYING)) break;
      if (!soundPlaying) {
        updateLEDEffect();
        stopSound();     
      }
      break;

    case STATE_BLUE_FIRING:
      waitUpButton(blueOn, bluePressed, SND_BLUE_FIRE, STATE_IDLE);
      break;

    case STATE_ORANGE_FIRING:
      waitUpButton(orangeOn, orangePressed, SND_ORANGE_FIRE, STATE_IDLE);
      break;

    case STATE_SPEECH_PLAYING:    
      waitUpButton(speechOn, speechPressed, snd_speech_sounds[snd_speech_current], STATE_IDLE);
      snd_speech_current++;
      if (snd_speech_current == snd_speech_size) {
        speech_shuffle();
      }
      break;

    case STATE_SONG_PLAYING:
      waitPressedButton(songOn, songPressed, SND_SONG, STATE_IDLE);
      break;    
  }
}
