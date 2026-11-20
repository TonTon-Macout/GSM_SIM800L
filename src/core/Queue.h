#pragma once
#include <Arduino.h>
#include <string.h>
#include "Types.h"

namespace gsm {

struct QueuedCommand {
    char            text[CORE_COMMAND_SIZE];
    RequestInfo     request;
    uint16_t        timeoutMs;
    CommandPriority priority;
    uint32_t        order;
};

// Fixed-size priority queue. Equal priorities remain FIFO.
class Queue {
public:
    Queue() : _count(0), _order(0) {}

/**
 * @brief Возвращает количество команд, находящихся в очереди.
 * @return Число ожидающих команд.
 * @details Не включает уже извлечённую активную команду.
 */
    uint8_t size() const { return _count; }
/**
 * @brief Проверяет, пуста ли очередь команд.
 * @return true — очередь пуста; false — в очереди есть команды.
 * @details Эквивалентно size() == 0.
 */
    bool    empty() const { return _count == 0; }
/**
 * @brief Проверяет, достигла ли очередь максимальной вместимости.
 * @return true — очередь заполнена; false — есть свободные места.
 * @details Вместимость задаётся CORE_QUEUE_CAPACITY.
 */
    bool    full() const { return _count >= CORE_QUEUE_CAPACITY; }

/**
 * @brief Добавляет AT-команду в очередь с указанным приоритетом.
 * @param command Команда без завершающих CR/LF.
 * @param request Контекст команды.
 * @param timeoutMs Таймаут ожидания ответа в миллисекундах.
 * @param priority Приоритет команды.
 * @return true — команда добавлена; false — пустая/слишком длинная команда или очередь заполнена.
 * @details Команды с более высоким приоритетом располагаются раньше. При одинаковом приоритете сохраняется FIFO-порядок.
 */
    bool push(const char* command,
              const RequestInfo& request,
              uint16_t timeoutMs,
              CommandPriority priority) {
        if (!command || !command[0] || full()) return false;

        const size_t len = strlen(command);
        if (len >= CORE_COMMAND_SIZE) return false;

        QueuedCommand item;
        memcpy(item.text, command, len + 1);
        item.request   = request;
        item.timeoutMs = timeoutMs;
        item.priority  = priority;
        item.order     = _order++;

        uint8_t pos = _count;
        while (pos > 0) {
            const QueuedCommand& prev = _items[pos - 1];
            if (static_cast<uint8_t>(prev.priority) >=
                static_cast<uint8_t>(item.priority)) break;
            _items[pos] = prev;
            --pos;
        }
        _items[pos] = item;
        ++_count;
        return true;
    }

/**
 * @brief Извлекает следующую команду из очереди.
 * @param item Структура, в которую будет скопирована извлечённая команда.
 * @return true — команда получена; false — очередь пуста.
 * @details Извлечённая команда удаляется из очереди.
 */
    bool pop(QueuedCommand& item) { return popMatching(item, 0, false); }

    // When a transaction owns the command channel, only its commands may run.
/**
 * @brief Извлекает следующую команду, принадлежащую указанной транзакции.
 * @param item Структура для извлечённой команды.
 * @param transactionId Идентификатор активной транзакции.
 * @return true — подходящая команда найдена и извлечена; false — такой команды в очереди нет.
 * @details Остальные команды остаются в очереди до завершения эксклюзивной транзакции.
 */
    bool popForTransaction(QueuedCommand& item, uint16_t transactionId) {
        return popMatching(item, transactionId, true);
    }

/**
 * @brief Удаляет все команды, ожидающие выполнения.
 * @return Ничего не возвращает.
 * @details Не влияет на команду, которая уже выполняется.
 */
    void clear() { _count = 0; }

private:
    bool popMatching(QueuedCommand& item, uint16_t transactionId, bool matchTransaction) {
        if (_count == 0) return false;
        uint8_t pos = 0;
        if (matchTransaction) {
            while (pos < _count && _items[pos].request.transactionId != transactionId) ++pos;
            if (pos >= _count) return false;
        }
        item = _items[pos];
        for (uint8_t i = pos + 1; i < _count; ++i) _items[i - 1] = _items[i];
        --_count;
        return true;
    }

    QueuedCommand _items[CORE_QUEUE_CAPACITY];
    uint8_t       _count;
    uint32_t      _order;
};

} // namespace gsm
