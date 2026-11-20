#include "Core.h"
#include <string.h>

namespace gsm {

Core::Core(Stream& serial)
    : _serial(serial), _running(false), _active(false), _transactionId(0) {
    memset(&_callbacks, 0, sizeof(_callbacks));
    _activeCommand.valid = false;
}

void Core::begin() {
    _parser.reset();
    _running = true;
}

void Core::stop() {
    _running = false;
    _queue.clear();
    _active              = false;
    _activeCommand.valid = false;
    _transactionId       = 0;
    _parser.reset();
}

void Core::setCallbacks(const CoreCallbacks& callbacks) {
    _callbacks = callbacks;
}

CoreState Core::state() const {
    if (!_running) return CoreState::STOPPED;
    return _active ? CoreState::WAITING_RESPONSE : CoreState::IDLE;
}

Result Core::enqueue(const char* command,
                     const RequestInfo& request,
                     uint16_t timeoutMs,
                     CommandPriority priority) {
    if (!command || !command[0]) return Result::INVALID_PARAM;
    if (!_running) return Result::NOT_READY;
    if (!_queue.push(command, request, timeoutMs, priority)) {
        return _queue.full() ? Result::QUEUE_FULL : Result::INVALID_PARAM;
    }
    return Result::OK; // Accepted into the queue, not the modem result.
}

void Core::process() {
    if (!_running) return;

    // Bound work per loop so a continuous UART stream cannot starve the sketch.
    uint8_t processed = 0;
    while (_serial.available() > 0 && processed < 64) {
        const int value = _serial.read();
        if (value < 0) break;

        const char* line = nullptr;
        if (_parser.feed(static_cast<uint8_t>(value), line)) {
            handleLine(line);
        }
        if (_parser.takePrompt()) {
            handlePrompt();
        }
        ++processed;
    }

    if (_active && _activeCommand.valid) {
        if (static_cast<uint32_t>(millis() - _activeCommand.startedAt) >=
            _activeCommand.command.timeoutMs) {
            finishActive(Result::TIMEOUT);
        }
    }

    if (!_active) startNextCommand();
}

void Core::startNextCommand() {
    if (!_running || _active) return;

    QueuedCommand next;
    const bool popped = _transactionId
        ? _queue.popForTransaction(next, _transactionId)
        : _queue.pop(next);
    if (!popped) return;

    _activeCommand.command   = next;
    _activeCommand.startedAt = millis();
    _activeCommand.valid     = true;
    _active                  = true;

    _serial.print(next.text);
    _serial.print("\r\n");
}

void Core::handleLine(const char* line) {
    if (!line || !line[0]) return;

    if (_callbacks.onRawLine) _callbacks.onRawLine(line);

    // Always-unsolicited events (RING, +CMTI, +CLIP …) may arrive at any time.
    if (isAlwaysURC(line)) {
        if (_callbacks.onURC) _callbacks.onURC(line);
        return;
    }

    if (_active) {
        // While a command is active, treat the line as a response
        // (even if the same prefix can also appear as URC when idle).
        if (_callbacks.onResponseLine) {
            _callbacks.onResponseLine(line, _activeCommand.command.request);
        }

        if (isTerminalOK(line)) {
            finishActive(Result::OK);
        } else if (isTerminalError(line)) {
            finishActive(Result::ERROR);
        }
        return;
    }

    // Idle: potential URC prefixes go to onURC, everything else is unsolicited.
    if (isPotentialURC(line)) {
        if (_callbacks.onURC) _callbacks.onURC(line);
        return;
    }

    if (_callbacks.onUnsolicitedLine) {
        _callbacks.onUnsolicitedLine(line);
    }
}

void Core::handlePrompt() {
    if (_active && _callbacks.onPrompt) {
        _callbacks.onPrompt(_activeCommand.command.request);
    }
}

bool Core::isTerminalOK(const char* line) const {
    return strcmp(line, "OK") == 0;
}

bool Core::isTerminalError(const char* line) const {
    return strcmp(line, "ERROR") == 0 ||
           strncmp(line, "+CME ERROR:", 11) == 0 ||
           strncmp(line, "+CMS ERROR:", 11) == 0;
}

// Events that must never be treated as command responses.
bool Core::isAlwaysURC(const char* line) const {
    return strcmp(line, "RING") == 0 ||
           strcmp(line, "CONNECT") == 0 ||
           strcmp(line, "VOICE CALL: BEGIN") == 0 ||
           strcmp(line, "VOICE CALL: END") == 0 ||
           strcmp(line, "NO CARRIER") == 0 ||
           strcmp(line, "BUSY") == 0 ||
           strcmp(line, "NO ANSWER") == 0 ||
           strcmp(line, "NORMAL POWER DOWN") == 0 ||
           strncmp(line, "+CMTI:", 6) == 0 ||
           strncmp(line, "+CLIP:", 6) == 0 ||
           strncmp(line, "+CUSD:", 6) == 0 ||
           strncmp(line, "+CMT:", 5) == 0 ||
           strncmp(line, "+CDS:", 5) == 0 ||
           strncmp(line, "UNDER-VOLTAGE", 13) == 0;
}

// Prefixes that are URC when the modem is idle, but normal responses
// when a matching AT command is active (AT+CREG?, AT+CSQ, …).
bool Core::isPotentialURC(const char* line) const {
    return strcmp(line, "RDY") == 0 ||
           strncmp(line, "+CREG:", 6) == 0 ||
           strncmp(line, "+CGREG:", 7) == 0 ||
           strncmp(line, "+CEREG:", 7) == 0 ||
           strncmp(line, "+CPIN:", 6) == 0 ||
           strncmp(line, "+CSQ:", 5) == 0 ||
           strncmp(line, "+COPS:", 6) == 0;
}

void Core::finishActive(Result result) {
    if (!_active || !_activeCommand.valid) return;

    CommandResult resultInfo;
    resultInfo.request   = _activeCommand.command.request;
    resultInfo.result    = result;
    resultInfo.elapsedMs = static_cast<uint32_t>(millis() - _activeCommand.startedAt);

    _active              = false;
    _activeCommand.valid = false;

    if (_callbacks.onCommandComplete) {
        _callbacks.onCommandComplete(resultInfo);
    }
}

bool Core::writeRaw(const uint8_t* data, size_t length) {
    if (!_running || !_active || !data || length == 0) return false;
    return _serial.write(data, length) == length;
}

bool Core::writeRaw(const char* data) {
    if (!data) return false;
    return writeRaw(reinterpret_cast<const uint8_t*>(data), strlen(data));
}

bool Core::beginTransaction(uint16_t transactionId) {
    if (!transactionId || _transactionId) return false;
    _transactionId = transactionId;
    return true;
}

void Core::endTransaction(uint16_t transactionId) {
    if (_transactionId == transactionId) _transactionId = 0;
}

void Core::clearQueue() {
    _queue.clear();
}

} // namespace gsm
