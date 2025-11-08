#include <SoftwareSerial.h>

// Пины JQ6500 (можно изменить при необходимости)
#define JQ_TX 4   // TX Arduino → RX JQ6500 через 1кОм
#define JQ_RX 5   // RX Arduino ← TX JQ6500

SoftwareSerial jqSerial(JQ_RX, JQ_TX); // RX, TX

// --- Пины кнопок ---
#define BLUEBTN   6
#define ORANGEBTN 7
#define SONGBTN   8
#define POWERSW   9

const uint8_t btn_pins[] =  {SONGBTN, POWERSW, BLUEBTN, ORANGEBTN};

// JQ6500 использует Serial (TX=1, RX=0 на Nano)
#define SND_FAKE        0 
#define SND_POWER_UP    1
#define SND_IDLE        2
#define SND_BLUE_FIRE   3
#define SND_ORANGE_FIRE 4
#define SND_POWER_DOWN  5
#define SND_SONG        6

const unsigned long snd_duration_ms[] = {0, 1358, 32809, 1332, 1488, 1776, 175046};

// FSM состояния
enum State {
  STATE_OFF,
  STATE_POWERING_UP,
  STATE_IDLE,
  STATE_BLUE_FIRING,
  STATE_ORANGE_FIRING,
  STATE_POWERING_DOWN,
  STATE_SONG_PLAYING,
  STATE_SONG_IDLE,
  STATE_SONG_END
};
State currentState = STATE_OFF;

unsigned long startSoundTime = 0;
bool soundPlaying = false;
int current_sound = 0;

bool powerPressed = false;
bool bluePressed = false;
bool orangePressed = false;
bool songPressed = false;

// --- Антидребезг ---
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;
bool lastPowerState = HIGH, lastBlueState = HIGH, lastOrangeState = HIGH, lastSongState = HIGH;
bool stablePower = HIGH, stableBlue = HIGH, stableOrange = HIGH, stableSong = HIGH;

// --- JQ6500 команды ---
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

void setup() {
  jqSerial.begin(9600);    // <-- Nano UART TX=1 RX=0

  for (uint8_t i = 0; i < sizeof(btn_pins); i++) {
    pinMode(btn_pins[i], INPUT_PULLUP);
  }

  jqReset();
  setVolume(25);
}

bool downButton(bool btn, bool &trigger, State newState) {
  if (btn && !trigger) {
    trigger = true;
    currentState = newState;
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
    currentState = newState;
  }
  return false;
}

void updateSoundPlaying() {
  unsigned long now = millis();
  if (current_sound >= 0 && soundPlaying && (now - startSoundTime) > snd_duration_ms[current_sound]) {
    soundPlaying = false;
  }
}

void loop() {
  updateSoundPlaying();

  bool rawPower   = digitalRead(POWERSW);
  bool rawBlue    = digitalRead(BLUEBTN);
  bool rawOrange  = digitalRead(ORANGEBTN);
  bool rawSong    = digitalRead(SONGBTN);

  unsigned long now = millis();
  if (rawPower != lastPowerState || rawBlue != lastBlueState || rawOrange != lastOrangeState || rawSong != lastSongState)
    lastDebounceTime = now;

  if ((now - lastDebounceTime) > debounceDelay) {
    stablePower  = rawPower;
    stableBlue   = rawBlue;
    stableOrange = rawOrange;
    stableSong   = rawSong;
  }

  lastPowerState = rawPower;
  lastBlueState = rawBlue;
  lastOrangeState = rawOrange;
  lastSongState = rawSong;

  bool powerOn  = stablePower == LOW;
  bool blueOn   = stableBlue == LOW;
  bool orangeOn = stableOrange == LOW;
  bool songOn   = stableSong == LOW;

  switch (currentState) {
    case STATE_OFF:
      downButton(powerOn, powerPressed, STATE_POWERING_UP);
      break;

    case STATE_POWERING_UP:
      waitUpButton(powerOn, powerPressed, SND_POWER_UP, STATE_IDLE);
      break;

    case STATE_IDLE:
      if (downButton(powerOn, powerPressed, STATE_POWERING_DOWN)) break;
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
        currentState = STATE_SONG_IDLE;
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

    case STATE_POWERING_DOWN:
      waitUpButton(powerOn, powerPressed, SND_POWER_DOWN, STATE_OFF);
      break;
  }
}
