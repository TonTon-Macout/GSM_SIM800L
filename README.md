# GSM — асинхронная библиотека для SIM800L

>[!TIP]
>
> Это Вайб-Библиотека, собственно как и документация ниже.
> Сделана для упаравления проектом через жсм модем.
> Асинхронно, с балансом, сохранением номеров, очередью ат-команд и изоляцией последовательностей ат-команд 
> Декодирует русский текст, разделяет длинные сообщения, автоматический мониторинг состояния сети , чтение сообщений из памяти и из сим и много всего удобного но которое было лень писать самому. 
 
Неблокирующая библиотека для работы с GSM-модемами **SIM800L** и совместимыми модулями на Arduino / ESP32.

Основной интерфейс — `gsm::GSM`. Библиотека работает через `Stream`, обрабатывает UART и AT-команды в `process()` и сообщает о результатах через колбэки.

## Возможности

* Асинхронная работа без `delay()`, `String` и динамического выделения памяти.
* Очередь AT-команд с приоритетами и эксклюзивными транзакциями.
* Приём SMS с декодированием UCS2 HEX → UTF-8.
* Отправка SMS на русском и английском языках.
* Длинные исходящие SMS: автоматическое разбиение на Multipart SMS с PDU/UDH.
* Очередь исходящих SMS.
* USSD-запросы и декодирование UCS2-ответов.
* Входящие и исходящие голосовые вызовы.
* Whitelist телефонных номеров с фиксированными слотами.
* Мониторинг SIM, регистрации в сети, уровня сигнала и оператора.
* Кэш диагностических данных.
* Контроль доступности модема и callback для аппаратного recovery.
* Подробные ошибки CME/CMS и русские описания.
* Комментарии Doxygen к публичному API для IntelliSense в VS Code.

## Совместимость

* Arduino C++11 и выше.
* ESP32, ESP8266, Arduino Mega / ATmega2560 и другие платформы с `Stream`.
* UART и назначение пинов настраиваются в скетче.
* Внешние библиотеки не требуются.

## Содержание

* [Подключение](#подключение)
* [Быстрый старт](#быстрый-старт)
* [Класс gsm::GSM](#класс-gsmgsm)
* [Состояние и диагностика](#состояние-и-диагностика)
* [Мониторинг сети](#мониторинг-сети)
* [SMS](#sms)
* [Звонки](#звонки)
* [USSD](#ussd)
* [Колбэки](#колбэки)
* [Auth — список разрешённых номеров](#auth--список-разрешённых-номеров)
* [Recovery](#recovery)
* [Низкоуровневые команды](#низкоуровневые-команды)
* [Типы и ошибки](#типы-и-ошибки)
* [Важные особенности](#важные-особенности)
* [Установка](#установка)

\---

## Подключение

|SIM800L|Подключение|
|-|-|
|VCC|Стабильный источник питания примерно 4,0–4,2 В|
|GND|Общая земля с микроконтроллером|
|TXD|RX микроконтроллера|
|RXD|TX микроконтроллера; при необходимости согласовать логические уровни|
|RST|GPIO микроконтроллера, необязательно|
|PWRKEY|GPIO микроконтроллера, необязательно|

> \[!IMPORTANT]
> SIM800L чувствителен к питанию. Во время передачи возникают кратковременные токовые пики, которые могут достигать 2 А. Нужен отдельный стабильный источник питания, рассчитанный на такие импульсы. Разместите рядом с модулем низкоимпедансный конденсатор подходящей ёмкости (часто используют 1000–2200 мкФ).

Библиотека не управляет выводами `RESET` и `PWRKEY` и не переключает питание модема. Она только сообщает приложению о необходимости аппаратного восстановления через `onRecoveryRequired()`.

\---

## Быстрый старт

```cpp
#include <Arduino.h>
#include <GSM.h>

gsm::GSM modem(Serial1);

void onReady() {
    Serial.println(F("Modem ready"));
}

void onInitComplete(const gsm::InitResult\& result) {
    Serial.print(F("Init: "));
    Serial.println(result.message ? result.message : "?");
}

bool onSmsMessage(const gsm::SMSMessage\& sms) {
    Serial.print(F("SMS from "));
    Serial.print(sms.sender);
    Serial.print(F(": "));
    Serial.println(sms.text);

    // Разбор команды приложения.
    return true; // Сообщение обработано.
}

void onSmsSent(const char\* number, const char\* text, gsm::Result result) {
    Serial.print(F("SMS result: "));
    Serial.println(gsm::resultToStr(result));
}

void setup() {
    Serial.begin(115200);

#if defined(ESP32)
    Serial1.begin(9600, SERIAL\_8N1, 16, 17); // RX, TX
#else
    Serial1.begin(9600);
#endif

    gsm::ModemCallbacks cb = {};
    cb.onReady = onReady;
    cb.onInitCompleteEx = onInitComplete;
    cb.onSmsMessage = onSmsMessage;
    cb.onSmsSent = onSmsSent;

    modem.setCallbacks(cb);
    modem.begin();
    modem.init();
}

void loop() {
    modem.process(); // Вызывать постоянно.
}
```

`process()` обязательно вызывать регулярно. Не допускайте длительных блокирующих участков кода, иначе обработка UART и AT-команд будет задерживаться.

\---

## Класс `gsm::GSM`

Создание объекта:

```cpp
gsm::GSM modem(Serial1);
```

### Конструктор

```cpp
explicit GSM(Stream\& serial);
```

Создаёт объект библиотеки и связывает его с UART-потоком. Передаваемый объект `Stream` должен быть инициализирован приложением.

### Запуск и обработка

```cpp
void begin();
```

Запускает внутреннее ядро очереди AT-команд. Вызывается один раз при старте приложения перед работой с модемом.

```cpp
void stop();
```

Останавливает внутреннее ядро. После остановки обработка команд прекращается до следующего `begin()`.

```cpp
void process();
```

Обрабатывает входящие данные UART, очередь AT-команд, события модема и внутренние автоматы драйвера. Вызывать постоянно из `loop()`.

```cpp
void setCallbacks(const ModemCallbacks\& cb);
```

Устанавливает набор callback-функций приложения. Неиспользуемые поля структуры можно оставить `nullptr`.

```cpp
Result init(const RequestInfo\& request = RequestInfo());
```

Запускает асинхронную последовательность инициализации модема. Сам метод не ждёт завершения; итог приходит через `onInitComplete` или `onInitCompleteEx`.

### Полный список методов класса

Ниже приведены все публичные методы фасадного класса `gsm::GSM`. 

```cpp
class GSM {
public:
    // Создать объект и привязать его к UART/Stream, который обслуживает приложение.
    explicit GSM(Stream\& serial);

    // Запустить внутреннее ядро обработки AT-команд.
    void begin();

    // Остановить внутреннее ядро обработки AT-команд.
    void stop();

    // Обработать UART, очередь команд и автоматы драйвера.
    // Вызывать регулярно из loop(), не использовать вместо блокирующего ожидания.
    void process();

    // Запустить периодический опрос SIM, регистрации, сигнала и оператора.
    void startMonitoring();

    // Остановить периодический опрос сетевых параметров.
    void stopMonitoring();

    // Проверить, включён ли периодический мониторинг сети.
    bool isMonitoring() const;

    // Задать интервалы опроса SIM, регистрации, уровня сигнала и оператора (мс).
    // Нулевой интервал отключает соответствующий периодический запрос.
    void setPollIntervals(uint32\_t simMs, uint32\_t registrationMs,
                          uint32\_t signalMs, uint32\_t operatorMs);

    // Установить callback-функции, через которые библиотека сообщает о событиях.
    void setCallbacks(const ModemCallbacks\& cb);

    // Запустить асинхронную инициализацию модема.
    // Результат выполнения будет сообщён через callback и lastInitResult().
    Result init(const RequestInfo\& request = RequestInfo());

    // Вернуть true, если модем хотя бы один раз успешно ответил на AT-команду.
    // Это подтверждает связь с модемом, но не означает регистрацию в сети.
    bool isAvailable() const;

    // Проверить, был ли успешный ответ модема за последние maxAgeMs миллисекунд.
    // По умолчанию используется окно 30000 мс.
    bool isResponsive(uint32\_t maxAgeMs = 30000) const;

    // Вернуть true, если модем зарегистрирован в сети (CREG status 1 или 5).
    bool isNetworkConnected() const;

    // Вернуть true, если полностью завершилась последовательность init,
    // включая готовность SIM и настройку SMS.
    bool isInitDone() const;

    // Вернуть true, если SIM-карта отвечает READY и готова к работе.
    bool isSimReady() const;

    // Вернуть true, если подсистема SMS успешно настроена при инициализации.
    bool isSmsReady() const;

    // Вернуть true, если Core сейчас ожидает ответ на выполняемую AT-команду.
    bool isBusy() const;

    // Получить текущее состояние Core: STOPPED, IDLE или WAITING\_RESPONSE.
    CoreState state() const;

    // Получить количество команд, ожидающих выполнения во внутренней очереди Core.
    // Не включает команду, которая уже выполняется.
    uint8\_t queuedCount() const;

    // Получить структуру с последней подробной ошибкой модема/операции.
    const ModemError\& lastError() const;

    // Получить текстовое описание последней ошибки.
    const char\* lastErrorText() const;

    // Получить структуру с результатом последней последовательности инициализации.
    const InitResult\& lastInitResult() const;

    // Получить последнее закэшированное качество сигнала CSQ (0..31).
    // Значение -1 означает, что данные ещё не получены.
    int8\_t signalQuality() const;

    // Получить оценку уровня сигнала в dBm.
    // Значение -1 означает, что данные ещё не получены.
    int8\_t signalDbm() const;

    // Получить последний статус регистрации +CREG.
    // Значения 1 и 5 означают регистрацию; 4 используется как неизвестное состояние.
    uint8\_t registrationStatus() const;

    // Получить текстовое описание последнего статуса +CREG.
    const char\* registrationStatusText() const;

    // Проверить регистрацию в домашней сети или роуминге (CREG 1 или 5).
    bool isRegistered() const;

    // Проверить, получены ли валидные данные об уровне сигнала.
    bool hasSignalData() const;

    // Проверить, получены ли валидные данные о регистрации в сети.
    bool hasRegistrationData() const;

    // Получить имя оператора из последнего успешного запроса +COPS.
    const char\* operatorName() const;

    // Проверить, получено ли валидное имя оператора.
    bool hasOperatorData() const;

    // Получить последний известный статус SIM, например READY или SIM PIN.
    const char\* simStatus() const;

    // Получить строку последнего ответа модема, сохранённую для диагностики.
    const char\* lastResponse() const;

    // Получить millis()-метку времени последнего сохранённого ответа модема.
    uint32\_t lastResponseTime() const;

    // Получить millis()-метку последнего обновления уровня сигнала.
    uint32\_t lastSignalUpdate() const;

    // Получить millis()-метку последнего обновления статуса регистрации.
    uint32\_t lastRegistrationUpdate() const;

    // Получить millis()-метку последнего обновления имени оператора.
    uint32\_t lastOperatorUpdate() const;

    // Получить millis()-метку последнего обновления статуса SIM.
    uint32\_t lastSimUpdate() const;

    // Запросить статус SIM-карты. Результат поступит асинхронно.
    Result checkSim(const RequestInfo\& request = RequestInfo());

    // Запросить статус регистрации в сети (+CREG). Результат поступит асинхронно.
    Result checkRegistration(const RequestInfo\& request = RequestInfo());

    // Запросить уровень сигнала (+CSQ). Результат поступит асинхронно.
    Result checkSignal(const RequestInfo\& request = RequestInfo());

    // Запросить имя оператора (+COPS). Результат поступит асинхронно.
    Result checkOperator(const RequestInfo\& request = RequestInfo());

    // Поставить произвольную AT-команду в очередь Core.
    // timeoutMs задаёт время ожидания ответа, priority — приоритет команды.
    Result enqueue(const char\* cmd,
                   const RequestInfo\& request = RequestInfo(),
                   uint16\_t timeoutMs = 3000,
                   CommandPriority priority = CommandPriority::P\_NORMAL);

    // Передать строковые данные непосредственно в UART модема.
    // Использовать только в тех протокольных состояниях, где это требуется.
    bool writeRaw(const char\* data);

    // Передать массив байт непосредственно в UART модема.
    bool writeRaw(const uint8\_t\* data, size\_t length);

    // Разрешить или запретить автоматическое удаление принятых SMS
    // после успешной обработки callback onSmsMessage().
    void setSmsAutoDelete(bool enabled);

    // Проверить, включено ли автоматическое удаление принятых SMS.
    bool isSmsAutoDelete() const;

    // Установить интервал периодического сканирования памяти SMS в миллисекундах.
    // 0 отключает периодическое сканирование.
    void setSmsScanInterval(uint32\_t intervalMs);

    // Получить текущий интервал периодического сканирования SMS.
    uint32\_t smsScanInterval() const;

    // Удалить SMS с указанным индексом из памяти "SM" или "ME".
    Result deleteSMS(const char\* storage, uint8\_t index);

    // Удалить все SMS из выбранной памяти модема ("SM" или "ME").
    Result deleteAllSMS(const char\* storage);

    // Запросить заполненность памяти SMS ("SM" или "ME").
    // Результат передаётся через onSmsMemory/onSmsMemoryWarning/onSmsMemoryFull.
    Result checkSmsMemory(const char\* storage);

    // Отправить USSD-код. dcs=15 обычно соответствует GSM 7-bit,
    // dcs=72 может использоваться оператором для UCS2.
    // Ответ поступает через onUssdResponse().
    Result sendUSSD(const char\* code, uint8\_t dcs = 15,
                    const RequestInfo\& request = RequestInfo());

    // Отправить SMS. Текст передаётся в UTF-8; длинное сообщение
    // автоматически разбивается на Multipart SMS.
    // onSmsSent вызывается один раз после завершения всего сообщения.
    Result sendSMS(const char\* number, const char\* text,
                   const RequestInfo\& request = RequestInfo());

    // Получить количество сообщений, ожидающих отправки в очереди SMS.
    uint8\_t smsSendQueueCount() const;

    // Проверить, выполняется ли сейчас отправка SMS (включая части Multipart SMS).
    bool isSmsSendActive() const;

    // Очистить очередь ещё не начатых исходящих SMS.
    // Текущая отправка при этом не прерывается.
    void clearSmsSendQueue();

    // Ответить на входящий голосовой вызов.
    Result answerCall(const RequestInfo\& request = RequestInfo());

    // Завершить текущий голосовой вызов.
    Result hangUp(const RequestInfo\& request = RequestInfo());

    // Инициировать исходящий вызов на указанный номер.
    Result dial(const char\* number, const RequestInfo\& request = RequestInfo());

    // Включить или отключить контроль доступности модема.
    void setRecoveryEnabled(bool enabled);

    // Проверить, включён ли механизм контроля доступности/recovery.
    bool isRecoveryEnabled() const;

    // Задать интервал проверки доступности модема в миллисекундах.
    void setRecoveryTimeout(uint32\_t intervalMs);

    // Получить текущий интервал проверки доступности модема.
    uint32\_t recoveryTimeout() const;

    // Задать максимальное количество попыток recovery.
    // Передача 0 трактуется как 1 попытка.
    void setMaxRecoveryAttempts(uint8\_t attempts);

    // Получить количество уже выполненных/зарегистрированных попыток recovery.
    uint8\_t recoveryAttempts() const;

    // Сбросить счётчик попыток recovery.
    void resetRecoveryAttempts();

    // Получить текущее состояние голосового вызова.
    CallState callState() const;

    // Получить доступ к внутреннему Core для расширенных сценариев и транзакций.
    Core\& core();

    // Получить доступ к драйверу SIM800L для функций, не вынесенных в фасад GSM.
    SIM800L\& driver();

    // Получить объект слоя сетевого мониторинга.
    Network\& network();
};
```

`Result::OK` у асинхронных методов означает, что запрос принят библиотекой/поставлен в очередь. Окончательный результат операции нужно получать через callback или соответствующий getter.

\---

## Состояние и диагностика

Краткая расшифровка основных методов:

|Метод|Что показывает|
|-|-|
|`isAvailable()`|Модем хотя бы один раз ответил на AT|
|`isResponsive(maxAgeMs)`|Был успешный ответ за заданный интервал|
|`isNetworkConnected()`|Есть регистрация в сети: CREG 1 или 5|
|`isInitDone()`|Полная последовательность инициализации завершилась успешно|
|`isSimReady()`|SIM находится в состоянии READY|
|`isSmsReady()`|Настройка SMS при инициализации прошла успешно|
|`isBusy()`|Core ожидает ответ на текущую AT-команду|
|`state()`|Состояние внутреннего Core|
|`queuedCount()`|Число ожидающих AT-команд|
|`lastError()`|Структура последней ошибки|
|`lastErrorText()`|Текст последней ошибки|
|`lastInitResult()`|Результат последней инициализации|

Важно различать `isAvailable()` и `isNetworkConnected()`: первый метод проверяет связь микроконтроллера с модемом по AT, второй — регистрацию модема в сотовой сети.

Все сетевые getter-методы возвращают кэш. Они не отправляют AT-команды и не запускают новый запрос.

\---

## Мониторинг сети

Периодический мониторинг:

```cpp
modem.setPollIntervals(
    60000,   // SIM — раз в 60 секунд
    15000,   // регистрация — раз в 15 секунд
    10000,   // сигнал — раз в 10 секунд
    300000   // оператор — раз в 5 минут
);

modem.startMonitoring();
```

Нулевой интервал отключает соответствующий опрос. Остановить мониторинг:

```cpp
modem.stopMonitoring();
```

Одноразовые запросы:

```cpp
modem.checkSim();
modem.checkRegistration();
modem.checkSignal();
modem.checkOperator();
```

Эти методы асинхронны. Новые значения обновляются в кэше и передаются через соответствующие callback-функции.

Статусы `+CREG`:

|Код|Значение|
|-:|-|
|0|Не зарегистрирован|
|1|Зарегистрирован в домашней сети|
|2|Выполняется поиск сети|
|3|Регистрация запрещена|
|4|Неизвестное состояние|
|5|Зарегистрирован в роуминге|

\---

## SMS

### Приём SMS

При появлении `+CMTI` библиотека ставит сообщение в очередь чтения, читает его из памяти модема и декодирует UCS2 HEX в UTF-8.

Структура `SMSMessage` содержит:

* `storage` — память, например `SM` или `ME`;
* `index` — индекс SMS в памяти;
* `status` — статус сообщения;
* `sender` — номер отправителя;
* `timestamp` — дата и время от оператора;
* `text` — текст SMS в UTF-8, буфер 241 байт включая завершающий ноль.

Пример:

```cpp
bool onSmsMessage(const gsm::SMSMessage\& sms) {
    Serial.println(sms.sender);
    Serial.println(sms.text);

    // true — приложение обработало SMS.
    return true;
}
```

Автоматическое удаление по умолчанию отключено:

```cpp
modem.setSmsAutoDelete(false); // Оставлять принятые SMS в памяти.
modem.setSmsAutoDelete(true);  // Удалять после успешной обработки.
```

При включённом auto-delete сообщение удаляется после возврата `true` из `onSmsMessage()`.

Периодическое сканирование памяти:

```cpp
modem.setSmsScanInterval(30000); // Проверять память каждые 30 секунд.
modem.setSmsScanInterval(0);     // Отключить периодическое сканирование.
```

Операции с памятью:

```cpp
modem.deleteSMS("SM", 3);       // Удалить SMS с индексом 3 из SM.
modem.deleteAllSMS("ME");       // Удалить все SMS из ME.
modem.checkSmsMemory("SM");     // Запросить заполненность SM.
```

### Отправка коротких и длинных SMS

```cpp
modem.sendSMS("+79091234567", "Привет! Команда выполнена.");

modem.sendSMS("+79091234567",
              "Длинный текст на русском языке. "
              "Библиотека сама разделит его на несколько частей, "
              "отправит последовательно и сформирует UDH.");
```

Особенности отправки:

* Входной текст — UTF-8.
* Отправка выполняется в PDU-режиме с кодировкой UCS2.
* Для длинного сообщения автоматически формируются части Multipart SMS с UDH.
* Получатель обычно видит составное сообщение как одно SMS.
* Максимальный входной размер — 280 байт UTF-8.
* Поддерживается до 5 сегментов на одно сообщение.
* Разбиение выполняется с учётом UTF-16 и суррогатных пар.
* Callback `onSmsSent` вызывается один раз на исходное сообщение, после завершения всех частей или при первой ошибке.

Для UCS2 обычный сегмент вмещает до 70 UTF-16 code units. В Multipart SMS часть места занимает UDH, поэтому полезная нагрузка каждой части меньше.

### Очередь исходящих SMS

```cpp
modem.sendSMS("+79091234567", "Первое сообщение");
modem.sendSMS("+79091234567", "Второе сообщение");

uint8\_t waiting = modem.smsSendQueueCount();
bool active = modem.isSmsSendActive();

modem.clearSmsSendQueue(); // Удалить ожидающие сообщения.
```

Очередь рассчитана на 5 исходящих сообщений. Сообщения отправляются последовательно. `clearSmsSendQueue()` очищает ожидающие сообщения, но не прерывает уже выполняемую отправку.

Callback:

```cpp
void onSmsSent(const char\* number,
               const char\* text,
               gsm::Result result) {
    if (result == gsm::Result::OK) {
        Serial.println(F("SMS sent"));
    } else {
        Serial.println(gsm::errorToString(result));
    }
}
```

`sendSMS()` возвращает результат принятия запроса. Это не подтверждение доставки SMS абоненту.

\---

## Звонки

```cpp
void onIncomingCall(const char\* number) {
    Serial.print(F("Incoming call: "));
    Serial.println(number);

    // Проверить номер и при необходимости:
    // modem.answerCall();
    // modem.hangUp();
}

void onCallState(gsm::CallState state, const char\* number) {
    // IDLE, RINGING, ANSWERING, ACTIVE,
    // DIALING, ENDED, BUSY, FAILED.
}

modem.dial("+79091234567"); // Исходящий вызов.
modem.answerCall();         // Ответить.
modem.hangUp();             // Завершить вызов.

gsm::CallState state = modem.callState();
```

События вызова также доступны через `onIncomingCall`, `onCallAnswered`, `onDialStarted`, `onCallEnded` и `onCallState`.

\---

## USSD

```cpp
void onUssdResponse(uint8\_t m,
                    const char\* text,
                    const gsm::RequestInfo\& request) {
    Serial.println(text); // Текст ответа UTF-8.
}

modem.sendUSSD("\*100#");      // DCS 15 по умолчанию.
modem.sendUSSD("\*100#", 72);  // DCS 72, если оператор требует UCS2.
```

UCS2-ответы автоматически декодируются в UTF-8. Поддерживается одноразовый запрос и ответ; интерактивные USSD-сессии не реализованы.

\---

## Колбэки

Все callback-поля можно оставить `nullptr`. Вызываются только установленные обработчики.

```cpp
struct ModemCallbacks {
    // Модем стал доступен и ответил на AT.
    void (\*onReady)();

    // Устаревший callback результата init; предпочтительнее onInitCompleteEx.
    void (\*onInitComplete)(gsm::Result result);

    // Полный результат инициализации: доступность модема, SIM, SMS и ошибка.
    void (\*onInitCompleteEx)(const gsm::InitResult\& result);

    // Устаревший callback ошибки с результатом и контекстом.
    void (\*onError)(gsm::Result result, const char\* context);

    // Подробная ошибка: Result, CME/CMS, контекст и текстовое описание.
    void (\*onModemError)(const gsm::ModemError\& error);

    // Изменение/получение статуса SIM, например READY или SIM PIN.
    void (\*onSimStatus)(const char\* status);

    // Ответ о регистрации в сети: n — режим отчёта, stat — статус +CREG.
    void (\*onRegistration)(uint8\_t n, uint8\_t stat);

    // Результат чтения качества сигнала +CSQ: rssi и ber.
    void (\*onSignal)(uint8\_t rssi, uint8\_t ber);

    // Имя оператора, полученное из +COPS.
    void (\*onOperator)(const char\* name);

    // Обнаружен входящий вызов; number — номер звонящего, если доступен.
    void (\*onIncomingCall)(const char\* number);

    // Удалённая сторона ответила на вызов, соединение установлено.
    void (\*onCallAnswered)();

    // Команда исходящего набора принята модемом.
    void (\*onDialStarted)(const char\* number);

    // Вызов завершён; оставлено для совместимости.
    void (\*onCallEnded)();

    // Изменилось состояние вызова.
    void (\*onCallState)(gsm::CallState state, const char\* number);

    // Требуется аппаратное восстановление; GPIO RESET/PWRKEY обслуживает приложение.
    void (\*onRecoveryRequired)(uint8\_t attempt);

    // Завершилась отправка исходного SMS целиком либо произошла ошибка.
    // Для Multipart SMS вызывается один раз, а не на каждую часть.
    void (\*onSmsSent)(const char\* number, const char\* text, gsm::Result result);

    // Устаревшее уведомление о +CMTI с индексом нового сообщения.
    void (\*onSmsReceived)(int index);

    // Прочитано входящее SMS. Вернуть true, если приложение обработало сообщение.
    // При включённом auto-delete true разрешает удалить SMS из памяти.
    bool (\*onSmsMessage)(const gsm::SMSMessage\& sms);

    // Получена информация о занятой и общей памяти SMS.
    void (\*onSmsMemory)(const gsm::SMSMemory\& memory);

    // Память SMS заполнена не менее чем на 80%.
    void (\*onSmsMemoryWarning)(const gsm::SMSMemory\& memory);

    // Память SMS заполнена на 100%.
    void (\*onSmsMemoryFull)(const gsm::SMSMemory\& memory);

    // Ошибка чтения/обработки SMS из указанной памяти и по указанному индексу.
    void (\*onSmsError)(const char\* storage, uint8\_t index, gsm::Result result);

    // Ответ на USSD: m — статус из +CUSD, str — текст ответа UTF-8,
    // request — метаданные исходного запроса.
    void (\*onUssdResponse)(uint8\_t m, const char\* str,
                           const gsm::RequestInfo\& request);

    // Каждая полученная строка UART для отладки.
    void (\*onRawLine)(const char\* line);
};
```

\---

## Auth — список разрешённых номеров

`gsm::Auth` — whitelist с фиксированными слотами в оперативной памяти. При удалении номера остальные слоты не сдвигаются. Это удобно, если номер слота используется в настройках или интерфейсе приложения.

```cpp
gsm::Auth auth;

auth.add("+79091234567");      // Добавить в первый свободный слот.
auth.set(4, "89098887766");    // Записать номер в слот 4.

auth.setMode(gsm::AuthMode::WHITELIST);

if (auth.isAllowed(sms)) {
    // Отправитель SMS разрешён.
}

if (auth.isAllowedCall(number)) {
    // Номер звонящего разрешён.
}

auth.removeAt(4);              // Очистить слот 4.
auth.remove("+79091234567");   // Удалить по номеру.
```

### Полный публичный API `gsm::Auth`

```cpp
// Максимальное число слотов и размер строки номера.
static constexpr uint8\_t MAX\_NUMBERS;
static constexpr uint8\_t NUMBER\_SIZE;

// Добавить номер в первый свободный слот.
// Если такой номер уже есть, метод возвращает true и не создаёт дубликат.
bool add(const char\* number);

// Записать номер в конкретный слот или заменить его содержимое.
// Не допускает дубликат номера в другом слоте.
bool set(uint8\_t slot, const char\* number);

// Удалить номер по значению. Остальные слоты сохраняют свои индексы.
bool remove(const char\* number);

// Очистить указанный слот. Другие слоты не сдвигаются.
bool removeAt(uint8\_t slot);

// Алиас removeAt(): очистить конкретный слот.
bool clearSlot(uint8\_t slot);

// Проверить, присутствует ли номер в списке с учётом нормализации.
bool contains(const char\* number) const;

// Найти номер и вернуть индекс слота; -1, если номер не найден или некорректен.
int8\_t findIndex(const char\* number) const;

// Очистить все слоты и сбросить количество занятых ячеек.
void clear();

// Получить количество занятых слотов, а не индекс последнего номера + 1.
uint8\_t count() const;

// Получить общую ёмкость списка.
uint8\_t capacity() const;

// Получить количество свободных слотов.
uint8\_t freeSlots() const;

// Проверить, заняты ли все слоты.
bool isFull() const;

// Проверить, пуст ли список.
bool isEmpty() const;

// Проверить, занят ли конкретный слот.
bool isSlotUsed(uint8\_t slot) const;

// Получить номер из слота; nullptr возвращается для пустого/неверного слота.
const char\* get(uint8\_t slot) const;

// Получить индекс первого свободного слота; -1, если свободных слотов нет.
int8\_t firstFreeSlot() const;

// Установить режим проверки доступа: DISABLE или WHITELIST.
void setMode(gsm::AuthMode mode);

// Получить текущий режим проверки доступа.
gsm::AuthMode mode() const;

// Проверить разрешение для номера. В режиме DISABLE всегда возвращает true.
bool isAllowed(const char\* number) const;

// Проверить разрешение для отправителя входящего SMS.
bool isAllowed(const gsm::SMSMessage\& sms) const;

// Проверить разрешение для номера входящего вызова.
bool isAllowedCall(const char\* number) const;

// Нормализовать номер в выходной буфер.
// Российские 8XXXXXXXXXX и 7XXXXXXXXXX преобразуются в +7XXXXXXXXXX.
// Возвращает false при неверных аргументах или недостаточном размере буфера.
static bool normalize(const char\* in, char\* out, size\_t outSize);
```

По умолчанию `AUTH\_MAX\_NUMBERS` равен 16, `AUTH\_NUMBER\_SIZE` — 20 байт. Значения можно переопределить до `#include <GSM.h>`:

```cpp
#define AUTH\_MAX\_NUMBERS 32
#define AUTH\_NUMBER\_SIZE 20
#include <GSM.h>
```

**Важно:** `count()` — число занятых ячеек, а не верхняя граница индекса. Перебирать нужно всю ёмкость:

```cpp
for (uint8\_t i = 0; i < auth.capacity(); ++i) {
    const char\* phone = auth.get(i);
    if (!phone) continue;

    Serial.println(phone);
}
```

Список хранится только в RAM. Сохранение в EEPROM / Preferences и восстановление после перезапуска выполняет приложение.

\---

## Recovery

Recovery контролирует, отвечает ли модем на AT-команды. Библиотека не управляет физическими линиями RESET/PWRKEY и питанием.

```cpp
modem.setRecoveryTimeout(60000); // Интервал контроля в миллисекундах.
modem.setMaxRecoveryAttempts(3);
modem.setRecoveryEnabled(true);

void onRecoveryRequired(uint8\_t attempt) {
    if (attempt == 1) {
        // Аппаратный RESET.
    } else if (attempt == 2) {
        // Переключение PWRKEY.
    } else if (attempt == 3) {
        // Полный power-cycle питания.
    }
}
```

После восстановления связи драйвер может повторно запустить инициализацию модема.

\---

## Низкоуровневые команды

Произвольная AT-команда:

```cpp
modem.enqueue("AT+CCLK?");
modem.enqueue("AT+CBC",
              gsm::RequestInfo(),
              5000,
              gsm::CommandPriority::P\_HIGH);
```

`enqueue()` помещает команду в очередь Core. `Result::OK` означает, что команда принята в очередь, а не то, что модем уже вернул успешный ответ.

Прямая передача данных:

```cpp
modem.writeRaw("данные");
modem.writeRaw(buffer, length);
```

`writeRaw()` отправляет байты непосредственно в UART и не выполняет управление последовательностью AT-команд. Использовать его следует только при понимании текущего состояния протокола.

Для доступа к внутренним компонентам:

```cpp
modem.core();    // Core: очередь, состояние и транзакции.
modem.driver();  // Драйвер SIM800L.
modem.network(); // Слой сетевых запросов и кэша.
```

\---

## Типы и ошибки

### `gsm::Result`

```cpp
enum class Result : uint8\_t {
    OK,           // Запрос принят/операция успешна — трактовка зависит от метода.
    ERROR,        // Общая ошибка выполнения.
    TIMEOUT,      // Не получен ответ за отведённое время.
    CANCELLED,    // Операция отменена.
    BUSY,         // Ресурс или транзакция заняты.
    NOT\_READY,    // Core или модем ещё не готовы.
    QUEUE\_FULL,   // Очередь переполнена.
    INVALID\_PARAM // Некорректный аргумент.
};
```

Функции описания результата:

```cpp
gsm::errorToString(result); // Русское развёрнутое описание Result.
gsm::resultToStr(result);   // Короткая строка для логов.
gsm::errorToString(error);  // Описание ModemError.
gsm::cmeToStr(code);        // Расшифровка +CME ERROR.
gsm::cmsToStr(code);        // Расшифровка +CMS ERROR.
gsm::registrationStatusToString(status); // Описание +CREG.
```

### Другие типы

* `gsm::Source` — источник запроса: `INTERNAL`, `PERIODIC`, `WEB`, `LCD`, `BUTTON`, `SMS`, `CALL`, `EXTERNALE`.
* `gsm::RequestInfo` — `requestId`, `source` и `transactionId`. Нулевой `transactionId` означает обычную команду.
* `gsm::CommandPriority` — приоритет: `P\_LOW`, `P\_NORMAL`, `P\_HIGH`, `P\_CRITICAL`.
* `gsm::CoreState` — состояние ядра: `STOPPED`, `IDLE`, `WAITING\_RESPONSE`.
* `gsm::CallState` — состояние вызова: `IDLE`, `RINGING`, `ANSWERING`, `ACTIVE`, `DIALING`, `ENDED`, `BUSY`, `FAILED`.
* `gsm::SMSMessage` — принятое SMS; поле `text` содержит UTF-8.
* `gsm::SMSMemory` — имя памяти, число занятых и общее число ячеек; методы `free()` и `percent()` возвращают свободные ячейки и процент заполнения.
* `gsm::InitResult` — результат инициализации, готовность модема/SIM/SMS и код CME.
* `gsm::ModemError` — результат, коды CME/CMS, контекст и текст ошибки.
* `gsm::CommandResult` — метаданные запроса, результат и время выполнения.

\---

## Важные особенности

* `modem.process()` нужно вызывать постоянно.
* В каждый момент Core выполняет не более одной AT-команды.
* Внутренняя очередь Core рассчитана на 8 команд.
* Для последовательностей команд можно использовать эксклюзивные транзакции через `modem.core()`.
* Незапрошенные события модема (URC), например `RING`, `+CLIP`, `+CMTI`, `+CUSD`, `NO CARRIER`, обрабатываются независимо от обычных ответов AT-команд.
* Сетевые getter-методы возвращают кэш и не отправляют AT-команды.
* Асинхронный `Result::OK` часто означает только принятие запроса.
* Отправка длинного SMS состоит из нескольких PDU-сегментов, но `onSmsSent()` вызывается один раз на исходный текст.

## Ограничения

* Максимальный входной размер одного исходящего SMS — 280 байт UTF-8.
* Максимум 5 сегментов Multipart SMS на одно исходное сообщение.
* Буфер принятого текста SMS — 240 байт UTF-8 плюс завершающий ноль.
* Интерактивные USSD-сессии не поддерживаются.
* GPRS / TCP / HTTP / MQTT не входят в задачи библиотеки.
* DTMF-декодирование не реализовано.
* Управление RESET/PWRKEY и питанием выполняет приложение.
* Успешный ответ модема об отправке не гарантирует, что получатель прочитал сообщение.

\---

## Установка

### Arduino IDE

1. Скопируйте папку `GSM` в каталог библиотек Arduino:

   * Windows: `Документы/Arduino/libraries/GSM`
   * macOS: `\~/Documents/Arduino/libraries/GSM`
   * Linux: `\~/Arduino/libraries/GSM`
2. Перезапустите Arduino IDE.
3. Откройте **File → Examples → GSM → BasicInit**.

### PlatformIO

Добавьте в `platformio.ini`:

```ini
lib\_deps =
    https://github.com/TonTon-Macout/GSM.git
```

### VS Code / Cursor

В библиотеке есть `.vscode/c\_cpp\_properties.json`. Откройте папку проекта в VS Code / Cursor и убедитесь, что путь к `GSM/src` присутствует в `includePath`.

Публичные методы снабжены Doxygen-комментариями в заголовках. Описание методов также приведено в этом README.

### Обновление

Перед установкой новой версии рекомендуется удалить старую папку `GSM` целиком и скопировать новую. Простая замена файлов может оставить устаревшие исходники.

\---

## Обратная связь

При создании Issue приложите:

* версию библиотеки;
* используемую плату и микроконтроллер;
* версию Arduino Core / SDK;
* версию Arduino IDE или PlatformIO;
* минимальный пример, воспроизводящий проблему;
* UART-лог;
* ожидаемое и фактическое поведение.

Библиотека открыта для предложений, доработок и Pull Request.

