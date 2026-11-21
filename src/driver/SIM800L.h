#pragma once
#include <Arduino.h>
#include "core/Core.h"

#define SMS_MAX_UTF16_UNITS 280
#define SMS_MAX_UTF8_BYTES  840

namespace gsm {
    

// Detailed modem error (CME/CMS + context + ready-made message).
struct ModemError {
    Result      result;
    int16_t     cme;       // +CME ERROR code, or -1
    int16_t     cms;       // +CMS ERROR code, or -1
    const char* context;   // "init", "CMGF", "CPIN", ...
    const char* message;   // human-readable, e.g. "SIM not inserted"
};

/**
 * @brief Возвращает готовое текстовое описание подробной ошибки модема.
 * @param error Структура ModemError с результатом, кодом CME/CMS и сообщением.
 * @return Текст ошибки; если в error.message нет текста, используется описание error.result.
 * @details Возвращаемая строка принадлежит библиотеке либо является статической строкой. Не освобождайте её.
 */
inline const char* errorToString(const ModemError& error) {
    if (error.message && error.message[0]) return error.message;
    return errorToString(error.result);
}

// Result of the full init sequence (soft init, variant B).
struct InitResult {
    Result      result;      // OK only if base + SIM + SMS setup all OK
    bool        available;   // modem answered AT at least once
    bool        simReady;    // CPIN: READY
    bool        smsReady;    // CMGF + CNMI succeeded
    int16_t     cme;         // last CME during init, or -1
    const char* message;     // short summary for logs/UI
};

// Parsed SMS message. Text is UTF-8; UCS2 HEX responses are decoded automatically.
struct SMSMessage {
    char storage[4];
    uint8_t index;
    char status[16];
    char sender[32];
    char timestamp[24];
    char text[241];
};

/**
 * @brief Содержит статистику заполнения выбранной памяти SMS.
 * @param storage Тип памяти: SM (SIM-карта) или ME (память модема).
 * @param used Количество занятых ячеек SMS.
 * @param total Общее количество ячеек SMS.
 */
struct SMSMemory {
    char storage[4];
    uint8_t used;
    uint8_t total;

    /**
     * @brief Вычисляет количество свободных ячеек SMS.
     * @return total - used; если used больше total, возвращается 0.
     */
    uint8_t free() const { return total >= used ? total - used : 0; }

    /**
     * @brief Вычисляет процент заполнения памяти SMS.
     * @return Целое значение от 0 до 100; при total == 0 возвращается 0.
     */
    uint8_t percent() const { return total ? (uint16_t)used * 100 / total : 0; }
};

enum class CallState : uint8_t {
    IDLE, RINGING, ANSWERING, ACTIVE, DIALING, ENDED, BUSY, FAILED
};

// High-level modem events delivered to the application.
struct ModemCallbacks {
    void (*onReady)();
    void (*onInitComplete)(Result result);              // legacy
    void (*onInitCompleteEx)(const InitResult& result); // preferred
    void (*onError)(Result result, const char* context); // legacy
    void (*onModemError)(const ModemError& error);       // preferred

    void (*onSimStatus)(const char* status);          // e.g. "READY", "SIM PIN"
    void (*onRegistration)(uint8_t n, uint8_t stat);  // +CREG
    void (*onSignal)(uint8_t rssi, uint8_t ber);      // +CSQ
    void (*onOperator)(const char* name);             // +COPS

    void (*onIncomingCall)(const char* number);       // +CLIP / RING
    void (*onCallAnswered)();                         // remote call actually connected
    void (*onDialStarted)(const char* number);        // ATD completed
    void (*onCallEnded)();                            // NO CARRIER etc. (legacy)
    void (*onCallState)(CallState state, const char* number);
    void (*onRecoveryRequired)(uint8_t attempt);      // user performs RESET/PWRKEY/power cycle
    void (*onSmsSent)(const char* number, const char* text, Result result); // outgoing SMS result
    void (*onSmsReceived)(int index);                 // legacy +CMTI callback
    bool (*onSmsMessage)(const SMSMessage& sms);      // true = successfully handled
    void (*onSmsMemory)(const SMSMemory& memory);
    void (*onSmsMemoryWarning)(const SMSMemory& memory); // threshold >=80%
    void (*onSmsMemoryFull)(const SMSMemory& memory); // threshold 100%
    void (*onSmsError)(const char* storage, uint8_t index, Result result);
    void (*onUssdResponse)(uint8_t m, const char* str, const RequestInfo& request); // +CUSD

    void (*onRawLine)(const char* line);              // optional debug
};

// Internal request identifiers used by the driver.
enum class DriverRequest : uint16_t {
    NONE = 0,
    INIT_AT,
    INIT_ATE,
    INIT_CMEE,
    INIT_CLIP,
    INIT_CREG,
    INIT_CPIN,
    INIT_CMGF,
    INIT_CNMI,
    CHECK_CPIN,
    CHECK_CREG,
    CHECK_CSQ,
    CHECK_COPS,
    SMS_SELECT = 20, SMS_READ, SMS_DELETE, SMS_DELETE_ALL, SMS_MEMORY,
    SMS_SCAN_SELECT, SMS_SCAN_LIST,
    USSD_SEND,
    SMS_SEND_CMGF, SMS_SEND_CSCS, SMS_SEND_CSMP, SMS_SEND_CMGS, SMS_SEND_RESTORE,
    CALL_ANSWER, CALL_HANGUP, CALL_DIAL,
    RECOVERY_PING,
    SMS_DELETE_SELECT, SMS_DELETE_ALL_SELECT, SMS_MEMORY_SELECT,
    USER_BASE = 1000
};

class SIM800L {
public:
/**
 * @brief Создаёт драйвер SIM800L поверх указанного ядра AT-команд.
 * @details Перед использованием настройте UART и необходимые параметры приложения.
 */
    explicit SIM800L(Core& core);

/**
 * @brief Назначает обработчики событий модема для пользовательского приложения.
 * @param cb Структура ModemCallbacks с callback для ошибок, инициализации, сети, звонков, SMS и USSD.
 * @return Ничего не возвращает.
 * @details Передавайте структуру, предварительно обнулённую через {}. Для неиспользуемых событий оставляйте nullptr. Старый onError сохраняется для совместимости; onModemError предоставляет расширенные сведения CME/CMS.
 */
    void setCallbacks(const ModemCallbacks& cb);

    // Start asynchronous initialisation sequence.
/**
 * @brief Запускает пошаговую асинхронную инициализацию модема: проверяет связь, настраивает базовые параметры, SIM и SMS.
 * @param request Контекст инициализации: источник и пользовательский идентификатор запроса.
 * @return Result::OK — первая команда принята; BUSY — уже идёт инициализация/транзакция; NOT_READY — Core не запущен; другое значение — ошибка постановки команды в очередь.
 * @details Метод только запускает процесс. Для продолжения регулярно вызывайте process(). Окончательный результат сообщается через onInitComplete или onInitCompleteEx; подробная ошибка также передаётся через onError/onModemError.
 */
    Result startInit(const RequestInfo& request = RequestInfo());

    // Periodic / on-demand status queries (non-blocking).
/**
 * @brief Ставит в очередь запрос текущего состояния SIM-карты (AT+CPIN?).
 * @param request Контекст запроса, включая источник вызова.
 * @return Result::OK — запрос поставлен в очередь; другое значение — причина отказа.
 * @details Метод асинхронный. Результат поступит через callback onSimStatus; сам метод не ждёт ответа модема.
 */
    Result checkSim(const RequestInfo& request = RequestInfo());
/**
 * @brief Ставит в очередь запрос статуса регистрации в сети (AT+CREG?).
 * @param request Контекст запроса, включая источник вызова.
 * @return Result::OK — запрос поставлен в очередь; другое значение — причина отказа.
 * @details Новый статус будет сохранён в кеше и передан через onRegistration.
 */
    Result checkRegistration(const RequestInfo& request = RequestInfo());
/**
 * @brief Ставит в очередь измерение уровня сигнала (AT+CSQ).
 * @param request Контекст запроса, включая источник вызова.
 * @return Result::OK — запрос поставлен в очередь; другое значение — причина отказа.
 * @details Результат будет сохранён в кеше и передан через onSignal.
 */
    Result checkSignal(const RequestInfo& request = RequestInfo());
/**
 * @brief Ставит в очередь запрос названия оператора (AT+COPS?).
 * @param request Контекст запроса, включая источник вызова.
 * @return Result::OK — запрос поставлен в очередь; другое значение — причина отказа.
 * @details Метод не ждёт ответа. Название оператора обновится асинхронно.
 */
    Result checkOperator(const RequestInfo& request = RequestInfo());

/**
 * @brief Добавляет произвольную AT-команду в очередь выполнения.
 * @param cmd Текст AT-команды без завершающих CR/LF.
 * @param request Контекст команды: ID, источник и ID транзакции.
 * @param timeoutMs Таймаут ожидания ответа модема, мс; по умолчанию 3000.
 * @param priority Приоритет команды; более высокий приоритет отправляется раньше.
 * @return Result::OK — команда принята; INVALID_PARAM — команда некорректна; QUEUE_FULL — очередь заполнена; другие значения описывают причину отказа.
 * @details Команда не отправляется немедленно, если канал занят. Ответ и завершение отслеживайте через callback команды.
 */
    Result enqueue(const char* cmd,
                   const RequestInfo& request = RequestInfo(),
                   uint16_t timeoutMs = 3000,
                   CommandPriority priority = CommandPriority::P_NORMAL);

/**
 * @brief Передаёт в UART текстовую строку без формирования AT-команды.
 * @param data Нуль-терминированная строка для отправки; nullptr не допускается.
 * @return true — строка передана в последовательный поток; false — поток недоступен или передача не выполнена.
 * @details Не добавляет CR/LF и не создаёт запись в очереди AT-команд. Все необходимые завершающие символы должен добавить вызывающий код.
 */
    bool writeRaw(const char* data);
/**
 * @brief Передаёт бинарный буфер байтов в UART без формирования AT-команды.
 * @param data Указатель на массив отправляемых байтов.
 * @param length Количество байтов для отправки.
 * @return true — данные переданы в последовательный поток; false — буфер некорректен или передача не выполнена.
 * @details Не добавляет CR/LF и не создаёт запись в очереди AT-команд. Передаваемый буфер не копируется библиотекой.
 */
    bool writeRaw(const uint8_t* data, size_t length);

    // SMS handling. Manual retention is the default.
/**
 * @brief Устанавливает политику автоматического удаления обработанных SMS.
 * @param enabled true — удалять обработанные сообщения; false — оставлять в памяти модема.
 * @return Ничего не возвращает.
 * @details По умолчанию используется ручное хранение сообщений.
 */
    void setSmsAutoDelete(bool enabled) { _smsAutoDelete = enabled; }
/**
 * @brief Показывает, включено ли автоматическое удаление обработанных SMS.
 * @return true — автоудаление включено; false — сообщения сохраняются.
 * @details Возвращает текущую настройку без обращения к модему.
 */
    bool isSmsAutoDelete() const { return _smsAutoDelete; }
/**
 * @brief Запускает асинхронное удаление одного SMS из памяти модема.
 * @param storage Тип памяти: "SM" — SIM-карта, "ME" — память модема.
 * @param index Индекс сообщения в выбранной памяти.
 * @return Result::OK — операция принята; INVALID_PARAM — неверная память; BUSY — канал занят; иначе причина отказа.
 * @details Удаление выполняется AT-командами в отдельной транзакции. Успешный возврат означает принятие операции, а не подтверждение фактического удаления.
 */
    Result deleteSMS(const char* storage, uint8_t index);
/**
 * @brief Запускает асинхронное удаление всех SMS из выбранной памяти.
 * @param storage Тип памяти: "SM" — SIM-карта, "ME" — память модема.
 * @return Result::OK — операция принята; INVALID_PARAM — неверная память; BUSY — канал занят; иначе причина отказа.
 * @details Операция выполняется в отдельной транзакции и может занять время в зависимости от модема.
 */
    Result deleteAllSMS(const char* storage);
/**
 * @brief Запрашивает количество занятых и свободных ячеек SMS в выбранной памяти.
 * @param storage Тип памяти: "SM" — SIM-карта, "ME" — память модема.
 * @return Result::OK — запрос принят; INVALID_PARAM — неверная память; BUSY — канал занят; иначе причина отказа.
 * @details Результат приходит асинхронно через onSmsMemory.
 */
    Result checkSmsMemory(const char* storage);

    // Send a one-shot USSD request. Response is delivered through onUssdResponse.
    // Default DCS 15 is GSM 7-bit; use 72 for UCS2 when required by the operator.
/**
 * @brief Отправляет одноразовый USSD-запрос оператору мобильной связи.
 * @param code USSD-код, например "*100#". Не должен содержать кавычки, обратную косую черту или управляющие символы.
 * @param dcs Кодировка USSD-строки; по умолчанию 15 (GSM 7-bit), для UCS2 обычно 72.
 * @param request Контекст запроса, который будет передан вместе с ответом.
 * @return Result::OK — запрос принят; BUSY — уже ожидается другой USSD-ответ; INVALID_PARAM — неверный код; иначе причина отказа.
 * @details Ответ приходит асинхронно через onUssdResponse. Одновременно поддерживается один незавершённый USSD-запрос.
 */
    Result sendUSSD(const char* code, uint8_t dcs = 15,
                    const RequestInfo& request = RequestInfo());

    // Outgoing SMS (UTF-8 text, encoded as UCS2 in PDU mode).
    // Long messages are automatically split into concatenated multipart SMS.
    // Up to SMS_OUT_QUEUE_SIZE messages can be queued and are sent one by one.
    // Returns OK when the message was started or accepted into the queue,
    // QUEUE_FULL when the outgoing queue is already full, BUSY only for
    // internal transaction conflicts while idle.
/**
 * @brief Отправляет SMS или добавляет его в очередь исходящих сообщений.
 * @param number Номер получателя в международном формате; допускаются цифры и начальный знак +.
 * @param text Текст сообщения в UTF-8. Длинный текст автоматически разбивается на составные SMS.
 * @param request Контекст отправки, включая источник сообщения.
 * @return Result::OK — сообщение отправлено в обработку/очередь; INVALID_PARAM — неверный номер или текст; NOT_READY — модем не запущен; QUEUE_FULL — очередь заполнена; BUSY — занята внутренняя транзакция.
 * @details Отправка асинхронная. Итог каждой отправки передаётся через onSmsSent. Максимальная длина ограничена SMS_MAX_UTF8_BYTES и лимитом UTF-16.
 */
    Result sendSMS(const char* number, const char* text,
                   const RequestInfo& request = RequestInfo());
/**
 * @brief Возвращает количество исходящих SMS, ожидающих своей очереди.
 * @return Количество ожидающих сообщений.
 * @details Не включает сообщение, отправка которого уже выполняется.
 */
    uint8_t smsSendQueueCount() const { return _smsOutCount; }
/**
 * @brief Показывает, выполняется ли сейчас отправка исходящего SMS.
 * @return true — отправка выполняется; false — отправки нет.
 * @details Для результата отправки используйте callback onSmsSent.
 */
    bool isSmsSendActive() const { return _smsSendActive; }
/**
 * @brief Очищает очередь SMS, ожидающих отправки.
 * @return Ничего не возвращает.
 * @details Текущее сообщение, если оно уже отправляется, не прерывается.
 */
    void clearSmsSendQueue() { _smsOutHead = 0; _smsOutCount = 0; }

    // Voice calls.
/**
 * @brief Отправляет модему команду ответа на входящий звонок (ATA).
 * @param request Контекст операции, включая источник вызова.
 * @return Result::OK — команда принята в очередь; иначе причина отказа.
 * @details Метод не ждёт фактического соединения. Состояние звонка обновляется асинхронно через onCallState/onCallAnswered.
 */
    Result answerCall(const RequestInfo& request = RequestInfo());
/**
 * @brief Отправляет модему команду завершения текущего звонка (ATH).
 * @param request Контекст операции, включая источник вызова.
 * @return Result::OK — команда принята в очередь; иначе причина отказа.
 * @details Фактическое завершение вызова отслеживается асинхронно по состоянию звонка.
 */
    Result hangUp(const RequestInfo& request = RequestInfo());
/**
 * @brief Набирает указанный телефонный номер.
 * @param number Номер абонента: цифры и необязательный + в начале; длина не более 20 символов.
 * @param request Контекст операции, включая источник вызова.
 * @return Result::OK — команда набора принята; INVALID_PARAM — неверный номер; иначе причина отказа.
 * @details Вызов выполняется асинхронно. Состояние DIALING и последующие изменения передаются через onCallState.
 */
    Result dial(const char* number, const RequestInfo& request = RequestInfo());
/**
 * @brief Задаёт интервал автоматического сканирования памяти на новые SMS.
 * @param intervalMs Интервал в миллисекундах; 0 отключает периодическое сканирование.
 * @return Ничего не возвращает.
 * @details Следующее сканирование при ненулевом интервале может быть запущено сразу.
 */
    void setSmsScanInterval(uint32_t intervalMs); // 0 disables periodic scan
/**
 * @brief Возвращает интервал периодического сканирования SMS.
 * @return Интервал в миллисекундах; 0 означает, что сканирование выключено.
 * @details Возвращает локальную настройку.
 */
    uint32_t smsScanInterval() const { return _smsScanInterval; }

    // Non-blocking AT health checks. Timeout is the interval between checks.
/**
 * @brief Включает или отключает механизм контроля доступности модема.
 * @param enabled true — включить; false — выключить.
 * @return Ничего не возвращает.
 * @details При потере связи драйвер может вызвать onRecoveryRequired, чтобы приложение выполнило аппаратный сброс.
 */
    void setRecoveryEnabled(bool enabled);
/**
 * @brief Показывает, включён ли контроль доступности модема.
 * @return true — контроль включён; false — отключён.
 * @details Не выполняет проверку связи самостоятельно.
 */
    bool isRecoveryEnabled() const { return _recoveryEnabled; }
/**
 * @brief Устанавливает период между контрольными проверками модема.
 * @param intervalMs Интервал между проверками в миллисекундах.
 * @return Ничего не возвращает.
 * @details Проверки выполняются неблокирующе из process().
 */
    void setRecoveryTimeout(uint32_t intervalMs);
/**
 * @brief Возвращает интервал между проверками доступности модема.
 * @return Интервал в миллисекундах.
 * @details Возвращает текущую настройку механизма восстановления.
 */
    uint32_t recoveryTimeout() const { return _recoveryTimeout; }
/**
 * @brief Ограничивает максимальное число попыток контроля/восстановления связи.
 * @param attempts Максимальное число попыток; 0 заменяется на 1.
 * @return Ничего не возвращает.
 * @details Для нового цикла восстановления используйте resetRecoveryAttempts().
 */
    void setMaxRecoveryAttempts(uint8_t attempts) { _maxRecoveryAttempts = attempts ? attempts : 1; }
/**
 * @brief Возвращает количество выполненных попыток восстановления.
 * @return Количество попыток с момента последнего сброса.
 * @details Счётчик не является числом успешных восстановлений.
 */
    uint8_t recoveryAttempts() const { return _recoveryAttempts; }
/**
 * @brief Сбрасывает счётчик попыток восстановления модема.
 * @return Ничего не возвращает.
 * @details Используйте после ручного перезапуска или восстановления питания модема.
 */
    void resetRecoveryAttempts();

/**
 * @brief Возвращает текущее состояние голосового вызова.
 * @return Значение CallState: IDLE, RINGING, ANSWERING, ACTIVE, DIALING, ENDED, BUSY или FAILED.
 * @details Состояние обновляется асинхронно по событиям модема.
 */
    CallState callState() const { return _callState; }

/**
 * @brief Обрабатывает входящие байты, таймауты и очередь AT-команд.
 * @return Ничего не возвращает.
 * @details Вызывать регулярно из loop(); метод не блокирует выполнение в ожидании ответа.
 */
    void process();

    // Automatic non-blocking network monitoring.
/**
 * @brief Включает автоматический периодический опрос состояния SIM-карты и мобильной сети.
 * @return Ничего не возвращает.
 * @details Результаты обновляют кеш и передаются через соответствующие callback. Для работы необходимо регулярно вызывать process().
 */
    void startMonitoring();
/**
 * @brief Выключает автоматический периодический опрос состояния модема.
 * @return Ничего не возвращает.
 * @details Уже отправленная AT-команда не прерывается; прекращается запуск следующих периодических проверок.
 */
    void stopMonitoring() { _monitoring = false; }
/**
 * @brief Проверяет, включён ли автоматический мониторинг сети.
 * @return true — мониторинг включён; false — выключен.
 * @details Не отправляет AT-команду, возвращает внутренний флаг.
 */
    bool isMonitoring() const { return _monitoring; }
/**
 * @brief Задаёт интервалы автоматического опроса параметров SIM-карты и сети.
 * @param simMs Интервал проверки SIM-карты, мс; 0 отключает этот опрос.
 * @param registrationMs Интервал проверки регистрации в сети, мс; 0 отключает опрос.
 * @param signalMs Интервал измерения уровня сигнала, мс; 0 отключает опрос.
 * @param operatorMs Интервал получения названия оператора, мс; 0 отключает опрос.
 * @return Ничего не возвращает.
 * @details Интервалы отсчитываются по millis(). Фактическое выполнение зависит от занятости очереди AT-команд.
 */
    void setPollIntervals(uint32_t simMs, uint32_t registrationMs,
                         uint32_t signalMs, uint32_t operatorMs);

/**
 * @brief Показывает, отвечал ли модем хотя бы один раз на AT-команду после запуска библиотеки.
 * @return true — был получен хотя бы один ответ; false — успешного ответа ещё не было.
 * @details Это признак обнаружения связи с модемом, а не подтверждение регистрации в сети или готовности SIM. Значение не требует новой AT-команды.
 */
    bool isAvailable() const { return _available; } // answered AT at least once
/**
 * @brief Проверяет, насколько давно модем последний раз успешно отвечал на AT-команду.
 * @param maxAgeMs Максимально допустимый возраст последнего успешного ответа, мс. По умолчанию 30000 мс (30 секунд).
 * @return true — успешный ответ получен не более maxAgeMs назад; false — ответ отсутствует или устарел.
 * @details Проверяет сохранённую отметку времени и не отправляет новую AT-команду.
 */
    bool isResponsive(uint32_t maxAgeMs = 30000) const {
        return _lastSuccessfulCommandAt != 0 &&
               static_cast<uint32_t>(millis() - _lastSuccessfulCommandAt) <= maxAgeMs;
    }
/**
 * @brief Проверяет, зарегистрирован ли модем в домашней сети или роуминге.
 * @return true — регистрация подтверждена; false — регистрации нет или состояние ещё неизвестно.
 * @details Эквивалентно проверке статуса регистрации 1 (домашняя сеть) или 5 (роуминг). Использует последнее полученное значение.
 */
    bool isNetworkConnected() const { return isRegistered(); }
/**
 * @brief Показывает, завершилась ли полная последовательность инициализации модема успешно.
 * @return true — базовая настройка, SIM и SMS готовы; false — инициализация не завершена или завершилась ошибкой.
 * @details Не путать с isAvailable(): наличие ответа на AT ещё не означает готовность SIM-карты и SMS.
 */
    bool isInitDone() const { return _initDone; }
/**
 * @brief Проверяет, готова ли SIM-карта к работе без запроса PIN/PUK.
 * @return true — модем сообщил CPIN: READY; false — SIM не готова или статус ещё не получен.
 * @details Возвращает последнее состояние, полученное при инициализации или checkSim().
 */
    bool isSimReady() const { return _simReady; }
/**
 * @brief Проверяет, выполнена ли настройка модема для работы с SMS.
 * @return true — режим SMS и уведомления настроены; false — настройка не завершена или завершилась ошибкой.
 * @details Это состояние настройки SMS, а не наличие свободной памяти для сообщений.
 */
    bool isSmsReady() const { return _smsReady; }
/**
 * @brief Проверяет, выполняется ли сейчас команда и ожидается ли ответ модема.
 * @return true — есть активная команда; false — активной команды нет.
 * @details Не учитывает только наличие команд в очереди; для очереди используйте queuedCount().
 */
    bool isBusy() const { return _core.isBusy(); }

/**
 * @brief Возвращает структуру с последней ошибкой модема и её дополнительными кодами.
 * @return Ссылка на внутренние данные.
 * @details Константная ссылка на внутреннюю ModemError; данные заменяются при следующей ошибке.
 */
    const ModemError& lastError() const { return _lastError; }
/**
 * @brief Возвращает подробный результат последней инициализации модема.
 * @return Ссылка на внутренние данные.
 * @details Константная ссылка на внутреннюю структуру InitResult.
 */
    const InitResult& lastInitResult() const { return _lastInit; }
/**
 * @brief Возвращает текст последней ошибки модема.
 * @return Указатель на строку.
 * @details Указатель на строку, хранящуюся внутри драйвера.
 */
    const char* lastErrorText() const { return errorToString(_lastError); }

    // Cached network data. Values are updated asynchronously from modem responses.
/**
 * @brief Возвращает последнее измеренное значение качества сигнала CSQ.
 * @return RSSI из ответа +CSQ (0–31); -1 означает, что достоверных данных пока нет.
 * @details Значение кешируется. Метод не выполняет запрос к модему.
 */
    int8_t signalQuality() const { return _signalQuality; } // -1 = unknown
/**
 * @brief Возвращает оценку уровня сигнала в dBm по последнему ответу CSQ.
 * @return Оценка уровня сигнала в dBm; -1 означает, что данных нет.
 * @details Значение вычисляется из RSSI и обновляется при получении нового ответа модема.
 */
    int8_t signalDbm() const { return _signalDbm; }         // -1 = unknown
/**
 * @brief Возвращает числовой статус регистрации в мобильной сети (+CREG).
 * @return Код статуса: 0 — нет регистрации; 1 — домашняя сеть; 2 — поиск; 3 — отказ; 4 — неизвестно; 5 — роуминг.
 * @details Возвращает кешированное значение, не отправляет AT+CREG? самостоятельно.
 */
    uint8_t registrationStatus() const { return _registrationStatus; } // 4 = unknown
/**
 * @brief Проверяет, зарегистрирован ли модем в мобильной сети.
 * @return true — статус 1 (домашняя сеть) или 5 (роуминг); иначе false.
 * @details Проверка выполняется по последнему полученному статусу регистрации.
 */
    bool isRegistered() const {
        return _registrationStatus == 1 || _registrationStatus == 5;
    }
/**
 * @brief Проверяет, получались ли от модема корректные данные об уровне сигнала.
 * @return true — данные CSQ уже получены; false — данных пока нет.
 * @details Позволяет отличить нулевой/низкий уровень сигнала от ещё не выполненного измерения.
 */
    bool hasSignalData() const { return _signalQuality >= 0; }
/**
 * @brief Проверяет, получен ли хотя бы один корректный ответ о регистрации в сети.
 * @return true — статус регистрации известен; false — ответ ещё не получен.
 * @details Не означает, что модем зарегистрирован: для этого используйте isRegistered().
 */
    bool hasRegistrationData() const { return _registrationValid; }
/**
 * @brief Возвращает кешированное название мобильного оператора.
 * @return Указатель на строку.
 * @details Указатель на внутреннюю строку; метод не отправляет AT-команду.
 */
    const char* operatorName() const { return _operatorName; }
/**
 * @brief Проверяет, получено ли название оператора мобильной сети.
 * @return true — данные оператора получены; false — данных пока нет.
 * @details Не отправляет AT-команду.
 */
    bool hasOperatorData() const { return _operatorValid; }
/**
 * @brief Возвращает кешированный статус SIM-карты.
 * @return Указатель на строку.
 * @details Указатель на внутреннюю строку, например READY или SIM PIN.
 */
    const char* simStatus() const { return _simStatus; }
/**
 * @brief Возвращает последнюю строку, полученную от модема.
 * @return Указатель на строку.
 * @details Указатель на внутренний буфер, который обновляется при следующих ответах.
 */
    const char* lastResponse() const { return _lastResponse; }
/**
 * @brief Возвращает время получения последней строки ответа модема.
 * @return Значение millis() на момент последнего ответа; 0 — ответов ещё не было.
 * @details Используйте разность unsigned-значений millis() для проверки возраста.
 */
    uint32_t lastResponseTime() const { return _lastResponseAt; }
/**
 * @brief Возвращает время последнего обновления данных уровня сигнала.
 * @return Время millis() последнего обновления; 0 — измерения ещё не было.
 * @details Позволяет оценить актуальность кешированного значения signalQuality().
 */
    uint32_t lastSignalUpdate() const { return _signalUpdatedAt; }
/**
 * @brief Возвращает время последнего обновления статуса регистрации.
 * @return Время millis() последнего обновления; 0 — статус ещё не получен.
 * @details Позволяет оценить актуальность registrationStatus().
 */
    uint32_t lastRegistrationUpdate() const { return _registrationUpdatedAt; }
/**
 * @brief Возвращает время последнего обновления названия оператора.
 * @return Время millis() последнего обновления; 0 — данные ещё не получены.
 * @details Позволяет оценить актуальность operatorName().
 */
    uint32_t lastOperatorUpdate() const { return _operatorUpdatedAt; }
/**
 * @brief Возвращает время последнего обновления статуса SIM-карты.
 * @return Время millis() последнего обновления; 0 — статус ещё не получен.
 * @details Позволяет оценить актуальность simStatus().
 */
    uint32_t lastSimUpdate() const { return _simUpdatedAt; }

private:
    Core&          _core;
    ModemCallbacks _cb;

    bool        _available;
    bool        _initDone;
    bool        _simReady;
    bool        _smsReady;
    bool        _initInProgress;
    uint8_t     _initStep;
    RequestInfo _initRequest;

    int16_t     _lastCme;
    int16_t     _lastCms;
    int8_t      _signalQuality;
    int8_t      _signalDbm;
    uint8_t     _registrationStatus;
    bool        _registrationValid;
    ModemError  _lastError;
    InitResult  _lastInit;
    char        _messageBuf[80];
    char        _operatorName[32];
    char        _simStatus[24];
    char        _lastResponse[64];
    bool        _operatorValid;
    bool        _monitoring;
    uint32_t    _simInterval, _registrationInterval, _signalInterval, _operatorInterval;
    uint32_t    _lastSimPoll, _lastRegistrationPoll, _lastSignalPoll, _lastOperatorPoll;
    uint32_t    _lastResponseAt, _lastSuccessfulCommandAt;
    uint32_t    _simUpdatedAt, _registrationUpdatedAt;
    uint32_t    _signalUpdatedAt, _operatorUpdatedAt;
    uint8_t     _pollCursor;

    bool        _recoveryEnabled;
    bool        _recoveryCheckPending;
    uint8_t     _recoveryAttempts;
    uint8_t     _maxRecoveryAttempts;
    uint32_t    _recoveryTimeout;
    uint32_t    _lastRecoveryCheck;

    CallState  _callState;
    char       _callNumber[24];

    struct PendingSMS { char storage[4]; uint8_t index; };
    PendingSMS _smsQueue[60];
    uint8_t _smsHead, _smsCount;
    uint8_t _smsProcessedSM[32], _smsProcessedME[32];
    uint32_t _smsScanInterval, _smsLastScan;
    bool _smsScanActive;
    uint8_t _smsScanStage;
    bool _smsAutoDelete, _smsReading, _smsDeletePending;
    char _smsActiveStorage[4];
    uint8_t _smsActiveIndex;
    SMSMessage _smsCurrent;
    SMSMemory _smsMemory;
    bool _smsHeaderSeen;

    // Pending asynchronous USSD request context (+CUSD response).
    bool        _ussdPending;
    RequestInfo _ussdRequest;
    uint32_t    _ussdStartedAt;

    char _smsSendNumberDisplay[24];
    char _smsSendBody[361];
    char _smsSendText[SMS_MAX_UTF8_BYTES + 1];
    Result _smsSendResult;
    bool _smsSendActive;
    uint16_t _smsUtf16[280];
    uint16_t _smsUtf16Count;
    uint16_t _smsSegmentOffset;
    uint8_t _smsSegmentCount;
    uint8_t _smsSegmentIndex;
    uint8_t _smsConcatRef;
    uint16_t _smsSendPduLength;
    char _dialNumber[24];

    // Outgoing SMS queue (send several messages back-to-back).
    static const uint8_t SMS_OUT_QUEUE_SIZE = 5;
    struct OutgoingSMS {
        char number[24];
        char text[SMS_MAX_UTF8_BYTES + 1];
        Source source;
    };
    OutgoingSMS _smsOutQueue[SMS_OUT_QUEUE_SIZE];
    uint8_t _smsOutHead;
    uint8_t _smsOutCount;

    void processMonitoring();
    void processRecovery();
    void processSms();
    void setCallState(CallState state);
    bool queueSms(const char* storage, uint8_t index);
    bool smsWasProcessed(const char* storage, uint8_t index) const;
    void setSmsProcessed(const char* storage, uint8_t index, bool value);
    void removeQueuedSms(const char* storage, int index);
    void startSmsScan();
    void advanceSmsScan(Result result);
    void startNextSms();
    void parseCmgrHeader(const char* line);
    void parseSmsMemory(const char* line);
    void finishSmsRead(Result result);
    Result enqueueSmsCommand(const char* cmd, int64_t requestId, uint16_t timeout = 10000,
                             uint16_t transactionId = 0);
    void storeLastResponse(const char* line);
    void finishSmsSend(Result result);
    Result beginSmsSend(const char* number, const char* text, Source source);
    bool prepareSmsText(const char* text);
    Result enqueueSmsPduSegment();
    bool buildSmsPduSegment();
    void startNextOutgoingSms();

    void onCoreRawLine(const char* line);
    void onCoreURC(const char* line);
    void onCoreResponseLine(const char* line, const RequestInfo& request);
    void onCoreCommandComplete(const CommandResult& result);
    void onCorePrompt(const RequestInfo& request);

    void advanceInit(Result lastResult);
    void finishInit();
    void reportError(Result result, const char* context);
    void captureErrorLine(const char* line);
    void handleURC(const char* line);
    void parseCreg(const char* line);
    void parseCsq(const char* line);
    void parseCpin(const char* line);
    void parseCops(const char* line);
    void parseClip(const char* line);
    void parseCmti(const char* line);
    void parseCusd(const char* line);
    void setMessage(const char* msg);

    static SIM800L* s_instance;
    static void trampolineRawLine(const char* line);
    static void trampolineURC(const char* line);
    static void trampolineResponseLine(const char* line, const RequestInfo& request);
    static void trampolineCommandComplete(const CommandResult& result);
    static void trampolinePrompt(const RequestInfo& request);
};

} // namespace gsm
