#pragma once
#include <Arduino.h>
#include "Types.h"

namespace gsm {

// Incremental line parser. Never blocks. Owns one fixed buffer.
class Parser {
public:
    Parser();

    // Returns true when a complete non-empty line is ready.
    // Pointer stays valid until the next feed() call.
/**
 * @brief Передаёт очередной байт в парсер входящих данных модема.
 * @param value Принятый байт из последовательного порта.
 * @param completedLine Сюда записывается указатель на готовую строку, если она сформирована.
 * @return true — получена полная непустая строка; false — строка ещё не завершена.
 * @details Парсер собирает данные до CR/LF. Указатель на completedLine действителен до следующего вызова feed(). Метод не выделяет динамическую память.
 */
    bool feed(uint8_t value, const char*& completedLine);

    // Modem prompt '>' is not terminated by CR/LF.
/**
 * @brief Проверяет, обнаружено ли приглашение модема к вводу данных, например символ > при отправке SMS.
 * @return true — приглашение было обнаружено и признак сброшен; false — приглашения нет.
 * @details Приглашение не заканчивается CR/LF, поэтому обрабатывается отдельно от обычных строк.
 */
    bool takePrompt();

/**
 * @brief Сбрасывает внутреннее состояние парсера входящих строк.
 * @return Ничего не возвращает.
 * @details Очищает текущую незавершённую строку и признаки специальных состояний.
 */
    void reset();

private:
    char    _line[CORE_LINE_SIZE];
    uint16_t _length;
    bool    _prompt;
    bool    _skipPromptSpace;
    bool    _overflow;
};

} // namespace gsm
