#pragma once
#include <Arduino.h>
#include "Types.h"
#include "Parser.h"
#include "Queue.h"

namespace gsm {

struct CoreCallbacks {
    // Every complete modem line, before classification.
    void (*onRawLine)(const char* line);

    // Recognised unsolicited result codes (URC).
    void (*onURC)(const char* line);

    // Ordinary response lines belonging to the active command.
    void (*onResponseLine)(const char* line, const RequestInfo& request);

    // Command finished with OK / ERROR / timeout.
    void (*onCommandComplete)(const CommandResult& result);

    // Modem SMS/data prompt ('>').
    void (*onPrompt)(const RequestInfo& request);

    // Lines that are neither a recognised URC nor part of a command.
    void (*onUnsolicitedLine)(const char* line);
};

class Core {
public:
/**
 * @brief Создаёт ядро обмена AT-командами через указанный последовательный порт.
 * @details Перед использованием настройте UART и необходимые параметры приложения.
 */
    explicit Core(Stream& serial);

/**
 * @brief Запускает ядро обмена AT-командами и подготавливает внутренний парсер.
 * @return Ничего не возвращает.
 * @details Вызывать после настройки UART и до постановки AT-команд в очередь.
 */
    void begin();
/**
 * @brief Останавливает ядро обмена AT-командами.
 * @return Ничего не возвращает.
 * @details Останавливает дальнейшую обработку и отправку команд. Для возобновления вызовите begin().
 */
    void stop();
/**
 * @brief Обрабатывает входящие байты, таймауты и очередь AT-команд.
 * @return Ничего не возвращает.
 * @details Вызывать регулярно из loop(); метод не блокирует выполнение в ожидании ответа.
 */
    void process();

/**
 * @brief Проверяет, запущено ли ядро обмена с модемом.
 * @return true — ядро запущено; false — остановлено.
 * @details После stop() необходимо снова вызвать begin().
 */
    bool      isRunning() const { return _running; }
/**
 * @brief Проверяет, выполняется ли сейчас команда и ожидается ли ответ модема.
 * @return true — есть активная команда; false — активной команды нет.
 * @details Не учитывает только наличие команд в очереди; для очереди используйте queuedCount().
 */
    bool      isBusy() const { return _active; }
/**
 * @brief Возвращает количество AT-команд, ожидающих отправки.
 * @return Количество команд в очереди (от 0 до CORE_QUEUE_CAPACITY).
 * @details Не включает команду, которая уже отправлена модему и ожидает ответа.
 */
    uint8_t   queuedCount() const { return _queue.size(); }
/**
 * @brief Возвращает состояние ядра очереди команд.
 * @return CoreState::STOPPED, CoreState::IDLE или CoreState::WAITING_RESPONSE.
 * @details WAITING_RESPONSE означает, что команда уже отправлена и ожидается её завершение.
 */
    CoreState state() const;

/**
 * @brief Устанавливает обработчики событий ядра AT-команд.
 * @param callbacks Структура CoreCallbacks с функциями обработки строк, ответов, приглашений и завершения команд.
 * @return Ничего не возвращает.
 * @details Неиспользуемые указатели должны быть nullptr. Callback вызываются во время process().
 */
    void setCallbacks(const CoreCallbacks& callbacks);

    // Enqueue one AT command (no trailing CR/LF).
/**
 * @brief Добавляет произвольную AT-команду в очередь выполнения.
 * @param command Текст AT-команды без завершающих CR/LF.
 * @param request Контекст команды: ID, источник и ID транзакции.
 * @param timeoutMs Таймаут ожидания ответа модема, мс; по умолчанию 3000.
 * @param priority Приоритет команды; более высокий приоритет отправляется раньше.
 * @return Result::OK — команда принята; INVALID_PARAM — команда некорректна; QUEUE_FULL — очередь заполнена; другие значения описывают причину отказа.
 * @details Команда не отправляется немедленно, если канал занят. Ответ и завершение отслеживайте через callback команды.
 */
    Result enqueue(const char* command,
                   const RequestInfo& request = RequestInfo(),
                   uint16_t timeoutMs = 3000,
                   CommandPriority priority = CommandPriority::P_NORMAL);

    // Write payload for multi-step operations (e.g. SMS body after '>').
    // Does not append CR/LF and does not create a new queued command.
/**
 * @brief Передаёт бинарный буфер байтов в UART без формирования AT-команды.
 * @param data Указатель на массив отправляемых байтов.
 * @param length Количество байтов для отправки.
 * @return true — данные записаны в поток; false — передача не выполнена.
 * @details Не добавляет CR/LF и не создаёт новую команду в очереди.
 */
    bool writeRaw(const uint8_t* data, size_t length);
/**
 * @brief Передаёт текстовую строку в UART без формирования AT-команды.
 * @param data Нуль-терминированная строка для отправки.
 * @return true — строка записана в поток; false — передача не выполнена.
 * @details Не добавляет CR/LF и не создаёт новую команду в очереди.
 */
    bool writeRaw(const char* data);

    // Exclusive command sequence. While active, only commands tagged with this ID run.
/**
 * @brief Резервирует канал команд для одной составной операции.
 * @param transactionId Ненулевой идентификатор транзакции.
 * @return true — канал зарезервирован; false — канал уже зарезервирован или ID недопустим.
 * @details Пока транзакция активна, выполняются только команды с таким же transactionId.
 */
    bool beginTransaction(uint16_t transactionId);
/**
 * @brief Освобождает канал команд, ранее зарезервированный транзакцией.
 * @param transactionId Идентификатор транзакции.
 * @return Ничего не возвращает.
 * @details Переданный ID должен совпадать с ID активной транзакции.
 */
    void endTransaction(uint16_t transactionId);
/**
 * @brief Проверяет, удерживается ли канал AT-команд эксклюзивной транзакцией.
 * @return true — транзакция активна; false — канал свободен.
 * @details Не проверяет наличие обычной активной AT-команды.
 */
    bool transactionActive() const { return _transactionId != 0; }

    // Clears waiting commands. Active command is not interrupted.
/**
 * @brief Удаляет все ожидающие AT-команды из очереди.
 * @return Ничего не возвращает.
 * @details Активная команда, уже отправленная модему, не прерывается.
 */
    void clearQueue();

private:
    struct ActiveCommand {
        QueuedCommand command;
        uint32_t      startedAt;
        bool          valid;
    };

    Stream&       _serial;
    Parser        _parser;
    Queue         _queue;
    CoreCallbacks _callbacks;
    ActiveCommand _activeCommand;
    bool          _running;
    bool          _active;
    uint16_t      _transactionId;

    void startNextCommand();
    void handleLine(const char* line);
    void handlePrompt();
    bool isTerminalOK(const char* line) const;
    bool isTerminalError(const char* line) const;
    bool isAlwaysURC(const char* line) const;
    bool isPotentialURC(const char* line) const;
    void finishActive(Result result);
};

} // namespace gsm
