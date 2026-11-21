#pragma once
#include <Arduino.h>

namespace gsm {

static const uint8_t CORE_QUEUE_CAPACITY = 8;
static const uint8_t CORE_COMMAND_SIZE   = 64;
static const uint16_t CORE_LINE_SIZE     = 300; // supports UCS2 SMS HEX lines

enum class Source : uint8_t {
    INTERNAL,
    PERIODIC,
    WEB,
    LCD,
    BUTTON,
    SMS,
    CALL,
    EXTERNALE,
    TELEGRAMM
};

/**
 * @brief Контекст запроса, который сопровождает AT-команду и её callback.
 * @param requestId Идентификатор операции, по которому приложение/драйвер определяет назначение ответа.
 * @param source Источник запроса: WEB, LCD, SMS, CALL и другие значения Source.
 * @param transactionId Идентификатор эксклюзивной последовательности; 0 означает обычную команду.
 */
struct RequestInfo {
    uint16_t requestId;
    Source   source;
    uint16_t transactionId; // 0 = ordinary command, non-zero = exclusive sequence

    RequestInfo(uint16_t id = 0, Source src = Source::INTERNAL, uint16_t transaction = 0)
        : requestId(id), source(src), transactionId(transaction) {}
};

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

enum class CommandPriority : uint8_t {
    P_LOW      = 0,
    P_NORMAL   = 1,
    P_HIGH     = 2,
    P_CRITICAL = 3
};

enum class CoreState : uint8_t {
    STOPPED,
    IDLE,
    WAITING_RESPONSE
};

struct CommandResult {
    RequestInfo request;
    Result      result;
    uint32_t    elapsedMs;
};


/**
 * @brief Возвращает русское описание результата или ошибки.
 * @param r Код Result, полученный от метода библиотеки или callback.
 * @return Статическая строка на русском языке, пригодная для журнала и интерфейса.
 */
inline const char* errorToString(Result r) {
    switch (r) {
        case Result::OK:            return "Нет ошибки";
        case Result::ERROR:         return "Ошибка выполнения команды";
        case Result::TIMEOUT:       return "Таймаут ожидания ответа модема";
        case Result::CANCELLED:     return "Команда отменена";
        case Result::BUSY:          return "Модем занят";
        case Result::NOT_READY:     return "Модем или Core не запущен";
        case Result::QUEUE_FULL:    return "Очередь команд переполнена";
        case Result::INVALID_PARAM: return "Некорректный параметр команды";
        default:                    return "Неизвестная ошибка";
    }
}

/**
 * @brief Преобразует Result в короткий символьный код для отладочного журнала.
 * @param r Код результата операции.
 * @return Статическая строка: OK, ERROR, TIMEOUT и т. д.
 */
inline const char* resultToStr(Result r) {
    switch (r) {
        case Result::OK:            return "OK";
        case Result::ERROR:         return "ERROR";
        case Result::TIMEOUT:       return "TIMEOUT";
        case Result::CANCELLED:     return "CANCELLED";
        case Result::BUSY:          return "BUSY";
        case Result::NOT_READY:     return "NOT_READY";
        case Result::QUEUE_FULL:    return "QUEUE_FULL";
        case Result::INVALID_PARAM: return "INVALID_PARAM";
        default:                    return "UNKNOWN";
    }
}


/**
 * @brief Возвращает русское описание статуса регистрации из ответа +CREG.
 * @param status Числовой код статуса регистрации, полученный от модема.
 * @return Статическая строка с описанием домашней сети, роуминга, поиска или ошибки регистрации.
 */
inline const char* registrationStatusToString(uint8_t status) {
    switch (status) {
        case 0: return "Не зарегистрирован";
        case 1: return "Домашняя сеть";
        case 2: return "Поиск сети";
        case 3: return "Регистрация запрещена";
        case 4: return "Неизвестное состояние";
        case 5: return "Роуминг";
        default: return "Неизвестное состояние";
    }
}

/**
 * @brief Расшифровывает числовой код +CME ERROR модема.
 * @param code Числовой код CME из ответа модема.
 * @return Статическая строка с русским описанием кода; для неизвестного кода — общее описание CME.
 */
inline const char* cmeToStr(int code) {
    switch (code) {
        case 0:  return "Сбой телефона";
        case 1:  return "Нет соединения с телефоном";
        case 3:  return "Операция не разрешена";
        case 4:  return "Операция не поддерживается";
        case 10: return "SIM-карта не установлена";
        case 11: return "Требуется PIN-код SIM-карты";
        case 12: return "Требуется PUK-код SIM-карты";
        case 13: return "Ошибка SIM-карты";
        case 14: return "SIM-карта занята";
        case 15: return "Неверная SIM-карта";
        case 16: return "Неверный пароль";
        case 17: return "Требуется PIN2-код SIM-карты";
        case 18: return "Требуется PUK2-код SIM-карты";
        case 20: return "Память заполнена";
        case 21: return "Неверный индекс";
        case 22: return "Не найдено";
        case 23: return "Ошибка памяти";
        case 24: return "Слишком длинный текст";
        case 25: return "Недопустимые символы в тексте";
        case 26: return "Слишком длинная строка набора";
        case 27: return "Недопустимые символы в номере";
        case 30: return "Нет обслуживания сети";
        case 31: return "Таймаут сети";
        case 32: return "Сеть не разрешена";
        case 40: return "Требуется PIN-код сетевой персонализации";
        case 100: return "Неизвестная ошибка";
        default: return "Ошибка CME";
    }
}

/**
 * @brief Расшифровывает числовой код +CMS ERROR, относящийся к SMS.
 * @param code Числовой код CMS из ответа модема.
 * @return Статическая строка с русским описанием кода; для неизвестного кода — общее описание CMS.
 */
inline const char* cmsToStr(int code) {
    switch (code) {
        case 300: return "Сбой оборудования модема";
        case 301: return "Служба SMS зарезервирована";
        case 302: return "Операция не разрешена";
        case 303: return "Операция не поддерживается";
        case 304: return "Неверный параметр режима PDU";
        case 305: return "Неверный параметр текстового режима";
        case 310: return "SIM-карта не установлена";
        case 311: return "Требуется PIN-код SIM-карты";
        case 312: return "Требуется PIN-код телефона для SIM";
        case 313: return "Ошибка SIM-карты";
        case 314: return "SIM-карта занята";
        case 315: return "Неверная SIM-карта";
        case 316: return "Требуется PUK-код SIM-карты";
        case 317: return "Требуется PIN2-код SIM-карты";
        case 318: return "Требуется PUK2-код SIM-карты";
        case 320: return "Ошибка памяти";
        case 321: return "Неверный индекс памяти";
        case 322: return "Память заполнена";
        case 330: return "Неизвестен адрес SMS-центра";
        case 331: return "Нет обслуживания сети";
        case 332: return "Таймаут сети";
        case 500: return "Неизвестная ошибка";
        default:  return "Ошибка CMS";
    }
}

} // namespace gsm
