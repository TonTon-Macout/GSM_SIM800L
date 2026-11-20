#pragma once

#include <Arduino.h>
#include "driver/SIM800L.h"   // for SMSMessage

namespace gsm {

#ifndef AUTH_MAX_NUMBERS
#define AUTH_MAX_NUMBERS 16
#endif

#ifndef AUTH_NUMBER_SIZE
#define AUTH_NUMBER_SIZE 20
#endif

enum class AuthMode : uint8_t {
    DISABLE,
    WHITELIST
};

/**
 * Fixed-slot in-memory whitelist.
 *
 * Slots never move when another slot is removed. This makes slot indexes
 * suitable for persistent settings/UI/SMS commands. Persistence remains
 * the application's responsibility.
 */
class Auth {
public:
    static constexpr uint8_t MAX_NUMBERS = AUTH_MAX_NUMBERS;
    static constexpr uint8_t NUMBER_SIZE = AUTH_NUMBER_SIZE;

    Auth() : _count(0), _mode(AuthMode::DISABLE) {
        clear();
    }

/**
 * @brief Добавляет телефонный номер в первый свободный слот белого списка.
 * @param number Номер телефона; допускаются разделители, которые удаляются при нормализации.
 * @return true — номер добавлен или уже присутствует; false — номер некорректен либо свободных слотов нет.
 * @details Существующие номера не дублируются. Индексы уже занятых слотов не меняются.
 */
    bool add(const char* number) {
        if (!number || !number[0]) return false;

        char norm[NUMBER_SIZE];
        if (!normalize(number, norm, sizeof(norm))) return false;
        if (indexOfNormalized(norm) >= 0) return true;

        const int slot = firstFreeSlot();
        if (slot < 0) return false;
        copyNumber(_numbers[slot], norm);
        ++_count;
        return true;
    }

/**
 * @brief Записывает номер телефона в указанный слот белого списка.
 * @param slot Индекс слота от 0 до MAX_NUMBERS-1.
 * @param number Новый номер телефона.
 * @return true — номер записан; false — неверный слот/номер или такой номер уже находится в другом слоте.
 * @details Содержимое указанного слота заменяется. Пустой слот нельзя создать через set(); для удаления используйте removeAt().
 */
    bool set(uint8_t slot, const char* number) {
        if (slot >= MAX_NUMBERS || !number || !number[0]) return false;

        char norm[NUMBER_SIZE];
        if (!normalize(number, norm, sizeof(norm))) return false;

        const int existing = indexOfNormalized(norm);
        if (existing >= 0 && existing != slot) return false; // no duplicates

        const bool wasEmpty = (_numbers[slot][0] == '\0');
        copyNumber(_numbers[slot], norm);
        if (wasEmpty) ++_count;
        return true;
    }

/**
 * @brief Удаляет телефонный номер из белого списка по его значению.
 * @param number Номер телефона для удаления.
 * @return true — номер найден и удалён; false — номер некорректен или отсутствует.
 * @details Освобождает слот, не сдвигая остальные номера.
 */
    bool remove(const char* number) {
        if (!number || !number[0]) return false;
        char norm[NUMBER_SIZE];
        if (!normalize(number, norm, sizeof(norm))) return false;
        const int slot = indexOfNormalized(norm);
        return slot >= 0 ? removeAt(static_cast<uint8_t>(slot)) : false;
    }

/**
 * @brief Удаляет номер из конкретного слота белого списка.
 * @param slot Индекс слота от 0 до MAX_NUMBERS-1.
 * @return true — слот был занят и очищен; false — индекс неверен или слот уже пуст.
 * @details Остальные номера остаются на прежних индексах.
 */
    bool removeAt(uint8_t slot) {
        if (slot >= MAX_NUMBERS || !_numbers[slot][0]) return false;
        _numbers[slot][0] = '\0';
        if (_count) --_count;
        return true;
    }

/**
 * @brief Очищает один слот белого списка, не перемещая остальные номера.
 * @param slot Индекс очищаемого слота.
 * @return true — слот очищен; false — слот пуст или индекс за пределами списка.
 * @details Полный аналог removeAt().
 */
    bool clearSlot(uint8_t slot) { return removeAt(slot); }

/**
 * @brief Проверяет, содержится ли номер телефона в белом списке.
 * @param number Проверяемый номер телефона.
 * @return true — номер найден; false — номер отсутствует или некорректен.
 * @details Перед сравнением номер нормализуется.
 */
    bool contains(const char* number) const {
        return findIndex(number) >= 0;
    }

/**
 * @brief Находит индекс слота, в котором хранится указанный номер.
 * @param number Искомый номер телефона.
 * @return Индекс слота от 0 до MAX_NUMBERS-1; -1 — номер не найден или некорректен.
 * @details Индекс фиксированный: удаление других номеров не сдвигает слоты.
 */
    int8_t findIndex(const char* number) const {
        if (!number || !number[0]) return -1;
        char norm[NUMBER_SIZE];
        if (!normalize(number, norm, sizeof(norm))) return -1;
        const int idx = indexOfNormalized(norm);
        return idx < 0 ? -1 : static_cast<int8_t>(idx);
    }

/**
 * @brief Удаляет все команды, ожидающие выполнения.
 * @return Ничего не возвращает.
 * @details Не влияет на команду, которая уже выполняется.
 */
    void clear() {
        _count = 0;
        for (uint8_t i = 0; i < MAX_NUMBERS; ++i)
            _numbers[i][0] = '\0';
    }

/**
 * @brief Возвращает количество фактически занятых слотов белого списка.
 * @return Количество занятых слотов от 0 до MAX_NUMBERS.
 * @details Это не индекс последнего занятого слота и не обязательно равно highestIndex + 1.
 */
    uint8_t count() const { return _count; }
/**
 * @brief Возвращает максимальное количество номеров, которое может хранить белый список.
 * @return Максимальное число слотов MAX_NUMBERS.
 * @details Размер задаётся макросом AUTH_MAX_NUMBERS.
 */
    uint8_t capacity() const { return MAX_NUMBERS; }
/**
 * @brief Возвращает количество свободных слотов белого списка.
 * @return Количество свободных слотов.
 * @details Равно capacity() минус count().
 */
    uint8_t freeSlots() const { return MAX_NUMBERS - _count; }
/**
 * @brief Проверяет, заполнены ли все слоты белого списка.
 * @return true — свободных слотов нет; false — есть хотя бы один свободный слот.
 * @details Не выполняет поиск номера.
 */
    bool isFull() const { return _count >= MAX_NUMBERS; }
/**
 * @brief Проверяет, пуст ли белый список.
 * @return true — ни одного номера не сохранено; false — есть хотя бы один номер.
 * @details Не зависит от выбранного режима авторизации.
 */
    bool isEmpty() const { return _count == 0; }
/**
 * @brief Проверяет, занят ли конкретный слот белого списка.
 * @param slot Индекс слота от 0 до MAX_NUMBERS-1.
 * @return true — слот существует и содержит номер; false — слот пуст или индекс неверен.
 * @details Позволяет перебирать все фиксированные слоты.
 */
    bool isSlotUsed(uint8_t slot) const {
        return slot < MAX_NUMBERS && _numbers[slot][0] != '\0';
    }

    /**
     * Get a fixed slot by index. Returns nullptr for an invalid OR empty slot.
     * Iterate from 0 to capacity()-1, not from 0 to count()-1.
     */
    const char* get(uint8_t slot) const {
        if (slot >= MAX_NUMBERS || !_numbers[slot][0]) return nullptr;
        return _numbers[slot];
    }

/**
 * @brief Находит первый свободный слот белого списка.
 * @return Индекс свободного слота или -1, если список заполнен.
 * @details Поиск начинается с нулевого слота.
 */
    int8_t firstFreeSlot() const {
        for (uint8_t i = 0; i < MAX_NUMBERS; ++i)
            if (!_numbers[i][0]) return static_cast<int8_t>(i);
        return -1;
    }

/**
 * @brief Устанавливает режим проверки телефонных номеров.
 * @param mode DISABLE — проверка отключена; WHITELIST — разрешены только номера из списка.
 * @return Ничего не возвращает.
 * @details При DISABLE метод isAllowed() разрешает любой номер.
 */
    void setMode(AuthMode mode) { _mode = mode; }
/**
 * @brief Возвращает текущий режим авторизации по белому списку.
 * @return Текущее значение AuthMode: DISABLE или WHITELIST.
 * @details Не изменяет настройки.
 */
    AuthMode mode() const { return _mode; }

/**
 * @brief Проверяет, разрешён ли номер согласно текущему режиму авторизации.
 * @param number Номер телефона или SMSMessage для соответствующей перегрузки.
 * @return true — действие разрешено; false — номер не входит в белый список или некорректен.
 * @details В режиме DISABLE проверка отключена и возвращается true. В режиме WHITELIST выполняется поиск нормализованного номера.
 */
    bool isAllowed(const char* number) const {
        if (_mode == AuthMode::DISABLE) return true;
        return contains(number);
    }

/**
 * @brief Проверяет, разрешён ли номер согласно текущему режиму авторизации.
 * @param number Номер телефона или SMSMessage для соответствующей перегрузки.
 * @return true — действие разрешено; false — номер не входит в белый список или некорректен.
 * @details В режиме DISABLE проверка отключена и возвращается true. В режиме WHITELIST выполняется поиск нормализованного номера.
 */
    bool isAllowed(const SMSMessage& sms) const {
        return isAllowed(sms.sender);
    }

/**
 * @brief Проверяет, разрешён ли входящий или исходящий вызов с указанного номера.
 * @param number Номер телефона абонента.
 * @return true — вызов разрешён текущим режимом авторизации; false — номер запрещён.
 * @details Использует ту же политику, что и isAllowed(number).
 */
    bool isAllowedCall(const char* number) const {
        return isAllowed(number);
    }

/**
 * @brief Нормализует телефонный номер, удаляя разделители и приводя российский формат к +7.
 * @param in Исходная строка номера.
 * @param out Буфер для нормализованного номера.
 * @param outSize Размер буфера out в байтах.
 * @return true — номер преобразован; false — входные данные некорректны или буфер мал.
 * @details Российские номера 8XXXXXXXXXX и 7XXXXXXXXXX преобразуются в +7XXXXXXXXXX.
 */
    static bool normalize(const char* in, char* out, size_t outSize) {
        if (!in || !out || outSize < 2) return false;

        char digits[NUMBER_SIZE];
        size_t d = 0;
        for (const char* p = in; *p; ++p) {
            if (*p >= '0' && *p <= '9') {
                if (d >= sizeof(digits) - 1) return false;
                digits[d++] = *p;
            }
        }
        digits[d] = '\0';
        if (!d) return false;

        if (d == 11 && (digits[0] == '8' || digits[0] == '7')) {
            if (outSize < 13) return false;
            out[0] = '+';
            out[1] = '7';
            memcpy(out + 2, digits + 1, 10);
            out[12] = '\0';
            return true;
        }

        const bool plus = in[0] == '+';
        const size_t required = d + (plus ? 2 : 1);
        if (outSize < required) return false;
        size_t o = 0;
        if (plus) out[o++] = '+';
        memcpy(out + o, digits, d);
        out[o + d] = '\0';
        return true;
    }

private:
    char _numbers[MAX_NUMBERS][NUMBER_SIZE];
    uint8_t _count;
    AuthMode _mode;

    static void copyNumber(char* dst, const char* src) {
        strncpy(dst, src, NUMBER_SIZE - 1);
        dst[NUMBER_SIZE - 1] = '\0';
    }

    int indexOfNormalized(const char* normalized) const {
        for (uint8_t i = 0; i < MAX_NUMBERS; ++i)
            if (_numbers[i][0] && strcmp(_numbers[i], normalized) == 0)
                return static_cast<int>(i);
        return -1;
    }
};

} // namespace gsm
