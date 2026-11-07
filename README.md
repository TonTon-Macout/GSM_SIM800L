# GSM_SIM800L

Неблокирующая библиотека для модулей **SIM800L** (и совместимых) под Arduino / ESP32 / ESP8266.



### Возможности
- Полностью асинхронная работа: нет `delay()`, нет `String`, нет динамической памяти
- Очередь AT-команд с приоритетами и эксклюзивными транзакциями
- Автоматический приём SMS (UCS2 → UTF-8), отправка SMS, USSD
- Голосовые звонки: входящие (CLIP), ответ, сброс, исходящие
- Whitelist номеров (Auth) с нормализацией `8…` / `7…` → `+7…`
- Мониторинг сети: сигнал, регистрация, оператор, состояние SIM
- Recovery-механизм при зависании модема
- Подробные ошибки CME/CMS + русские тексты

### Совместимость
- Arduino C++11 и выше
- ESP32, ESP8266, Arduino Mega / ATmega2560 и любые платы с `Stream`
- Скетч сам инициализирует UART и выбирает пины

### Зависимости
Нет внешних зависимостей.

## Содержание
- [Подключение](#wiring)
- [Быстрый старт](#quickstart)
- [Документация](#usage)
  - [gsm::GSM](#gsm)
  - [Колбэки (ModemCallbacks)](#callbacks)
  - [SMS](#sms)
  - [Звонки](#calls)
  - [USSD](#ussd)
  - [Сеть и диагностика](#network)
  - [Auth — whitelist](#auth)
  - [Recovery](#recovery)
  - [Низкоуровневые команды](#raw)
  - [Типы и коды ошибок](#types)
- [Важные семантические моменты](#semantics)
- [Ограничения](#limits)
- [Версии](#versions)
- [Установка](#install)
- [Баги и обратная связь](#feedback)

<a id="wiring"></a>

## Подключение

Типовая схема SIM800L:

```
SIM800L          МК
--------         --------
VCC      →       4.0…4.2 В (отдельный источник, не от 3.3 В МК!)
GND      →       GND
TXD      →       RX МК
RXD      →       TX МК (через делитель 5→3.3 В, если МК 3.3 В)
RST      →       любой GPIO (опционально, для recovery)
PWRKEY   →       любой GPIO (опционально)
```

> [!IMPORTANT]
> SIM800L очень чувствителен к питанию. При передаче ток может кратковременно достигать 2 А. Обязателен конденсатор 1000–2200 мкФ рядом с модулем и стабильный источник 4 В.

Скетч **сам** владеет пинами RESET/PWRKEY и питанием. Библиотека только сообщает, когда нужна аппаратная перезагрузка (`onRecoveryRequired`).

<a id="quickstart"></a>

## Быстрый старт

```cpp
#include <Arduino.h>
#include <GSM.h>

gsm::GSM modem(Serial1);

void onReady() {
    Serial.println(F("Modem ready"));
}

void onInitCompleteEx(const gsm::InitResult& r) {
    Serial.println(r.message ? r.message : "?");
}

bool onSmsMessage(const gsm::SMSMessage& sms) {
    Serial.print(F("SMS from "));
    Serial.print(sms.sender);
    Serial.print(F(": "));
    Serial.println(sms.text);
    // здесь разбор команд: "RELAY ON", "STATUS" и т.д.
    return true;   // true → сообщение обработано (и удалится, если включён auto-delete)
}

void setup() {
    Serial.begin(115200);

#if defined(ESP32)
    Serial1.begin(9600, SERIAL_8N1, 16, 17);  // RX, TX
#else
    Serial1.begin(9600);
#endif

    gsm::ModemCallbacks cb = {};
    cb.onReady          = onReady;
    cb.onInitCompleteEx = onInitCompleteEx;
    cb.onSmsMessage     = onSmsMessage;
    modem.setCallbacks(cb);

    modem.begin();
    modem.init();                 // асинхронная инициализация
}

void loop() {
    modem.process();              // обязательно вызывать постоянно
}
```

<a id="usage"></a>

## Документация

<a id="gsm"></a>

### gsm::GSM — основной класс

```cpp
gsm::GSM modem(Serial1);          // любой Stream (HardwareSerial / SoftwareSerial)

void begin();                     // запуск ядра
void stop();                      // остановка
void process();                   // обрабатывать UART + очередь (вызывать в loop)

Result init(const RequestInfo& request = RequestInfo());
                                  // мягкая асинхронная инициализация модема

void setCallbacks(const ModemCallbacks& cb);

// ---- статус ----
bool isAvailable() const;         // модем хоть раз ответил на AT
bool isResponsive(uint32_t maxAgeMs = 30000) const;
bool isNetworkConnected() const;  // CREG = 1 или 5
bool isInitDone() const;          // полная успешная инициализация (SIM + SMS)
bool isSimReady() const;
bool isSmsReady() const;
bool isBusy() const;              // сейчас выполняется AT-команда
CoreState state() const;
uint8_t queuedCount() const;

const ModemError& lastError() const;
const char* lastErrorText() const;
const InitResult& lastInitResult() const;

// ---- сеть (кэш, без отправки AT) ----
int8_t signalQuality() const;     // 0…31, -1 = неизвестно
int8_t signalDbm() const;         // -113…-51, -1 = неизвестно
uint8_t registrationStatus() const;
const char* registrationStatusText() const;
bool isRegistered() const;
bool hasSignalData() const;
bool hasRegistrationData() const;
const char* operatorName() const;
bool hasOperatorData() const;
const char* simStatus() const;
const char* lastResponse() const;
uint32_t lastResponseTime() const;
uint32_t lastSignalUpdate() const;
uint32_t lastRegistrationUpdate() const;
uint32_t lastOperatorUpdate() const;
uint32_t lastSimUpdate() const;

// ---- периодический мониторинг ----
void startMonitoring();
void stopMonitoring();
bool isMonitoring() const;
void setPollIntervals(uint32_t simMs, uint32_t registrationMs,
                      uint32_t signalMs, uint32_t operatorMs);

// ---- одноразовые запросы ----
Result checkSim(const RequestInfo& = RequestInfo());
Result checkRegistration(const RequestInfo& = RequestInfo());
Result checkSignal(const RequestInfo& = RequestInfo());
Result checkOperator(const RequestInfo& = RequestInfo());

// ---- SMS ----
Result sendSMS(const char* number, const char* text,
               const RequestInfo& = RequestInfo());
void setSmsAutoDelete(bool enabled);
bool isSmsAutoDelete() const;
void setSmsScanInterval(uint32_t intervalMs);   // 0 = выкл
uint32_t smsScanInterval() const;
Result deleteSMS(const char* storage, uint8_t index);   // "SM" / "ME"
Result deleteAllSMS(const char* storage);
Result checkSmsMemory(const char* storage);

// ---- USSD ----
Result sendUSSD(const char* code, uint8_t dcs = 15,
                const RequestInfo& = RequestInfo());

// ---- звонки ----
Result answerCall(const RequestInfo& = RequestInfo());
Result hangUp(const RequestInfo& = RequestInfo());
Result dial(const char* number, const RequestInfo& = RequestInfo());
CallState callState() const;

// ---- recovery ----
void setRecoveryEnabled(bool enabled);
bool isRecoveryEnabled() const;
void setRecoveryTimeout(uint32_t intervalMs);
uint32_t recoveryTimeout() const;
void setMaxRecoveryAttempts(uint8_t attempts);
uint8_t recoveryAttempts() const;

// ---- низкий уровень ----
Result enqueue(const char* cmd,
               const RequestInfo& = RequestInfo(),
               uint16_t timeoutMs = 3000,
               CommandPriority priority = CommandPriority::P_NORMAL);
bool writeRaw(const char* data);
bool writeRaw(const uint8_t* data, size_t length);

// доступ к слоям
Core&    core();
SIM800L& driver();
Network& network();
```

> [!NOTE]
> `Result::OK` у `init()`, `sendSMS()`, `enqueue()` и т.д. означает только то, что команда **принята в локальную очередь**. Реальный результат приходит позже через колбэки.

<a id="callbacks"></a>

### Колбэки (ModemCallbacks)

```cpp
struct ModemCallbacks {
    void (*onReady)();
    void (*onInitComplete)(Result result);                 // legacy
    void (*onInitCompleteEx)(const InitResult& result);    // предпочтительно
    void (*onError)(Result result, const char* context);   // legacy
    void (*onModemError)(const ModemError& error);         // предпочтительно

    void (*onSimStatus)(const char* status);               // "READY", "SIM PIN"...
    void (*onRegistration)(uint8_t n, uint8_t stat);       // +CREG
    void (*onSignal)(uint8_t rssi, uint8_t ber);           // +CSQ
    void (*onOperator)(const char* name);                  // +COPS

    void (*onIncomingCall)(const char* number);            // +CLIP / RING
    void (*onCallAnswered)();
    void (*onDialStarted)(const char* number);
    void (*onCallEnded)();                                 // legacy
    void (*onCallState)(CallState state, const char* number);

    void (*onRecoveryRequired)(uint8_t attempt);

    void (*onSmsSent)(const char* number, Result result);
    void (*onSmsReceived)(int index);                      // legacy +CMTI
    bool (*onSmsMessage)(const SMSMessage& sms);           // true = обработано
    void (*onSmsMemory)(const SMSMemory& memory);
    void (*onSmsMemoryWarning)(const SMSMemory& memory);   // ≥ 80 %
    void (*onSmsMemoryFull)(const SMSMemory& memory);
    void (*onSmsError)(const char* storage, uint8_t index, Result result);

    void (*onUssdResponse)(uint8_t m, const char* str);    // +CUSD

    void (*onRawLine)(const char* line);                   // отладка
};
```

Все указатели можно оставлять `nullptr`. Неиспользуемые колбэки просто не вызываются.

<a id="sms"></a>

### SMS

#### Приём

Библиотека сама ловит `+CMTI`, ставит сообщение в очередь, читает через `AT+CMGR`, декодирует UCS2 HEX → UTF-8 и вызывает `onSmsMessage`.

```cpp
bool onSmsMessage(const gsm::SMSMessage& sms) {
    // sms.storage   — "SM" или "ME"
    // sms.index
    // sms.status    — "REC UNREAD" / "REC READ" ...
    // sms.sender    — номер отправителя
    // sms.timestamp — время от оператора
    // sms.text      — UTF-8 текст

    if (!auth.isAllowed(sms)) {
        return true;          // отклонить и (при auto-delete) удалить
    }

    // разбор команды
    if (strcmp(sms.text, "STATUS") == 0) {
        modem.sendSMS(sms.sender, "OK, all systems normal");
    }

    return true;              // true → успешно обработано
}
```

```cpp
modem.setSmsAutoDelete(true);     // удалять только если onSmsMessage вернул true
modem.setSmsScanInterval(30000);  // периодическое сканирование SM+ME (0 = выкл)
modem.deleteSMS("SM", 3);
modem.deleteAllSMS("ME");
modem.checkSmsMemory("SM");       // результат придёт в onSmsMemory
```

По умолчанию сообщения **не удаляются**. После успешного `onSmsMessage(true)` + включённом auto-delete — удаляются.

#### Отправка

```cpp
modem.sendSMS("+79091234567", "Привет! Команда выполнена");
```

- Текст принимается в UTF-8, отправляется как UCS2
- Лимит — **70 UTF-16 code units** (суррогатные пары считаются за 2)
- Multipart SMS **не поддерживается**
- Результат приходит в `onSmsSent`

<a id="calls"></a>

### Звонки

```cpp
void onIncomingCall(const char* number) {
    if (!auth.isAllowedCall(number)) {
        modem.hangUp();
        return;
    }
    // modem.answerCall();   // ответить
}

void onCallState(gsm::CallState state, const char* number) {
    // IDLE, RINGING, ANSWERING, ACTIVE, DIALING, ENDED, BUSY, FAILED
}

modem.dial("+79091234567");
modem.answerCall();
modem.hangUp();
CallState st = modem.callState();
```

<a id="ussd"></a>

### USSD

```cpp
void onUssdResponse(uint8_t status, const char* text) {
    // status — поле m из +CUSD
    // text   — UTF-8 (UCS2 автоматически декодируется)
    Serial.println(text);
}

modem.sendUSSD("*100#");        // DCS 15 (GSM 7-bit) по умолчанию
modem.sendUSSD("*100#", 72);    // если оператор требует UCS2
```

Интерактивные USSD-сессии не поддерживаются — только один запрос → один ответ.

<a id="network"></a>

### Сеть и диагностика

Мониторинг запускается автоматически после успешного ответа модема на AT. Можно управлять вручную:

```cpp
modem.startMonitoring();
modem.setPollIntervals(60000, 15000, 10000, 300000);
//                     SIM    CREG   CSQ    COPS
// 0 = отключить конкретный опрос

// Чтение кэша (без отправки AT):
int8_t  csq  = modem.signalQuality();      // 0…31
int8_t  dbm  = modem.signalDbm();
uint8_t reg  = modem.registrationStatus(); // 0…5
bool    ok   = modem.isRegistered();
const char* op = modem.operatorName();
const char* sim = modem.simStatus();
```

| CREG | Значение              |
|------|-----------------------|
| 0    | Не зарегистрирован    |
| 1    | Домашняя сеть         |
| 2    | Поиск сети            |
| 3    | Регистрация запрещена |
| 4    | Неизвестно            |
| 5    | Роуминг               |

<a id="auth"></a>

### Auth — whitelist номеров

Простой in-memory список. Сохранение в EEPROM / Preferences — ответственность приложения.

```cpp
gsm::Auth auth;

auth.add("+79091234567");
auth.add("89098887766");          // нормализуется в +7909…
auth.add("79161234567");
auth.setMode(gsm::AuthMode::WHITELIST);  // или DISABLE

if (auth.isAllowed(sms)) { /* … */ }
if (auth.isAllowedCall(number)) { /* … */ } else { modem.hangUp(); }

auth.remove("+79091234567");
auth.contains("+79091234567");
auth.clear();
auth.count();
auth.get(0);                      // указатель на нормализованный номер
```

- Максимум номеров: `AUTH_MAX_NUMBERS` (по умолчанию 16). Можно переопределить **до** `#include`:
  ```cpp
  #define AUTH_MAX_NUMBERS 32
  #include <GSM.h>
  ```
- Нормализация: оставляются только цифры и опциональный `+`. Российские 8XXXXXXXXXX и 7XXXXXXXXXX → `+7XXXXXXXXXX`.

<a id="recovery"></a>

### Recovery

Библиотека не управляет пинами RESET/PWRKEY. Она только периодически проверяет отзывчивость модема и сообщает приложению:

```cpp
modem.setRecoveryTimeout(60000);      // проверка раз в минуту
modem.setMaxRecoveryAttempts(3);
modem.setRecoveryEnabled(true);

void onRecoveryRequired(uint8_t attempt) {
    // attempt 1 → импульс RESET
    // attempt 2 → импульс PWRKEY
    // attempt 3 → полный power-cycle
}
```

После того как модем снова ответит на AT, драйвер автоматически запускает `init()` и восстанавливает настройки.

<a id="raw"></a>

### Низкоуровневые команды

```cpp
modem.enqueue("AT+CCLK?");                    // любая AT-команда
modem.enqueue("AT+CBC", RequestInfo(), 5000, gsm::CommandPriority::P_HIGH);

modem.writeRaw("текст SMS\x1A");              // после приглашения '>'
```

Для сложных последовательностей (несколько команд подряд) используйте транзакции через `core()`:

```cpp
if (modem.core().beginTransaction(42)) {
    modem.enqueue("AT+CMGF=1", RequestInfo(0, gsm::Source::INTERNAL, 42));
    // …
    modem.core().endTransaction(42);
}
```

<a id="types"></a>

### Типы и коды ошибок

```cpp
enum class Result : uint8_t {
    OK, ERROR, TIMEOUT, CANCELLED, BUSY, NOT_READY, QUEUE_FULL, INVALID_PARAM
};

enum class CallState : uint8_t {
    IDLE, RINGING, ANSWERING, ACTIVE, DIALING, ENDED, BUSY, FAILED
};

enum class AuthMode : uint8_t { DISABLE, WHITELIST };

enum class CommandPriority : uint8_t {
    P_LOW = 0, P_NORMAL = 1, P_HIGH = 2, P_CRITICAL = 3
};

struct SMSMessage {
    char storage[4];
    uint8_t index;
    char status[16];
    char sender[32];
    char timestamp[24];
    char text[241];           // UTF-8
};

struct SMSMemory {
    char storage[4];
    uint8_t used;
    uint8_t total;
    uint8_t free() const;
    uint8_t percent() const;
};

struct InitResult {
    Result result;
    bool available;
    bool simReady;
    bool smsReady;
    int16_t cme;
    const char* message;
};

struct ModemError {
    Result result;
    int16_t cme;              // -1 если нет
    int16_t cms;              // -1 если нет
    const char* context;
    const char* message;
};

// Русские описания
const char* errorToString(Result r);
const char* errorToString(const ModemError& e);
const char* resultToStr(Result r);            // короткие коды для логов
const char* registrationStatusToString(uint8_t);
const char* cmeToStr(int code);
const char* cmsToStr(int code);
```

<a id="semantics"></a>

## Важные семантические моменты

- `process()` нужно вызывать **постоянно** в `loop()`.
- В один момент выполняется максимум одна AT-команда. В очереди до 8 команд.
- Пока активна транзакция — выполняются только команды с тем же `transactionId`.
- URC (`RING`, `+CLIP`, `+CMTI`, `+CUSD`, `NO CARRIER`…) всегда доставляются в колбэки, даже во время ожидания ответа на команду.
- Парсер обрабатывает не более 64 байт UART за один вызов `process()`.
- Геттеры сети (`signalQuality()`, `operatorName()` и т.д.) **никогда** не отправляют AT — только читают кэш.

<a id="limits"></a>

## Ограничения

- Multipart SMS не реализован (исходящие UCS2 — максимум 70 code units)
- Интерактивные USSD-сессии не поддерживаются
- Нет GPRS / TCP / HTTP / MQTT (это GSM-транспорт для команд, а не data-модем)
- Нет DTMF-декодирования
- Аппаратные RESET/PWRKEY — полностью на стороне приложения

<a id="versions"></a>

## Версии

- **v0.4.0** — текущая
  - Core + SIM800L Driver + Network + Auth
  - Асинхронная очередь, SMS, звонки, USSD, recovery, мониторинг сети

<a id="install"></a>

## Установка

1. Скопировать папку `GSM` в каталог библиотек Arduino:
   - Windows: `Документы/Arduino/libraries/GSM`
   - macOS: `~/Documents/Arduino/libraries/GSM`
   - Linux: `~/Arduino/libraries/GSM`
2. Перезапустить Arduino IDE (или Reload Window в VS Code / Cursor)
3. Открыть **File → Examples → GSM → BasicInit**

Для VS Code / Cursor IntelliSense в комплекте есть готовый `.vscode/c_cpp_properties.json`.

### PlatformIO

```ini
lib_deps =
    https://github.com/TonTon-Macout/GSM.git
```

### Обновление

Рекомендуется **удалять** старую папку библиотеки целиком и класть новую. Простая замена файлов может оставить удалённые в новой версии исходники и привести к ошибкам компиляции.

<a id="feedback"></a>

## Баги и обратная связь

При нахождении багов создавайте **Issue**.

При сообщении о проблеме обязательно указывайте:

- Версию библиотеки
- Какой используется МК (ESP32, Mega, …)
- Версию SDK (для ESP)
- Версию Arduino IDE / PlatformIO
- Корректно ли работают встроенные примеры
- Минимальный код, в котором проявляется проблема
- Что ожидалось и что происходит на самом деле

---

Библиотека открыта для доработок и Pull Request'ов.


