# Параметры

## Пины на Arduino

Пины для **JQ6500** можно изменить при необходимости:

```cpp
#define JQ_TX 4   // TX Arduino → RX JQ6500 через 1кОм
#define JQ_RX 5   // RX Arduino ← TX JQ6500
```

Пин для **LED**:

```cpp
#define LED_PIN 3
```

Пины кнопок:

```cpp
#define BLUEBTN   6
#define ORANGEBTN 7
#define SONGBTN   8
```


## Переменные и константы аудио

Громкость **JQ6500** (0–30):

```cpp
#define JQ_VOLUME 30
```

Список мелодий в **JQ6500**:

```cpp
#define SND_FAKE        0 
#define SND_POWER_UP    1
#define SND_IDLE        2
#define SND_BLUE_FIRE   3
#define SND_ORANGE_FIRE 4
#define SND_POWER_DOWN  5
#define SND_SONG        6
```

> Мелодия `SND_FAKE` ненастоящая — нужна для некоторых процессов.

Длительность мелодий (для определения завершения звуков).  
При изменении нужно пересчитать:

```cpp
const unsigned long snd_duration_ms[] = {0, 1358, 32809, 1332, 1488, 1776, 175046};
```


## Переменные и константы LED

Количество светодиодов (0 — центр, 1–12 — круг):

```cpp
#define LED_NUM 13
```

Длительность кадра LED (мс):

```cpp
const unsigned long ledFrameDelay = 15;
```
