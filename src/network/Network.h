#pragma once
#include <Arduino.h>
#include "driver/SIM800L.h"

namespace gsm {

// Thin convenience layer over SIM800L status queries.
// All methods are asynchronous; results arrive via ModemCallbacks.
class Network {
public:
/**
 * @brief Создаёт интерфейс сетевых запросов поверх драйвера модема.
 * @param driver Ссылка на уже созданный объект SIM800L, который будет выполнять AT-команды.
 * @details Объект Network не владеет драйвером; SIM800L должен существовать всё время жизни Network.
 */
    explicit Network(SIM800L& driver) : _driver(driver) {}

/**
 * @brief Ставит в очередь запрос текущего состояния SIM-карты (AT+CPIN?).
 * @param request Контекст запроса, включая источник вызова.
 * @return Result::OK — запрос поставлен в очередь; другое значение — причина отказа.
 * @details Метод асинхронный. Результат поступит через callback onSimStatus; сам метод не ждёт ответа модема.
 */
    Result checkSim(const RequestInfo& request = RequestInfo()) {
        return _driver.checkSim(request);
    }

/**
 * @brief Ставит в очередь запрос статуса регистрации в сети (AT+CREG?).
 * @param request Контекст запроса, включая источник вызова.
 * @return Result::OK — запрос поставлен в очередь; другое значение — причина отказа.
 * @details Новый статус будет сохранён в кеше и передан через onRegistration.
 */
    Result checkRegistration(const RequestInfo& request = RequestInfo()) {
        return _driver.checkRegistration(request);
    }

/**
 * @brief Ставит в очередь измерение уровня сигнала (AT+CSQ).
 * @param request Контекст запроса, включая источник вызова.
 * @return Result::OK — запрос поставлен в очередь; другое значение — причина отказа.
 * @details Результат будет сохранён в кеше и передан через onSignal.
 */
    Result checkSignal(const RequestInfo& request = RequestInfo()) {
        return _driver.checkSignal(request);
    }

/**
 * @brief Ставит в очередь запрос названия оператора (AT+COPS?).
 * @param request Контекст запроса, включая источник вызова.
 * @return Result::OK — запрос поставлен в очередь; другое значение — причина отказа.
 * @details Метод не ждёт ответа. Название оператора обновится асинхронно.
 */
    Result checkOperator(const RequestInfo& request = RequestInfo()) {
        return _driver.checkOperator(request);
    }

/**
 * @brief Включает автоматический периодический опрос состояния SIM-карты и мобильной сети.
 * @return Ничего не возвращает.
 * @details Результаты обновляют кеш и передаются через соответствующие callback. Для работы необходимо регулярно вызывать process().
 */
    void startMonitoring() { _driver.startMonitoring(); }
/**
 * @brief Выключает автоматический периодический опрос состояния модема.
 * @return Ничего не возвращает.
 * @details Уже отправленная AT-команда не прерывается; прекращается запуск следующих периодических проверок.
 */
    void stopMonitoring() { _driver.stopMonitoring(); }
/**
 * @brief Проверяет, включён ли автоматический мониторинг сети.
 * @return true — мониторинг включён; false — выключен.
 * @details Не отправляет AT-команду, возвращает внутренний флаг.
 */
    bool isMonitoring() const { return _driver.isMonitoring(); }
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
        _driver.setPollIntervals(simMs, registrationMs, signalMs, operatorMs);
    }

    // Last asynchronously received network values; getters do not send AT commands.
/**
 * @brief Возвращает последнее измеренное значение качества сигнала CSQ.
 * @return RSSI из ответа +CSQ (0–31); -1 означает, что достоверных данных пока нет.
 * @details Значение кешируется. Метод не выполняет запрос к модему.
 */
    int8_t signalQuality() const { return _driver.signalQuality(); }
/**
 * @brief Возвращает оценку уровня сигнала в dBm по последнему ответу CSQ.
 * @return Оценка уровня сигнала в dBm; -1 означает, что данных нет.
 * @details Значение вычисляется из RSSI и обновляется при получении нового ответа модема.
 */
    int8_t signalDbm() const { return _driver.signalDbm(); }
/**
 * @brief Возвращает числовой статус регистрации в мобильной сети (+CREG).
 * @return Код статуса: 0 — нет регистрации; 1 — домашняя сеть; 2 — поиск; 3 — отказ; 4 — неизвестно; 5 — роуминг.
 * @details Возвращает кешированное значение, не отправляет AT+CREG? самостоятельно.
 */
    uint8_t registrationStatus() const { return _driver.registrationStatus(); }
    const char* registrationStatusText() const {
        return registrationStatusToString(_driver.registrationStatus());
    }
/**
 * @brief Проверяет, зарегистрирован ли модем в мобильной сети.
 * @return true — статус 1 (домашняя сеть) или 5 (роуминг); иначе false.
 * @details Проверка выполняется по последнему полученному статусу регистрации.
 */
    bool isRegistered() const { return _driver.isRegistered(); }
/**
 * @brief Проверяет, зарегистрирован ли модем в домашней сети или роуминге.
 * @return true — регистрация подтверждена; false — регистрации нет или состояние ещё неизвестно.
 * @details Эквивалентно проверке статуса регистрации 1 (домашняя сеть) или 5 (роуминг). Использует последнее полученное значение.
 */
    bool isNetworkConnected() const { return _driver.isNetworkConnected(); }
/**
 * @brief Проверяет, насколько давно модем последний раз успешно отвечал на AT-команду.
 * @param maxAgeMs Максимально допустимый возраст последнего успешного ответа, мс. По умолчанию 30000 мс (30 секунд).
 * @return true — успешный ответ получен не более maxAgeMs назад; false — ответ отсутствует или устарел.
 * @details Проверяет сохранённую отметку времени и не отправляет новую AT-команду.
 */
    bool isResponsive(uint32_t maxAgeMs = 30000) const { return _driver.isResponsive(maxAgeMs); }
/**
 * @brief Проверяет, получались ли от модема корректные данные об уровне сигнала.
 * @return true — данные CSQ уже получены; false — данных пока нет.
 * @details Позволяет отличить нулевой/низкий уровень сигнала от ещё не выполненного измерения.
 */
    bool hasSignalData() const { return _driver.hasSignalData(); }
/**
 * @brief Проверяет, получен ли хотя бы один корректный ответ о регистрации в сети.
 * @return true — статус регистрации известен; false — ответ ещё не получен.
 * @details Не означает, что модем зарегистрирован: для этого используйте isRegistered().
 */
    bool hasRegistrationData() const { return _driver.hasRegistrationData(); }
    const char* operatorName() const { return _driver.operatorName(); }
/**
 * @brief Проверяет, получено ли название оператора мобильной сети.
 * @return true — данные оператора получены; false — данных пока нет.
 * @details Не отправляет AT-команду.
 */
    bool hasOperatorData() const { return _driver.hasOperatorData(); }
    const char* simStatus() const { return _driver.simStatus(); }
    const char* lastResponse() const { return _driver.lastResponse(); }
/**
 * @brief Возвращает время получения последней строки ответа модема.
 * @return Значение millis() на момент последнего ответа; 0 — ответов ещё не было.
 * @details Используйте разность unsigned-значений millis() для проверки возраста.
 */
    uint32_t lastResponseTime() const { return _driver.lastResponseTime(); }
/**
 * @brief Возвращает время последнего обновления данных уровня сигнала.
 * @return Время millis() последнего обновления; 0 — измерения ещё не было.
 * @details Позволяет оценить актуальность кешированного значения signalQuality().
 */
    uint32_t lastSignalUpdate() const { return _driver.lastSignalUpdate(); }
/**
 * @brief Возвращает время последнего обновления статуса регистрации.
 * @return Время millis() последнего обновления; 0 — статус ещё не получен.
 * @details Позволяет оценить актуальность registrationStatus().
 */
    uint32_t lastRegistrationUpdate() const { return _driver.lastRegistrationUpdate(); }
/**
 * @brief Возвращает время последнего обновления названия оператора.
 * @return Время millis() последнего обновления; 0 — данные ещё не получены.
 * @details Позволяет оценить актуальность operatorName().
 */
    uint32_t lastOperatorUpdate() const { return _driver.lastOperatorUpdate(); }
/**
 * @brief Возвращает время последнего обновления статуса SIM-карты.
 * @return Время millis() последнего обновления; 0 — статус ещё не получен.
 * @details Позволяет оценить актуальность simStatus().
 */
    uint32_t lastSimUpdate() const { return _driver.lastSimUpdate(); }

private:
    SIM800L& _driver;
};

} // namespace gsm
