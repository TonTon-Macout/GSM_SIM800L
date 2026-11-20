#pragma once

#include "core/Types.h"
#include "core/Core.h"
#include "driver/SIM800L.h"
#include "network/Network.h"
#include "Auth.h"

namespace gsm {

/**
 * Public facade of the GSM library.
 *
 *   #include <GSM.h>
 *   gsm::GSM modem(Serial1);
 */
class GSM {
public:
/**
 * @brief Создаёт фасад библиотеки GSM и связывает последовательный порт с ядром и драйвером.
 * @details Перед использованием настройте UART и необходимые параметры приложения.
 */
    explicit GSM(Stream& serial)
        : _core(serial),
          _driver(_core),
          _network(_driver) {}

/**
 * @brief Запускает ядро обмена AT-командами и подготавливает внутренний парсер.
 * @return Ничего не возвращает.
 * @details Вызывать после настройки UART и до постановки AT-команд в очередь.
 */
    void begin() { _core.begin(); }
/**
 * @brief Останавливает ядро обмена AT-командами.
 * @return Ничего не возвращает.
 * @details Останавливает дальнейшую обработку и отправку команд. Для возобновления вызовите begin().
 */
    void stop()  { _core.stop(); }

/**
 * @brief Обрабатывает входящие байты, таймауты и очередь AT-команд.
 * @return Ничего не возвращает.
 * @details Вызывать регулярно из loop(); метод не блокирует выполнение в ожидании ответа.
 */
    void process() {
        _core.process();
        _driver.process();
    }

    // Automatic asynchronous monitoring (enabled automatically after successful init).
/**
 * @brief Включает автоматический периодический опрос состояния SIM-карты и мобильной сети.
 * @return Ничего не возвращает.
 * @details Результаты обновляют кеш и передаются через соответствующие callback. Для работы необходимо регулярно вызывать process().
 */
    void startMonitoring() { _network.startMonitoring(); }
/**
 * @brief Выключает автоматический периодический опрос состояния модема.
 * @return Ничего не возвращает.
 * @details Уже отправленная AT-команда не прерывается; прекращается запуск следующих периодических проверок.
 */
    void stopMonitoring() { _network.stopMonitoring(); }
/**
 * @brief Проверяет, включён ли автоматический мониторинг сети.
 * @return true — мониторинг включён; false — выключен.
 * @details Не отправляет AT-команду, возвращает внутренний флаг.
 */
    bool isMonitoring() const { return _network.isMonitoring(); }
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
                          uint32_t signalMs, uint32_t operatorMs) {
        _network.setPollIntervals(simMs, registrationMs, signalMs, operatorMs);
    }

/**
 * @brief Устанавливает обработчики событий ядра AT-команд.
 * @param callbacks Структура CoreCallbacks с функциями обработки строк, ответов, приглашений и завершения команд.
 * @return Ничего не возвращает.
 * @details Неиспользуемые указатели должны быть nullptr. Callback вызываются во время process().
 */
    void setCallbacks(const ModemCallbacks& cb) {
        _driver.setCallbacks(cb);
    }

    // Asynchronous modem initialisation (soft init, variant B).
/**
 * @brief Запускает асинхронную последовательность первоначальной настройки SIM800L.
 * @param request Контекст запроса: идентификатор, источник и идентификатор транзакции.
 * @return Result::OK — последовательность принята к выполнению; иначе возвращается причина, по которой запуск невозможен.
 * @details Успешный возврат не означает, что модем уже настроен. Итог приходит через onInitComplete/onInitCompleteEx; во время настройки вызывайте process().
 */
    Result init(const RequestInfo& request = RequestInfo()) {
        return _driver.startInit(request);
    }

    // Status
/**
 * @brief Показывает, отвечал ли модем хотя бы один раз на AT-команду после запуска библиотеки.
 * @return true — был получен хотя бы один ответ; false — успешного ответа ещё не было.
 * @details Это признак обнаружения связи с модемом, а не подтверждение регистрации в сети или готовности SIM. Значение не требует новой AT-команды.
 */
    bool isAvailable() const { return _driver.isAvailable(); } // modem answered AT at least once
/**
 * @brief Проверяет, насколько давно модем последний раз успешно отвечал на AT-команду.
 * @param maxAgeMs Максимально допустимый возраст последнего успешного ответа, мс. По умолчанию 30000 мс (30 секунд).
 * @return true — успешный ответ получен не более maxAgeMs назад; false — ответ отсутствует или устарел.
 * @details Проверяет сохранённую отметку времени и не отправляет новую AT-команду.
 */
    bool isResponsive(uint32_t maxAgeMs = 30000) const { return _driver.isResponsive(maxAgeMs); }
/**
 * @brief Проверяет, зарегистрирован ли модем в домашней сети или роуминге.
 * @return true — регистрация подтверждена; false — регистрации нет или состояние ещё неизвестно.
 * @details Эквивалентно проверке статуса регистрации 1 (домашняя сеть) или 5 (роуминг). Использует последнее полученное значение.
 */
    bool isNetworkConnected() const { return _network.isNetworkConnected(); }
/**
 * @brief Показывает, завершилась ли полная последовательность инициализации модема успешно.
 * @return true — базовая настройка, SIM и SMS готовы; false — инициализация не завершена или завершилась ошибкой.
 * @details Не путать с isAvailable(): наличие ответа на AT ещё не означает готовность SIM-карты и SMS.
 */
    bool isInitDone() const { return _driver.isInitDone(); }   // full success (SIM+SMS)
/**
 * @brief Проверяет, готова ли SIM-карта к работе без запроса PIN/PUK.
 * @return true — модем сообщил CPIN: READY; false — SIM не готова или статус ещё не получен.
 * @details Возвращает последнее состояние, полученное при инициализации или checkSim().
 */
    bool isSimReady() const { return _driver.isSimReady(); }
/**
 * @brief Проверяет, выполнена ли настройка модема для работы с SMS.
 * @return true — режим SMS и уведомления настроены; false — настройка не завершена или завершилась ошибкой.
 * @details Это состояние настройки SMS, а не наличие свободной памяти для сообщений.
 */
    bool isSmsReady() const { return _driver.isSmsReady(); }
/**
 * @brief Проверяет, выполняется ли сейчас команда и ожидается ли ответ модема.
 * @return true — есть активная команда; false — активной команды нет.
 * @details Не учитывает только наличие команд в очереди; для очереди используйте queuedCount().
 */
    bool isBusy() const { return _driver.isBusy(); }
/**
 * @brief Возвращает состояние ядра очереди команд.
 * @return CoreState::STOPPED, CoreState::IDLE или CoreState::WAITING_RESPONSE.
 * @details WAITING_RESPONSE означает, что команда уже отправлена и ожидается её завершение.
 */
    CoreState state() const { return _core.state(); }
/**
 * @brief Возвращает количество AT-команд, ожидающих отправки.
 * @return Количество команд в очереди (от 0 до CORE_QUEUE_CAPACITY).
 * @details Не включает команду, которая уже отправлена модему и ожидает ответа.
 */
    uint8_t queuedCount() const { return _core.queuedCount(); }

/**
 * @brief Возвращает структуру с последней ошибкой, включая Result, CME/CMS, контекст и текст.
 * @return Ссылка на внутренние данные.
 * @details Константная ссылка на внутреннюю структуру ModemError. Она обновляется при следующей ошибке.
 */
    const ModemError& lastError() const { return _driver.lastError(); }
/**
 * @brief Возвращает готовый текст последней ошибки для вывода в журнал или интерфейс.
 * @return Указатель на строку.
 * @details Указатель на внутреннюю строку; она может измениться после следующей ошибки.
 */
    const char* lastErrorText() const { return _driver.lastErrorText(); }
/**
 * @brief Возвращает подробный результат последней попытки инициализации.
 * @return Ссылка на внутренние данные.
 * @details Константная ссылка на InitResult с отдельными флагами доступности модема, SIM и SMS.
 */
    const InitResult& lastInitResult() const { return _driver.lastInitResult(); }

    // Cached network status; getters are non-blocking and do not enqueue commands.
/**
 * @brief Возвращает последнее измеренное значение качества сигнала CSQ.
 * @return RSSI из ответа +CSQ (0–31); -1 означает, что достоверных данных пока нет.
 * @details Значение кешируется. Метод не выполняет запрос к модему.
 */
    int8_t signalQuality() const { return _network.signalQuality(); }
/**
 * @brief Возвращает оценку уровня сигнала в dBm по последнему ответу CSQ.
 * @return Оценка уровня сигнала в dBm; -1 означает, что данных нет.
 * @details Значение вычисляется из RSSI и обновляется при получении нового ответа модема.
 */
    int8_t signalDbm() const { return _network.signalDbm(); }
/**
 * @brief Возвращает числовой статус регистрации в мобильной сети (+CREG).
 * @return Код статуса: 0 — нет регистрации; 1 — домашняя сеть; 2 — поиск; 3 — отказ; 4 — неизвестно; 5 — роуминг.
 * @details Возвращает кешированное значение, не отправляет AT+CREG? самостоятельно.
 */
    uint8_t registrationStatus() const { return _network.registrationStatus(); }
/**
 * @brief Возвращает русское текстовое описание последнего статуса регистрации в сети.
 * @return Указатель на строку.
 * @details Указатель на строку, соответствующую коду +CREG; новое значение формируется по кешированному статусу.
 */
    const char* registrationStatusText() const { return _network.registrationStatusText(); }
/**
 * @brief Проверяет, зарегистрирован ли модем в мобильной сети.
 * @return true — статус 1 (домашняя сеть) или 5 (роуминг); иначе false.
 * @details Проверка выполняется по последнему полученному статусу регистрации.
 */
    bool isRegistered() const { return _network.isRegistered(); }
/**
 * @brief Проверяет, получались ли от модема корректные данные об уровне сигнала.
 * @return true — данные CSQ уже получены; false — данных пока нет.
 * @details Позволяет отличить нулевой/низкий уровень сигнала от ещё не выполненного измерения.
 */
    bool hasSignalData() const { return _network.hasSignalData(); }
/**
 * @brief Проверяет, получен ли хотя бы один корректный ответ о регистрации в сети.
 * @return true — статус регистрации известен; false — ответ ещё не получен.
 * @details Не означает, что модем зарегистрирован: для этого используйте isRegistered().
 */
    bool hasRegistrationData() const { return _network.hasRegistrationData(); }
/**
 * @brief Возвращает название оператора, полученное при последнем запросе AT+COPS?.
 * @return Указатель на строку.
 * @details Указатель на кешированную строку; пустая строка означает, что название ещё не получено.
 */
    const char* operatorName() const { return _network.operatorName(); }
/**
 * @brief Проверяет, получено ли название оператора мобильной сети.
 * @return true — данные оператора получены; false — данных пока нет.
 * @details Не отправляет AT-команду.
 */
    bool hasOperatorData() const { return _network.hasOperatorData(); }
/**
 * @brief Возвращает последний статус SIM-карты, например READY или SIM PIN.
 * @return Указатель на строку.
 * @details Указатель на внутреннюю строку, обновляемую после ответа модема.
 */
    const char* simStatus() const { return _network.simStatus(); }
/**
 * @brief Возвращает последнюю принятую строку ответа модема.
 * @return Указатель на строку.
 * @details Указатель на внутренний буфер; содержимое меняется при поступлении новых строк.
 */
    const char* lastResponse() const { return _network.lastResponse(); }
/**
 * @brief Возвращает время получения последней строки ответа модема.
 * @return Значение millis() на момент последнего ответа; 0 — ответов ещё не было.
 * @details Используйте разность unsigned-значений millis() для проверки возраста.
 */
    uint32_t lastResponseTime() const { return _network.lastResponseTime(); }
/**
 * @brief Возвращает время последнего обновления данных уровня сигнала.
 * @return Время millis() последнего обновления; 0 — измерения ещё не было.
 * @details Позволяет оценить актуальность кешированного значения signalQuality().
 */
    uint32_t lastSignalUpdate() const { return _network.lastSignalUpdate(); }
/**
 * @brief Возвращает время последнего обновления статуса регистрации.
 * @return Время millis() последнего обновления; 0 — статус ещё не получен.
 * @details Позволяет оценить актуальность registrationStatus().
 */
    uint32_t lastRegistrationUpdate() const { return _network.lastRegistrationUpdate(); }
/**
 * @brief Возвращает время последнего обновления названия оператора.
 * @return Время millis() последнего обновления; 0 — данные ещё не получены.
 * @details Позволяет оценить актуальность operatorName().
 */
    uint32_t lastOperatorUpdate() const { return _network.lastOperatorUpdate(); }
/**
 * @brief Возвращает время последнего обновления статуса SIM-карты.
 * @return Время millis() последнего обновления; 0 — статус ещё не получен.
 * @details Позволяет оценить актуальность simStatus().
 */
    uint32_t lastSimUpdate() const { return _network.lastSimUpdate(); }

    // Network status queries (periodic, non-blocking).
/**
 * @brief Ставит в очередь запрос текущего состояния SIM-карты (AT+CPIN?).
 * @param request Контекст запроса, включая источник вызова.
 * @return Result::OK — запрос поставлен в очередь; другое значение — причина отказа.
 * @details Метод асинхронный. Результат поступит через callback onSimStatus; сам метод не ждёт ответа модема.
 */
    Result checkSim(const RequestInfo& request = RequestInfo()) {
        return _network.checkSim(request);
    }
/**
 * @brief Ставит в очередь запрос статуса регистрации в сети (AT+CREG?).
 * @param request Контекст запроса, включая источник вызова.
 * @return Result::OK — запрос поставлен в очередь; другое значение — причина отказа.
 * @details Новый статус будет сохранён в кеше и передан через onRegistration.
 */
    Result checkRegistration(const RequestInfo& request = RequestInfo()) {
        return _network.checkRegistration(request);
    }
/**
 * @brief Ставит в очередь измерение уровня сигнала (AT+CSQ).
 * @param request Контекст запроса, включая источник вызова.
 * @return Result::OK — запрос поставлен в очередь; другое значение — причина отказа.
 * @details Результат будет сохранён в кеше и передан через onSignal.
 */
    Result checkSignal(const RequestInfo& request = RequestInfo()) {
        return _network.checkSignal(request);
    }
/**
 * @brief Ставит в очередь запрос названия оператора (AT+COPS?).
 * @param request Контекст запроса, включая источник вызова.
 * @return Result::OK — запрос поставлен в очередь; другое значение — причина отказа.
 * @details Метод не ждёт ответа. Название оператора обновится асинхронно.
 */
    Result checkOperator(const RequestInfo& request = RequestInfo()) {
        return _network.checkOperator(request);
    }

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
                   CommandPriority priority = CommandPriority::P_NORMAL) {
        return _driver.enqueue(cmd, request, timeoutMs, priority);
    }

/**
 * @brief Записывает произвольные данные в последовательный порт без формирования AT-команды.
 * @param data Указатель на строку или буфер отправляемых байтов.
 * @param length Размер бинарного буфера в байтах; используется в перегрузке с uint8_t*.
 * @return true — запись выполнена; false — поток недоступен или данные не могут быть записаны.
 * @details Не добавляет CR/LF и не создаёт запись в очереди AT-команд.
 */
    bool writeRaw(const char* data) { return _driver.writeRaw(data); }
/**
 * @brief Записывает произвольные данные в последовательный порт без формирования AT-команды.
 * @param data Указатель на строку или буфер отправляемых байтов.
 * @param length Размер бинарного буфера в байтах; используется в перегрузке с uint8_t*.
 * @return true — запись выполнена; false — поток недоступен или данные не могут быть записаны.
 * @details Не добавляет CR/LF и не создаёт запись в очереди AT-команд.
 */
    bool writeRaw(const uint8_t* data, size_t length) {
        return _driver.writeRaw(data, length);
    }

/**
 * @brief Устанавливает политику автоматического удаления обработанных SMS.
 * @param enabled true — удалять обработанные сообщения; false — оставлять в памяти модема.
 * @return Ничего не возвращает.
 * @details По умолчанию используется ручное хранение сообщений.
 */
    void setSmsAutoDelete(bool enabled) { _driver.setSmsAutoDelete(enabled); }
/**
 * @brief Показывает, включено ли автоматическое удаление обработанных SMS.
 * @return true — автоудаление включено; false — сообщения сохраняются.
 * @details Возвращает текущую настройку без обращения к модему.
 */
    bool isSmsAutoDelete() const { return _driver.isSmsAutoDelete(); }
/**
 * @brief Задаёт интервал автоматического сканирования памяти на новые SMS.
 * @param intervalMs Интервал в миллисекундах; 0 отключает периодическое сканирование.
 * @return Ничего не возвращает.
 * @details Следующее сканирование при ненулевом интервале может быть запущено сразу.
 */
    void setSmsScanInterval(uint32_t intervalMs) { _driver.setSmsScanInterval(intervalMs); }
/**
 * @brief Возвращает интервал периодического сканирования SMS.
 * @return Интервал в миллисекундах; 0 означает, что сканирование выключено.
 * @details Возвращает локальную настройку.
 */
    uint32_t smsScanInterval() const { return _driver.smsScanInterval(); }
/**
 * @brief Запускает асинхронное удаление одного SMS из памяти модема.
 * @param storage Тип памяти: "SM" — SIM-карта, "ME" — память модема.
 * @param index Индекс сообщения в выбранной памяти.
 * @return Result::OK — операция принята; INVALID_PARAM — неверная память; BUSY — канал занят; иначе причина отказа.
 * @details Удаление выполняется AT-командами в отдельной транзакции. Успешный возврат означает принятие операции, а не подтверждение фактического удаления.
 */
    Result deleteSMS(const char* storage, uint8_t index) { return _driver.deleteSMS(storage, index); }
/**
 * @brief Запускает асинхронное удаление всех SMS из выбранной памяти.
 * @param storage Тип памяти: "SM" — SIM-карта, "ME" — память модема.
 * @return Result::OK — операция принята; INVALID_PARAM — неверная память; BUSY — канал занят; иначе причина отказа.
 * @details Операция выполняется в отдельной транзакции и может занять время в зависимости от модема.
 */
    Result deleteAllSMS(const char* storage) { return _driver.deleteAllSMS(storage); }
/**
 * @brief Запрашивает количество занятых и свободных ячеек SMS в выбранной памяти.
 * @param storage Тип памяти: "SM" — SIM-карта, "ME" — память модема.
 * @return Result::OK — запрос принят; INVALID_PARAM — неверная память; BUSY — канал занят; иначе причина отказа.
 * @details Результат приходит асинхронно через onSmsMemory.
 */
    Result checkSmsMemory(const char* storage) { return _driver.checkSmsMemory(storage); }

/**
 * @brief Отправляет одноразовый USSD-запрос оператору мобильной связи.
 * @param code USSD-код, например "*100#". Не должен содержать кавычки, обратную косую черту или управляющие символы.
 * @param dcs Кодировка USSD-строки; по умолчанию 15 (GSM 7-bit), для UCS2 обычно 72.
 * @param request Контекст запроса, который будет передан вместе с ответом.
 * @return Result::OK — запрос принят; BUSY — уже ожидается другой USSD-ответ; INVALID_PARAM — неверный код; иначе причина отказа.
 * @details Ответ приходит асинхронно через onUssdResponse. Одновременно поддерживается один незавершённый USSD-запрос.
 */
    Result sendUSSD(const char* code, uint8_t dcs = 15,
                    const RequestInfo& request = RequestInfo()) {
        return _driver.sendUSSD(code, dcs, request);
    }

/**
 * @brief Отправляет SMS или добавляет его в очередь исходящих сообщений.
 * @param number Номер получателя в международном формате; допускаются цифры и начальный знак +.
 * @param text Текст сообщения в UTF-8. Длинный текст автоматически разбивается на составные SMS.
 * @param request Контекст отправки, включая источник сообщения.
 * @return Result::OK — сообщение отправлено в обработку/очередь; INVALID_PARAM — неверный номер или текст; NOT_READY — модем не запущен; QUEUE_FULL — очередь заполнена; BUSY — занята внутренняя транзакция.
 * @details Отправка асинхронная. Итог каждой отправки передаётся через onSmsSent. Максимальная длина ограничена SMS_MAX_UTF8_BYTES и лимитом UTF-16.
 */
    Result sendSMS(const char* number, const char* text,
                   const RequestInfo& request = RequestInfo()) {
        return _driver.sendSMS(number, text, request);
    }
/**
 * @brief Возвращает количество исходящих SMS, ожидающих своей очереди.
 * @return Количество ожидающих сообщений.
 * @details Не включает сообщение, отправка которого уже выполняется.
 */
    uint8_t smsSendQueueCount() const { return _driver.smsSendQueueCount(); }
/**
 * @brief Показывает, выполняется ли сейчас отправка исходящего SMS.
 * @return true — отправка выполняется; false — отправки нет.
 * @details Для результата отправки используйте callback onSmsSent.
 */
    bool isSmsSendActive() const { return _driver.isSmsSendActive(); }
/**
 * @brief Очищает очередь SMS, ожидающих отправки.
 * @return Ничего не возвращает.
 * @details Текущее сообщение, если оно уже отправляется, не прерывается.
 */
    void clearSmsSendQueue() { _driver.clearSmsSendQueue(); }

/**
 * @brief Отправляет модему команду ответа на входящий звонок (ATA).
 * @param request Контекст операции, включая источник вызова.
 * @return Result::OK — команда принята в очередь; иначе причина отказа.
 * @details Метод не ждёт фактического соединения. Состояние звонка обновляется асинхронно через onCallState/onCallAnswered.
 */
    Result answerCall(const RequestInfo& request = RequestInfo()) {
        return _driver.answerCall(request);
    }
/**
 * @brief Отправляет модему команду завершения текущего звонка (ATH).
 * @param request Контекст операции, включая источник вызова.
 * @return Result::OK — команда принята в очередь; иначе причина отказа.
 * @details Фактическое завершение вызова отслеживается асинхронно по состоянию звонка.
 */
    Result hangUp(const RequestInfo& request = RequestInfo()) {
        return _driver.hangUp(request);
    }
/**
 * @brief Набирает указанный телефонный номер.
 * @param number Номер абонента: цифры и необязательный + в начале; длина не более 20 символов.
 * @param request Контекст операции, включая источник вызова.
 * @return Result::OK — команда набора принята; INVALID_PARAM — неверный номер; иначе причина отказа.
 * @details Вызов выполняется асинхронно. Состояние DIALING и последующие изменения передаются через onCallState.
 */
    Result dial(const char* number, const RequestInfo& request = RequestInfo()) {
        return _driver.dial(number, request);
    }

/**
 * @brief Включает или отключает механизм контроля доступности модема.
 * @param enabled true — включить; false — выключить.
 * @return Ничего не возвращает.
 * @details При потере связи драйвер может вызвать onRecoveryRequired, чтобы приложение выполнило аппаратный сброс.
 */
    void setRecoveryEnabled(bool enabled) { _driver.setRecoveryEnabled(enabled); }
/**
 * @brief Показывает, включён ли контроль доступности модема.
 * @return true — контроль включён; false — отключён.
 * @details Не выполняет проверку связи самостоятельно.
 */
    bool isRecoveryEnabled() const { return _driver.isRecoveryEnabled(); }
/**
 * @brief Устанавливает период между контрольными проверками модема.
 * @param intervalMs Интервал между проверками в миллисекундах.
 * @return Ничего не возвращает.
 * @details Проверки выполняются неблокирующе из process().
 */
    void setRecoveryTimeout(uint32_t intervalMs) { _driver.setRecoveryTimeout(intervalMs); }
/**
 * @brief Возвращает интервал между проверками доступности модема.
 * @return Интервал в миллисекундах.
 * @details Возвращает текущую настройку механизма восстановления.
 */
    uint32_t recoveryTimeout() const { return _driver.recoveryTimeout(); }
/**
 * @brief Ограничивает максимальное число попыток контроля/восстановления связи.
 * @param attempts Максимальное число попыток; 0 заменяется на 1.
 * @return Ничего не возвращает.
 * @details Для нового цикла восстановления используйте resetRecoveryAttempts().
 */
    void setMaxRecoveryAttempts(uint8_t attempts) { _driver.setMaxRecoveryAttempts(attempts); }
/**
 * @brief Возвращает количество выполненных попыток восстановления.
 * @return Количество попыток с момента последнего сброса.
 * @details Счётчик не является числом успешных восстановлений.
 */
    uint8_t recoveryAttempts() const { return _driver.recoveryAttempts(); }
/**
 * @brief Сбрасывает счётчик попыток восстановления модема.
 * @return Ничего не возвращает.
 * @details Используйте после ручного перезапуска или восстановления питания модема.
 */
    void resetRecoveryAttempts() { _driver.resetRecoveryAttempts(); }
/**
 * @brief Возвращает текущее состояние голосового вызова.
 * @return Значение CallState: IDLE, RINGING, ANSWERING, ACTIVE, DIALING, ENDED, BUSY или FAILED.
 * @details Состояние обновляется асинхронно по событиям модема.
 */
    CallState callState() const { return _driver.callState(); }

/**
 * @brief Предоставляет доступ к объекту ядра очереди AT-команд.
 * @return Ссылка на внутренний объект Core.
 * @details Используется для расширенного управления очередью и транзакциями. Обычно достаточно методов фасада GSM.
 */
    Core&    core() { return _core; }
/**
 * @brief Предоставляет доступ к низкоуровневому драйверу SIM800L.
 * @return Ссылка на внутренний объект SIM800L.
 * @details Нужен для доступа к функциям драйвера, не вынесенным в фасад GSM.
 */
    SIM800L& driver() { return _driver; }
/**
 * @brief Предоставляет доступ к объекту сетевого мониторинга.
 * @return Ссылка на внутренний объект Network.
 * @details Содержит методы опроса и чтения кешированных параметров сети.
 */
    Network& network() { return _network; }

private:
    Core    _core;
    SIM800L _driver;
    Network _network;
};

} // namespace gsm
