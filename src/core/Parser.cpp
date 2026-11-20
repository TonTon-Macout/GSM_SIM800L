#include "Parser.h"
#include <string.h>

namespace gsm {

Parser::Parser()
    : _length(0), _prompt(false), _skipPromptSpace(false), _overflow(false) {
    _line[0] = '\0';
}

void Parser::reset() {
    _length          = 0;
    _line[0]         = '\0';
    _prompt          = false;
    _skipPromptSpace = false;
    _overflow        = false;
}

bool Parser::takePrompt() {
    const bool value = _prompt;
    _prompt = false;
    return value;
}

bool Parser::feed(uint8_t value, const char*& completedLine) {
    completedLine = nullptr;

    if (_skipPromptSpace) {
        _skipPromptSpace = false;
        if (value == ' ' || value == '\t') return false;
    }

    // SIM800 prompt usually arrives as "> " without CR/LF.
    if (_length == 0 && value == '>') {
        _prompt          = true;
        _skipPromptSpace = true;
        return false;
    }

    if (value == '\r') return false;

    if (value == '\n') {
        if (_overflow) {
            _overflow = false;
            _length   = 0;
            _line[0]  = '\0';
            return false;
        }
        if (_length == 0) return false;
        _line[_length] = '\0';
        completedLine  = _line;
        _length        = 0;
        return true;
    }

    if (_length < (CORE_LINE_SIZE - 1)) {
        _line[_length++] = static_cast<char>(value);
    } else {
        // Discard the entire overlong line.
        _overflow = true;
        _length   = 0;
        _line[0]  = '\0';
    }

    return false;
}

} // namespace gsm
