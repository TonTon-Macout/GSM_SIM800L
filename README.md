# GSM — асинхронная библиотека для SIM800L

Неблокирующая библиотека для работы с GSM-модемами **SIM800L** и совместимыми модулями на Arduino-платформах.

Основной интерфейс — класс `gsm::GSM`. Работа с модемом выполняется асинхронно: библиотека принимает запросы, обрабатывает UART и вызывает пользовательские колбэки по мере получения ответов.

```cpp
#include <Arduino.h>
#include <GSM.h>

gsm::GSM modem(Serial1);
```

## Возможности

- Асинхронная работа без `delay()`, `String`, динамического выделения памяти и блокирующих циклов ожидания.
- Очередь AT-команд с приоритетами и эксклюзивными транзакциями.
- Приём SMS: уведомления `+CMTI`, чтение из памяти модема, декодирование UCS2 HEX в UTF-8.
- Отправка SMS на русском и английском языках.
- **Длинные SMS с автоматическим разбиением на части (Multipart SMS)** через PDU и UDH.
- Очередь исходящих SMS: до 5 сообщений, отправляемых последовательно.
- USSD-запросы и декодирование UCS2-ответов в UTF-8.
- Входящие и исходящие голосовые вызовы.
- Whitelist телефонных номеров с нормализацией российских номеров `8…` / `7…` в `+7…`.
- Мониторинг SIM-карты, регистрации в сети, уровня сигнала и оператора.
- Кэш диагностических данных без повторной отправки AT-команд.
- Контроль доступности модема и recovery-уведомления для аппаратного перезапуска.
- Подробные ошибки CME/CMS и готовые русские описания.
- Комментарии Doxygen к публичным методам для подсказок IntelliSense в VS Code.

## Совместимость

- Arduino C++11 и выше.
- ESP32, Arduino Mega / ATmega2560 и другие платы, предоставляющие объект `Stream`.
- UART и выбор пинов инициализируются в скетче.
- Для ESP32 можно использовать `HardwareSerial` с указанием RX/TX в `begin()`.

Библиотека не управляет питанием модема и физическими выводами `RESET` / `PWRKEY`: это остаётся задачей приложения.

## Зависимости

Внешние библиотеки не требуются.

## Содержание

- [Подключение](#подключение)
- [Быстрый старт](#быстрый-старт)
- [Основной класс gsm::GSM](#основной-класс-gsmgsm)
- [Колбэки](#колбэки)
- [SMS](#sms)
- [Звонки](#звонки)
- [USSD](#ussd)
- [Сеть и диагностика](#сеть-и-диагностика)
- [Auth — список разрешённых номеров](#auth--список-разрешённых-номеров)
- [Recovery](#recovery)
- [Низкоуровневые команды](#низкоуровневые-команды)
- [Типы и ошибки](#типы-и-ошибки)
- [Важные особенности работы](#важные-особенности-работы)
- [Установка](#установка)
- [Обратная связь](#обратная-связь)

---

## Подключение

Типовая схема подключения SIM800L:

| SIM800L | Микроконтроллер / питание |
|---|---|
| VCC | Стабильный источник примерно 4,0–4,2 В |
| GND | Общая земля |
| TXD | RX микроконтроллера |
| RXD | TX микроконтроллера; при необходимости согласовать уровни |
| RST | GPIO (необязательно, для аппаратного recovery) |
| PWRKEY | GPIO (необязательно, для управления питанием) |

> [!IMPORTANT]
> SIM800L чувствителен к питанию. Во время передачи возможны кратковременные токовые пики до 2 А. Используйте отдельный стабильный источник питания, рассчитанный на такие импульсы, и установите рядом с модулем низкоимпедансный конденсатор подходящей ёмкости (обычно 1000–2200 мкФ).

Библиотека не переключает GPIO и не выполняет аппаратный reset. При необходимости она вызывает `onRecoveryRequired(attempt)`, после чего приложение само выполняет нужную последовательность RESET/PWRKEY/power-cycle.

---

## Быстрый старт

```cpp
#include <Arduino.h>
#include <GSM.h>

gsm::GSM modem(Serial1);

void onReady() {
    Serial.println(F("Modem ready"));
}

void onInitCompleteEx(const gsm::InitResult& result) {
    Serial.print(F("Init: "));
    Serial.println(result.message ? result.message : "?");
}

bool onSmsMessage(const gsm::SMSMessage& sms) {
    Serial.print(F("SMS from "));
    Serial.print(sms.sender);
    Serial.print(F(": "));
    Serial.println(sms.text);

    // Здесь можно разобрать команду и выполнить действие.
    // true означает, что сообщение обработано.
    return true;
}

void onSmsSent(const char* number, const char* text, gsm::Result result) {
    Serial.print(F("SMS to "));
    Serial.print(number);
    Serial.print(F(": "));
    Serial.println(gsm::resultToStr(result));
}

void setup() {
    Serial.begin(115200);

#if defined(ESP32)
    Serial1.begin(9600, SERIAL_8N1, 16, 17); // RX, TX
#else
    Serial1.begin(9600);
#endif

    gsm::ModemCallbacks cb = {};
    cb.onReady = onReady;
    cb.onInitCompleteEx = onInitCompleteEx;
    cb.onSmsMessage = onSmsMessage;
    cb.onSmsSent = onSmsSent;
    modem.setCallbacks(cb);

    modem.begin();
    modem.init(); // Асинхронная инициализация.
}

void loop() {
    modem.process(); // Вызывать постоянно.
}
```

`modem.process()` обязательно должен регулярно вызываться из `loop()`. Не помещайте его в длинные блокирующие участки кода.

---

## Основной класс `gsm::GSM`

Создание объекта:

```cpp
gsm::GSM modem(Serial1);
```

### Запуск и обработка

```cpp
void begin();       // Запустить Core.
void stop();        // Остановить Core.
void process();     // Обработать UART, очередь команд и драйвер; вызывать постоянно.
Result init(const RequestInfo& request = RequestInfo());
                    // Запустить асинхронную инициализацию модема.
void setCallbacks(const ModemCallbacks& cb);
                    // Установить набор пользовательских колбэков.
```

### Состояние модема

```cpp
bool isAvailable() const;       // Модем хотя бы один раз ответил на AT.
bool isResponsive(uint32_t maxAgeMs = 30000) const;
bool isNetworkConnected() const;
bool isInitDone() const;        // Успешно завершены базовая настройка, SIM и SMS.
bool isSimReady() const;
bool isSmsReady() const;
bool isBusy() const;
CoreState state() const;
uint8_t queuedCount() const;

const ModemError& lastError() const;
const char* lastErrorText() const;
const InitResult& lastInitResult() const;
```

### SMS

```cpp
Result sendSMS(const char* number, const char* text,
               const RequestInfo& request = RequestInfo());

uint8_t smsSendQueueCount() const;
bool isSmsSendActive() const;
void clearSmsSendQueue();

void setSmsAutoDelete(bool enabled);
bool isSmsAutoDelete() const;
void setSmsScanInterval(uint32_t intervalMs); // 0 — отключить периодическое сканирование.
uint32_t smsScanInterval() const;

Result deleteSMS(const char* storage, uint8_t index);
Result deleteAllSMS(const char* storage);
Result checkSmsMemory(const char* storage);
```

### USSD и звонки

```cpp
Result sendUSSD(const char* code, uint8_t dcs = 15,
                const RequestInfo& request = RequestInfo());

Result answerCall(const RequestInfo& request = RequestInfo());
Result hangUp(const RequestInfo& request = RequestInfo());
Result dial(const char* number, const RequestInfo& request = RequestInfo());
CallState callState() const;
```

### Мониторинг сети

```cpp
void startMonitoring();
void stopMonitoring();
bool isMonitoring() const;

void setPollIntervals(uint32_t simMs,
                      uint32_t registrationMs,
                      uint32_t signalMs,
                      uint32_t operatorMs);

Result checkSim(const RequestInfo& request = RequestInfo());
Result checkRegistration(const RequestInfo& request = RequestInfo());
Result checkSignal(const RequestInfo& request = RequestInfo());
Result checkOperator(const RequestInfo& request = RequestInfo());
```

### Кэш диагностических данных

Эти методы возвращают последние полученные значения и сами не отправляют AT-команды:

```cpp
int8_t signalQuality() const;              // 0..31, -1 — неизвестно.
int8_t signalDbm() const;                  // ориентировочно -113..-51 dBm.
uint8_t registrationStatus() const;
const char* registrationStatusText() const;
bool isRegistered() const;                 // CREG = 1 или 5.
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
```

### Recovery

```cpp
void setRecoveryEnabled(bool enabled);
bool isRecoveryEnabled() const;
void setRecoveryTimeout(uint32_t intervalMs);
uint32_t recoveryTimeout() const;
void setMaxRecoveryAttempts(uint8_t attempts);
uint8_t recoveryAttempts() const;
void resetRecoveryAttempts();
```

### Низкоуровневый доступ

```cpp
Result enqueue(const char* cmd,
               const RequestInfo& request = RequestInfo(),
               uint16_t timeoutMs = 3000,
               CommandPriority priority = CommandPriority::P_NORMAL);

bool writeRaw(const char* data);
bool writeRaw(const uint8_t* data, size_t length);

Core& core();
SIM800L& driver();
Network& network();
```

`core()`, `driver()` и `network()` предоставляют доступ к нижележащим компонентам библиотеки. Для обычной работы предпочтительно использовать методы `gsm::GSM`.

> [!NOTE]
> `Result::OK` у `init()`, `sendSMS()`, `enqueue()` и других асинхронных методов означает, что запрос принят библиотекой (запущен или поставлен в очередь). Это не всегда означает, что модем уже выполнил операцию. Итоговый результат сообщается через соответствующий колбэк.

---

## Колбэки

Все поля `ModemCallbacks` можно оставить `nullptr`, если соответствующее событие не требуется.

```cpp
struct ModemCallbacks {
    void (*onReady)();

    void (*onInitComplete)(gsm::Result result); // Совместимый старый вариант.
    void (*onInitCompleteEx)(const gsm::InitResult& result);
    void (*onError)(gsm::Result result, const char* context);
    void (*onModemError)(const gsm::ModemError& error);

    void (*onSimStatus)(const char* status);
    void (*onRegistration)(uint8_t n, uint8_t stat);
    void (*onSignal)(uint8_t rssi, uint8_t ber);
    void (*onOperator)(const char* name);

    void (*onIncomingCall)(const char* number);
    void (*onCallAnswered)();
    void (*onDialStarted)(const char* number);
    void (*onCallEnded)();
    void (*onCallState)(gsm::CallState state, const char* number);

    void (*onRecoveryRequired)(uint8_t attempt);

    void (*onSmsSent)(const char* number, const char* text, gsm::Result result);
    void (*onSmsReceived)(int index); // Старый callback по событию +CMTI.
    bool (*onSmsMessage)(const gsm::SMSMessage& sms);
    void (*onSmsMemory)(const gsm::SMSMemory& memory);
    void (*onSmsMemoryWarning)(const gsm::SMSMemory& memory);
    void (*onSmsMemoryFull)(const gsm::SMSMemory& memory);
    void (*onSmsError)(const char* storage, uint8_t index, gsm::Result result);

    void (*onUssdResponse)(uint8_t status, const char* text,
                           const gsm::RequestInfo& request);

    void (*onRawLine)(const char* line);
};
```

Предпочтительно использовать `onInitCompleteEx` и `onModemError`: они предоставляют больше диагностической информации, чем legacy-варианты.

---

## SMS

### Приём SMS

Библиотека обрабатывает уведомления `+CMTI`, добавляет сообщение в очередь чтения, получает его через `AT+CMGR` и декодирует UCS2 HEX в UTF-8.

```cpp
bool onSmsMessage(const gsm::SMSMessage& sms) {
    // sms.storage   — память ("SM" или "ME")
    // sms.index     — индекс сообщения
    // sms.status    — статус сообщения
    // sms.sender    — номер отправителя
    // sms.timestamp — дата/время от оператора
    // sms.text      — текст в UTF-8

    Serial.println(sms.sender);
    Serial.println(sms.text);

    // true — сообщение успешно обработано.
    // При включённом auto-delete библиотека сможет удалить его.
    return true;
}
```

Настройка хранения:

```cpp
modem.setSmsAutoDelete(false); // По умолчанию сообщения сохраняются.
modem.setSmsAutoDelete(true);  // Удалять после успешной обработки.
modem.setSmsScanInterval(30000); // Периодически проверять SM и ME; 0 — выкл.

modem.deleteSMS("SM", 3);
modem.deleteAllSMS("ME");
modem.checkSmsMemory("SM");
```

По умолчанию автоматическое удаление отключено. Возврат `true` из `onSmsMessage()` означает, что приложение обработало SMS; фактическое удаление выполняется только при включённом `setSmsAutoDelete(true)`.

### Отправка коротких и длинных SMS

Отправка принимает обычную UTF-8 строку:

```cpp
modem.sendSMS("+79091234567", "Привет! Команда выполнена.");

modem.sendSMS("+79091234567",
              "Это длинное сообщение. Библиотека автоматически "
              "разделит его на несколько частей и отправит последовательно.");
```

Особенности реализации:

- Для исходящих SMS используется **PDU-режим**.
- Текст UTF-8 преобразуется в UCS2.
- Для длинных сообщений формируются сегменты с UDH (concatenated SMS).
- Телефон получателя обычно объединяет части в одно сообщение.
- Разбиение выполняется автоматически; вручную делить текст не нужно.
- Поддерживаются до **5 сегментов** в одном сообщении.
- Максимальный входной размер текста — **280 байт UTF-8**.
- При разбиении учитываются границы UTF-16 и суррогатные пары.
- После отправки драйвер возвращает модем в текстовый режим SMS (`AT+CMGF=1`).

Для UCS2 один обычный SMS-сегмент вмещает до 70 UTF-16 code units; в составном сообщении часть места занимает UDH, поэтому полезная нагрузка сегмента меньше. Количество сегментов зависит от кодировки и длины текста.

### Очередь исходящих SMS

Можно отправлять несколько сообщений подряд, не ожидая завершения предыдущего:

```cpp
modem.sendSMS("+79091234567", "Первое сообщение");
modem.sendSMS("+79091234567", "Второе сообщение");
modem.sendSMS("+79091234567", "Третье сообщение");

uint8_t waiting = modem.smsSendQueueCount();
bool sending = modem.isSmsSendActive();

// Очистить ещё не начатые сообщения очереди:
modem.clearSmsSendQueue();
```

- Очередь рассчитана на **5 исходящих сообщений**.
- Сообщения отправляются последовательно.
- Каждое длинное сообщение может состоять из нескольких сегментов.
- `onSmsSent(number, text, result)` вызывается **один раз на исходное сообщение**, а не на каждый сегмент.
- Callback вызывается после успешной отправки всех сегментов либо после первой ошибки.
- `clearSmsSendQueue()` очищает ожидающие сообщения; уже выполняемая отправка не является элементом ожидающей очереди.

Пример callback:

```cpp
void onSmsSent(const char* number, const char* text, gsm::Result result) {
    if (result == gsm::Result::OK) {
        Serial.println(F("SMS sent"));
    } else {
        Serial.print(F("SMS error: "));
        Serial.println(gsm::errorToString(result));
    }
}
```

`sendSMS()` возвращает `Result::OK`, если сообщение запущено или принято в очередь. Если очередь заполнена, возвращается `Result::QUEUE_FULL`. Некорректные или слишком большие входные данные приводят к `Result::INVALID_PARAM`.

---

## Звонки

```cpp
void onIncomingCall(const char* number) {
    Serial.print(F("Incoming call: "));
    Serial.println(number);

    // Например, проверить номер через auth.isAllowedCall(number).
    // modem.answerCall();
}

void onCallState(gsm::CallState state, const char* number) {
    // IDLE, RINGING, ANSWERING, ACTIVE,
    // DIALING, ENDED, BUSY, FAILED.
}

modem.dial("+79091234567");
modem.answerCall();
modem.hangUp();

gsm::CallState state = modem.callState();
```

События входящего вызова определяются по `RING` / `+CLIP`. Состояние соединения и завершения можно отслеживать через `onCallState`; `onCallEnded` оставлен для совместимости.

---

## USSD

```cpp
void onUssdResponse(uint8_t status, const char* text,
                    const gsm::RequestInfo& request) {
    Serial.print(F("USSD: "));
    Serial.println(text); // UTF-8
}

modem.sendUSSD("*100#");       // DCS 15 по умолчанию.
modem.sendUSSD("*100#", 72);   // DCS 72, если оператор требует UCS2.
```

UCS2-ответы автоматически декодируются в UTF-8. Поддерживается одноразовый запрос с ответом; интерактивные USSD-сессии не реализованы.

---

## Сеть и диагностика

Мониторинг может запускаться автоматически после успешного обмена с модемом во время инициализации. Им также можно управлять вручную:

```cpp
modem.startMonitoring();

modem.setPollIntervals(
    60000,   // SIM — 60 секунд
    15000,   // регистрация — 15 секунд
    10000,   // уровень сигнала — 10 секунд
    300000   // оператор — 5 минут
);

// 0 отключает соответствующий периодический опрос.
```

Получение кэшированных значений:

```cpp
int8_t csq = modem.signalQuality();          // 0..31, -1 — неизвестно.
int8_t dbm = modem.signalDbm();              // оценка уровня в dBm.
uint8_t reg = modem.registrationStatus();    // код +CREG.
bool registered = modem.isRegistered();      // true при статусе 1 или 5.

const char* operatorName = modem.operatorName();
const char* sim = modem.simStatus();
```

Значения `registrationStatus()` соответствуют статусу `+CREG`:

| Код | Значение |
|---:|---|
| 0 | Не зарегистрирован |
| 1 | Зарегистрирован в домашней сети |
| 2 | Выполняется поиск сети |
| 3 | Регистрация запрещена |
| 4 | Неизвестное состояние |
| 5 | Зарегистрирован в роуминге |

Геттеры не отправляют AT-команды: они возвращают последнее сохранённое значение. Для получения свежих данных используйте периодический мониторинг или методы `checkSim()`, `checkRegistration()`, `checkSignal()`, `checkOperator()`.

---

## Auth — список разрешённых номеров

`gsm::Auth` — фиксированный список номеров в оперативной памяти. Слоты не сдвигаются при удалении номера, поэтому индекс слота можно сохранять в EEPROM / Preferences или использовать в интерфейсе приложения.

```cpp
gsm::Auth auth;

auth.set(0, "+79091234567");       // Записать номер в слот 0.
auth.set(4, "89098887766");        // Записать в слот 4; остальные слоты не сдвигаются.
auth.add("79161234567");           // Добавить в первый свободный слот.

auth.removeAt(4);                  // Очистить слот 4.
auth.remove("+79091234567");       // Удалить номер по значению.

const char* number = auth.get(0);  // nullptr, если слот пуст или индекс неверен.
int8_t slot = auth.findIndex(number);

auth.setMode(gsm::AuthMode::WHITELIST);
// gsm::AuthMode::DISABLE — проверка доступа отключена.

if (auth.isAllowed(sms)) {
    // Отправитель SMS разрешён.
}

if (auth.isAllowedCall(number)) {
    // Номер звонящего разрешён.
}
```

Дополнительные методы:

```cpp
bool contains(const char* number) const;
uint8_t count() const;              // Количество занятых слотов.
uint8_t capacity() const;           // Полная ёмкость списка.
uint8_t freeSlots() const;
bool isFull() const;
bool isEmpty() const;
bool isSlotUsed(uint8_t slot) const;
int8_t firstFreeSlot() const;
bool clearSlot(uint8_t slot);
void clear();
gsm::AuthMode mode() const;
```

**Важно:** `count()` возвращает количество занятых ячеек, а не индекс последней занятой ячейки плюс один. Перебирать список нужно от `0` до `capacity() - 1`, пропуская `nullptr`:

```cpp
for (uint8_t i = 0; i < auth.capacity(); ++i) {
    const char* phone = auth.get(i);
    if (!phone) continue;

    Serial.println(phone);
}
```

По умолчанию доступно 16 слотов. Ёмкость можно изменить до подключения заголовка:

```cpp
#define AUTH_MAX_NUMBERS 32
#include <GSM.h>
```

Номера нормализуются при добавлении и сравнении. Российские номера `8XXXXXXXXXX` и `7XXXXXXXXXX` приводятся к формату `+7XXXXXXXXXX`. Список не сохраняется автоматически — сохранение и восстановление выполняет приложение.

Пример использования находится в `examples/AuthExample`.

---

## Recovery

Библиотека отслеживает доступность модема и может сообщить приложению, что требуется аппаратное восстановление. Физические линии RESET/PWRKEY и питание остаются под управлением приложения.

```cpp
modem.setRecoveryTimeout(60000); // Интервал проверки.
modem.setMaxRecoveryAttempts(3);
modem.setRecoveryEnabled(true);

void onRecoveryRequired(uint8_t attempt) {
    switch (attempt) {
        case 1:
            // Выполнить аппаратный RESET.
            break;
        case 2:
            // Попробовать управление PWRKEY.
            break;
        case 3:
            // Выполнить полный power-cycle.
            break;
    }
}
```

После того как модем снова начинает отвечать на AT, драйвер автоматически запускает `init()` для восстановления конфигурации.

---

## Низкоуровневые команды

Для одиночных AT-команд можно использовать `enqueue()`:

```cpp
modem.enqueue("AT+CCLK?");
modem.enqueue("AT+CBC",
              gsm::RequestInfo(),
              5000,
              gsm::CommandPriority::P_HIGH);
```

Для передачи произвольных данных предусмотрен `writeRaw()`:

```cpp
modem.writeRaw("текст");
modem.writeRaw(buffer, length);
```

`writeRaw()` не заменяет механизм команд и транзакций. Используйте его только тогда, когда понимаете текущий протокол обмена и состояние модема.

Для последовательностей команд, которые нельзя перемежать с другими запросами, Core предоставляет эксклюзивные транзакции:

```cpp
const uint16_t transactionId = 42;

if (modem.core().beginTransaction(transactionId)) {
    modem.enqueue(
        "AT+COMMAND",
        gsm::RequestInfo(0, gsm::Source::INTERNAL, transactionId)
    );

    // Добавьте остальные команды той же транзакции.
    modem.core().endTransaction(transactionId);
}
```

---

## Типы и ошибки

### `gsm::Result`

```cpp
enum class Result : uint8_t {
    OK,
    ERROR,
    TIMEOUT,
    CANCELLED,
    BUSY,
    NOT_READY,
    QUEUE_FULL,
    INVALID_PARAM
};
```

Для вывода ошибки используйте:

```cpp
gsm::errorToString(result); // Развёрнутое русское описание.
gsm::resultToStr(result);   // Короткий код для логов.
```

### Другие основные типы

- `gsm::RequestInfo` — идентификатор запроса, источник и ID транзакции.
- `gsm::Source` — источник операции (`INTERNAL`, `PERIODIC`, `WEB`, `LCD`, `BUTTON`, `SMS`, `CALL`, `EXTERNALE`).
- `gsm::CommandPriority` — приоритет AT-команды: `P_LOW`, `P_NORMAL`, `P_HIGH`, `P_CRITICAL`.
- `gsm::CoreState` — состояние ядра: `STOPPED`, `IDLE`, `WAITING_RESPONSE`.
- `gsm::CallState` — состояние голосового вызова.
- `gsm::SMSMessage` — принятое SMS с текстом UTF-8.
- `gsm::SMSMemory` — сведения о занятой и общей памяти SMS.
- `gsm::InitResult` — результат полной инициализации.
- `gsm::ModemError` — ошибка с кодами CME/CMS, контекстом и описанием.

Для диагностики кодов модема доступны `gsm::cmeToStr(code)` и `gsm::cmsToStr(code)`.

---

## Важные особенности работы

- `modem.process()` необходимо вызывать постоянно.
- В каждый момент времени выполняется не более одной AT-команды.
- Внутренняя очередь Core рассчитана на 8 команд.
- Команды имеют приоритеты; при одинаковом приоритете сохраняется FIFO-порядок.
- Эксклюзивная транзакция не позволяет выполнять посторонние команды внутри защищённой последовательности.
- Незапрошенные события модема (URC), например `RING`, `+CLIP`, `+CMTI`, `+CUSD`, `NO CARRIER`, направляются в соответствующие обработчики.
- За один вызов `process()` парсер обрабатывает ограниченный объём входящих UART-данных.
- Сетевые геттеры читают кэш и не отправляют AT-команды.
- Возврат `Result::OK` означает принятие асинхронного запроса, а не обязательно окончательное выполнение.
- Для исходящих SMS используется отдельная очередь на 5 сообщений; Multipart-сегменты одного SMS отправляются последовательно.

---

## Текущие ограничения

- Максимальный размер исходящего текста одного SMS-запроса — 280 байт UTF-8.
- Одно длинное SMS может быть разделено максимум на 5 сегментов.
- Размер текста приёма SMS ограничен буфером `SMSMessage::text` (240 байт UTF-8 плюс завершающий ноль).
- Поддерживаются одноразовые USSD-запросы; интерактивные USSD-сессии не реализованы.
- GPRS / TCP / HTTP / MQTT не входят в задачи этой библиотеки.
- DTMF-декодирование не реализовано.
- Аппаратное управление RESET/PWRKEY и питанием выполняет приложение.
- Реальная доставка SMS зависит от сети оператора, SIM-карты и возможностей модема; успешный ответ модема подтверждает отправку в сеть, но не гарантирует прочтение получателем.

---

## Установка

### Arduino IDE

1. Скопируйте папку `GSM` в каталог библиотек Arduino:
   - Windows: `Документы/Arduino/libraries/GSM`
   - macOS: `~/Documents/Arduino/libraries/GSM`
   - Linux: `~/Arduino/libraries/GSM`
2. Перезапустите Arduino IDE.
3. Откройте **File → Examples → GSM → BasicInit**.

### PlatformIO

Добавьте в `platformio.ini`:

```ini
lib_deps =
    https://github.com/TonTon-Macout/GSM.git
```

### VS Code / Cursor и IntelliSense

В комплект входит `.vscode/c_cpp_properties.json`. Для корректных подсказок откройте папку `GSM` как workspace либо добавьте `GSM/src` в `includePath`.

Публичные методы снабжены комментариями Doxygen, поэтому IntelliSense может показывать описание методов и их параметров при наведении и автодополнении.

### Обновление

Перед установкой новой версии рекомендуется полностью удалить старую папку `GSM`, а затем скопировать новую. Простая замена файлов может оставить устаревшие исходники и привести к конфликтам компиляции.

---

## Примеры

В комплект входят:

- `BasicInit` — запуск и инициализация модема.
- `SMS_Calls` — работа с SMS и голосовыми вызовами.
- `USSD` — отправка USSD-запроса и обработка ответа.
- `AuthExample` — использование списка разрешённых номеров.

---

## Обратная связь

При обнаружении ошибки создайте Issue и приложите:

- версию библиотеки;
- используемую плату и микроконтроллер;
- версию Arduino Core / SDK;
- версию Arduino IDE или PlatformIO;
- пример кода, воспроизводящий проблему;
- фрагмент UART-лога;
- ожидаемое и фактическое поведение.

Библиотека открыта для предложений, доработок и Pull Request.
