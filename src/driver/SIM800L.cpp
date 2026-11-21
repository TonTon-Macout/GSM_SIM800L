#include "SIM800L.h"
#include <string.h>
#include <stdlib.h>

namespace gsm {

namespace {
static const uint16_t TX_INIT = 0xF100;
static const uint16_t TX_SMS_SEND = 0xF101;
static const uint16_t TX_SMS_READ = 0xF102;
static const uint16_t TX_SMS_SCAN = 0xF103;
static const uint16_t TX_SMS_DELETE = 0xF104;
static const uint16_t TX_SMS_MEMORY = 0xF105;
}

SIM800L* SIM800L::s_instance = nullptr;
static uint8_t g_smsConcatReference = 1;

SIM800L::SIM800L(Core& core)
    : _core(core),
      _available(false),
      _initDone(false),
      _simReady(false),
      _smsReady(false),
      _initInProgress(false),
      _initStep(0),
      _lastCme(-1),
      _lastCms(-1),
      _signalQuality(-1),
      _signalDbm(-1),
      _registrationStatus(4),
      _registrationValid(false),
      _operatorValid(false),
      _monitoring(false),
      _simInterval(60000),
      _registrationInterval(15000),
      _signalInterval(10000),
      _operatorInterval(300000),
      _lastSimPoll(0),
      _lastRegistrationPoll(0),
      _lastSignalPoll(0),
      _lastOperatorPoll(0),
      _lastResponseAt(0),
      _lastSuccessfulCommandAt(0),
      _simUpdatedAt(0),
      _registrationUpdatedAt(0),
      _signalUpdatedAt(0),
      _operatorUpdatedAt(0),
      _pollCursor(0),
      _recoveryEnabled(false), _recoveryCheckPending(false),
      _recoveryAttempts(0), _maxRecoveryAttempts(3),
      _recoveryTimeout(60000), _lastRecoveryCheck(0),
      _callState(CallState::IDLE),
      _smsHead(0), _smsCount(0),
      _smsScanInterval(30000), _smsLastScan(0), _smsScanActive(false), _smsScanStage(0),
      _smsAutoDelete(false), _smsReading(false), _smsDeletePending(false),
      _smsActiveIndex(0), _smsHeaderSeen(false),
      _ussdPending(false), _ussdRequest(), _ussdStartedAt(0),
      _smsSendText{0},
      _smsSendResult(Result::OK), _smsSendActive(false),
      _smsUtf16Count(0), _smsSegmentOffset(0), _smsSegmentCount(0),
      _smsSegmentIndex(0), _smsConcatRef(0), _smsSendPduLength(0),
      _smsOutHead(0), _smsOutCount(0) {
    memset(&_cb, 0, sizeof(_cb));
    memset(&_lastError, 0, sizeof(_lastError));
    memset(&_lastInit, 0, sizeof(_lastInit));
    _lastError.cme     = -1;
    _lastError.cms     = -1;
    _lastError.context = "";
    _lastError.message = "";
    _lastInit.cme      = -1;
    _lastInit.message  = "";
    _messageBuf[0]     = '\0';
    _operatorName[0]   = '\0';
    _simStatus[0]      = '\0';
    _lastResponse[0]   = '\0';
    memset(_smsQueue, 0, sizeof(_smsQueue));
    memset(_smsProcessedSM, 0, sizeof(_smsProcessedSM));
    memset(_smsProcessedME, 0, sizeof(_smsProcessedME));
    memset(&_smsCurrent, 0, sizeof(_smsCurrent));
    memset(&_smsMemory, 0, sizeof(_smsMemory));
    memset(_smsOutQueue, 0, sizeof(_smsOutQueue));
    _smsActiveStorage[0] = '\0';
    _smsSendNumberDisplay[0] = '\0';
    _smsSendBody[0] = '\0';
    _dialNumber[0] = '\0';
    _callNumber[0] = '\0';
    s_instance         = this;

    CoreCallbacks coreCb = {};
    coreCb.onRawLine         = trampolineRawLine;
    coreCb.onURC             = trampolineURC;
    coreCb.onResponseLine    = trampolineResponseLine;
    coreCb.onCommandComplete = trampolineCommandComplete;
    coreCb.onPrompt          = trampolinePrompt;
    _core.setCallbacks(coreCb);
}

void SIM800L::setCallbacks(const ModemCallbacks& cb) {
    _cb = cb;
}

void SIM800L::setMessage(const char* msg) {
    if (!msg) {
        _messageBuf[0] = '\0';
        return;
    }
    strncpy(_messageBuf, msg, sizeof(_messageBuf) - 1);
    _messageBuf[sizeof(_messageBuf) - 1] = '\0';
}

Result SIM800L::startInit(const RequestInfo& request) {
    if (_initInProgress) {
        if (_cb.onError) _cb.onError(Result::BUSY, "INIT: initialization is already running");
        return Result::BUSY;
    }
    if (!_core.isRunning()) {
        if (_cb.onError) _cb.onError(Result::NOT_READY, "INIT: Core is not running; call begin() first");
        return Result::NOT_READY;
    }

    _initInProgress = true;
    _initDone       = false;
    _simReady       = false;
    _smsReady       = false;
    _initStep       = 0;
    _initRequest    = request;
    _lastCme        = -1;
    _lastCms        = -1;

    if (!_core.beginTransaction(TX_INIT)) {
        _initInProgress = false;
        if (_cb.onError) _cb.onError(Result::BUSY, "INIT: command channel is occupied by another transaction");
        return Result::BUSY;
    }
    Result r = enqueue("AT",
                       RequestInfo(static_cast<uint16_t>(DriverRequest::INIT_AT),
                                   request.source, TX_INIT),
                       2000, CommandPriority::P_HIGH);
    if (r != Result::OK) {
        _core.endTransaction(TX_INIT);
        _initInProgress = false;
        if (_cb.onError) _cb.onError(r, "INIT: failed to enqueue initial AT command");
    }
    return r;
}

Result SIM800L::checkSim(const RequestInfo& request) {
    Result result = enqueue("AT+CPIN?",
                   RequestInfo(static_cast<uint16_t>(DriverRequest::CHECK_CPIN),
                               request.source),
                   3000);
    if (result != Result::OK && _cb.onError)
        _cb.onError(result, "NETWORK: failed to enqueue SIM check");
    return result;
}

Result SIM800L::checkRegistration(const RequestInfo& request) {
    Result result = enqueue("AT+CREG?",
                   RequestInfo(static_cast<uint16_t>(DriverRequest::CHECK_CREG),
                               request.source),
                   3000);
    if (result != Result::OK && _cb.onError)
        _cb.onError(result, "NETWORK: failed to enqueue registration check");
    return result;
}

Result SIM800L::checkSignal(const RequestInfo& request) {
    Result result = enqueue("AT+CSQ",
                   RequestInfo(static_cast<uint16_t>(DriverRequest::CHECK_CSQ),
                               request.source),
                   3000);
    if (result != Result::OK && _cb.onError)
        _cb.onError(result, "NETWORK: failed to enqueue signal check");
    return result;
}

Result SIM800L::checkOperator(const RequestInfo& request) {
    Result result = enqueue("AT+COPS?",
                   RequestInfo(static_cast<uint16_t>(DriverRequest::CHECK_COPS),
                               request.source),
                   5000);
    if (result != Result::OK && _cb.onError)
        _cb.onError(result, "NETWORK: failed to enqueue operator check");
    return result;
}

Result SIM800L::sendUSSD(const char* code, uint8_t dcs, const RequestInfo& request) {
    if (!code || !code[0]) {
        if (_cb.onError) _cb.onError(Result::INVALID_PARAM, "USSD: empty request code");
        return Result::INVALID_PARAM;
    }
    if (_ussdPending) {
        if (_cb.onError) _cb.onError(Result::BUSY, "USSD: another request is awaiting a response");
        return Result::BUSY;
    }

    // USSD strings are normally short (*100#, *102#, etc.). Reject quotes and
    // control characters rather than allowing AT-command injection.
    char cmd[64];
    size_t n = strlen(code);
    if (n > 40) {
        if (_cb.onError) _cb.onError(Result::INVALID_PARAM, "USSD: request code is longer than 40 characters");
        return Result::INVALID_PARAM;
    }
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(code[i]);
        if (c < 0x20 || c == '"' || c == '\\') {
            if (_cb.onError) _cb.onError(Result::INVALID_PARAM, "USSD: request contains a control character or forbidden quote/backslash");
            return Result::INVALID_PARAM;
        }
    }
    snprintf(cmd, sizeof(cmd), "AT+CUSD=1,\"%s\",%u", code, dcs);

    // Keep the caller's complete context for the asynchronous +CUSD URC.
    // The AT command itself uses an internal ID so command completion remains identifiable.
    _ussdRequest = request;
    _ussdPending = true;
    _ussdStartedAt = millis();
    Result result = enqueue(cmd,
        RequestInfo(static_cast<uint16_t>(DriverRequest::USSD_SEND), request.source), 10000);
    if (result != Result::OK) {
        _ussdPending = false;
        _ussdRequest = RequestInfo();
        _ussdStartedAt = 0;
        if (_cb.onError) _cb.onError(result, "USSD: failed to enqueue request");
    }
    return result;
}

Result SIM800L::enqueue(const char* cmd,
                        const RequestInfo& request,
                        uint16_t timeoutMs,
                        CommandPriority priority) {
    Result r = _core.enqueue(cmd, request, timeoutMs, priority);
    if (r == Result::OK) {
        _lastCme = -1;
        _lastCms = -1;
    } else if ((request.requestId == 0 ||
                request.requestId >= static_cast<uint16_t>(DriverRequest::USER_BASE)) && _cb.onError) {
        // Пользовательские AT-команды не имеют отдельной обёртки, поэтому сообщаем об отказе здесь.
        // Внутренние операции обрабатывают ошибку на своём уровне, чтобы не было двойных callback.
        _cb.onError(r, "AT: failed to enqueue user command");
    }
    return r;
}

/* Result SIM800L::sendSMS(const char* number, const char* text,
                        const RequestInfo& request) {
    if (!_core.isRunning()) return Result::NOT_READY;
    if (!number || !number[0]) return Result::INVALID_PARAM;
    if (!text || !text[0]) return Result::INVALID_PARAM;

    if (strlen(number) > 20) return Result::INVALID_PARAM;
    if (strlen(text) > SMS_MAX_UTF8_BYTES) return Result::INVALID_PARAM;

    // Validate recipient and UTF-8/UCS2 payload before accepting it.
    size_t digitCount = 0;
    for (size_t i = 0; number[i]; ++i) {
        const char c = number[i];
        if (c == '+' && i == 0) continue;
        if (c < '0' || c > '9' || digitCount >= 20) return Result::INVALID_PARAM;
        ++digitCount;
    }
    if (!digitCount) return Result::INVALID_PARAM;

    size_t probeCount = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(text);
    while (*p) {
        uint32_t cp;
        if (*p < 0x80) cp = *p++;
        else if ((*p & 0xE0) == 0xC0 && p[1]) {
            cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); p += 2;
        } else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
            cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3;
        } else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
            cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
                 ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); p += 4;
        } else return Result::INVALID_PARAM;

        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return Result::INVALID_PARAM;
        if (cp <= 0xFFFF) {
            if (probeCount >= 280) return Result::INVALID_PARAM;
            ++probeCount;
        } else {
            if (probeCount + 2 > 280) return Result::INVALID_PARAM;
            probeCount += 2;
        }
    }
    if (!probeCount) return Result::INVALID_PARAM;

    if (_smsSendActive) {
        if (_smsOutCount >= SMS_OUT_QUEUE_SIZE) return Result::QUEUE_FULL;
        OutgoingSMS& slot = _smsOutQueue[(_smsOutHead + _smsOutCount) % SMS_OUT_QUEUE_SIZE];
        strncpy(slot.number, number, sizeof(slot.number) - 1);
        slot.number[sizeof(slot.number) - 1] = '\0';
        strncpy(slot.text, text, sizeof(slot.text) - 1);
        slot.text[sizeof(slot.text) - 1] = '\0';
        slot.source = request.source;
        ++_smsOutCount;
        return Result::OK;
    }

    return beginSmsSend(number, text, request.source);
}
 */

Result SIM800L::sendSMS(const char* number, const char* text,
                        const RequestInfo& request) {

    if (!_core.isRunning()) {
        if (_cb.onError) _cb.onError(Result::NOT_READY, "SMS: modem not ready");
        return Result::NOT_READY;
    }

    if (!number || !number[0]) {
        if (_cb.onError) _cb.onError(Result::INVALID_PARAM, "SMS: empty phone number");
        return Result::INVALID_PARAM;
    }

    if (!text || !text[0]) {
        if (_cb.onError) _cb.onError(Result::INVALID_PARAM, "SMS: empty text");
        return Result::INVALID_PARAM;
    }

    if (strlen(number) > 20) {
        if (_cb.onError) _cb.onError(Result::INVALID_PARAM, "SMS: phone number too long");
        return Result::INVALID_PARAM;
    }

    if (strlen(text) > SMS_MAX_UTF8_BYTES) {
        if (_cb.onError) _cb.onError(Result::INVALID_PARAM, "SMS: UTF-8 text too long");
        return Result::INVALID_PARAM;
    }

    // Validate recipient.
    size_t digitCount = 0;

    for (size_t i = 0; number[i]; ++i) {
        const char c = number[i];

        if (c == '+' && i == 0) continue;

        if (c < '0' || c > '9' || digitCount >= 20) {
            if (_cb.onError) _cb.onError(Result::INVALID_PARAM, "SMS: invalid phone number");
            return Result::INVALID_PARAM;
        }

        ++digitCount;
    }

    if (!digitCount) {
        if (_cb.onError) _cb.onError(Result::INVALID_PARAM, "SMS: invalid phone number");
        return Result::INVALID_PARAM;
    }

    // Validate UTF-8 and calculate UTF-16 length.
    size_t probeCount = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(text);

    while (*p) {
        uint32_t cp;

        if (*p < 0x80) {
            cp = *p++;
        } else if ((*p & 0xE0) == 0xC0 && p[1]) {
            cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
            p += 2;
        } else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
            cp = ((p[0] & 0x0F) << 12) |
                 ((p[1] & 0x3F) << 6) |
                 (p[2] & 0x3F);
            p += 3;
        } else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
            cp = ((p[0] & 0x07) << 18) |
                 ((p[1] & 0x3F) << 12) |
                 ((p[2] & 0x3F) << 6) |
                 (p[3] & 0x3F);
            p += 4;
        } else {
            if (_cb.onError) _cb.onError(Result::INVALID_PARAM, "SMS: invalid UTF-8 encoding");
            return Result::INVALID_PARAM;
        }

        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            if (_cb.onError) _cb.onError(Result::INVALID_PARAM, "SMS: invalid Unicode character");
            return Result::INVALID_PARAM;
        }

        if (cp <= 0xFFFF) {
            if (probeCount >= SMS_MAX_UTF16_UNITS) {
                if (_cb.onError) _cb.onError(Result::INVALID_PARAM, "SMS: text exceeds UTF-16 limit");
                return Result::INVALID_PARAM;
            }
            ++probeCount;
        } else {
            if (probeCount + 2 > SMS_MAX_UTF16_UNITS) {
                if (_cb.onError) _cb.onError(Result::INVALID_PARAM, "SMS: text exceeds UTF-16 limit");
                return Result::INVALID_PARAM;
            }
            probeCount += 2;
        }
    }

    if (!probeCount) {
        if (_cb.onError) _cb.onError(Result::INVALID_PARAM, "SMS: empty UTF-16 text");
        return Result::INVALID_PARAM;
    }

    // Queue outgoing SMS if another message is being sent.
    if (_smsSendActive) {
        if (_smsOutCount >= SMS_OUT_QUEUE_SIZE) {
            if (_cb.onError) _cb.onError(Result::QUEUE_FULL, "SMS: outgoing queue full");
            return Result::QUEUE_FULL;
        }

        OutgoingSMS& slot =
            _smsOutQueue[(_smsOutHead + _smsOutCount) % SMS_OUT_QUEUE_SIZE];

        strncpy(slot.number, number, sizeof(slot.number) - 1);
        slot.number[sizeof(slot.number) - 1] = '\0';

        strncpy(slot.text, text, sizeof(slot.text) - 1);
        slot.text[sizeof(slot.text) - 1] = '\0';

        slot.source = request.source;
        ++_smsOutCount;

        return Result::OK;
    }
    return beginSmsSend(number, text, request.source);
    // Start sending immediately.
    //Result result = beginSmsSend(number, text, request.source);
    //if (result != Result::OK && _cb.onError) {
    //    _cb.onError(result, "SMS: failed to start sending");
    //}
    //return result;
}


bool SIM800L::prepareSmsText(const char* text) {
    _smsUtf16Count = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(text);
    while (*p) {
        uint32_t cp;
        if (*p < 0x80) cp = *p++;
        else if ((*p & 0xE0) == 0xC0 && p[1]) {
            cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); p += 2;
        } else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
            cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3;
        } else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
            cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
                 ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); p += 4;
        } else return false;

        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        if (cp <= 0xFFFF) {
            if (_smsUtf16Count >= 280) return false;
            _smsUtf16[_smsUtf16Count++] = static_cast<uint16_t>(cp);
        } else {
            if (_smsUtf16Count + 2 > 280) return false;
            cp -= 0x10000;
            _smsUtf16[_smsUtf16Count++] = static_cast<uint16_t>(0xD800 | (cp >> 10));
            _smsUtf16[_smsUtf16Count++] = static_cast<uint16_t>(0xDC00 | (cp & 0x3FF));
        }
    }
    if (!_smsUtf16Count) return false;

    if (_smsUtf16Count <= 70) {
        _smsSegmentCount = 1;
    } else {
        _smsSegmentCount = 0;
        uint16_t offset = 0;
        while (offset < _smsUtf16Count) {
            uint16_t part = _smsUtf16Count - offset;
            if (part > 67) part = 67;
            // Never leave a UTF-16 high surrogate at the end of a part.
            if (offset + part < _smsUtf16Count &&
                _smsUtf16[offset + part - 1] >= 0xD800 &&
                _smsUtf16[offset + part - 1] <= 0xDBFF &&
                _smsUtf16[offset + part] >= 0xDC00 &&
                _smsUtf16[offset + part] <= 0xDFFF) {
                --part;
            }
            if (!part || _smsSegmentCount == 255) return false;
            offset += part;
            ++_smsSegmentCount;
        }
    }
    _smsSegmentOffset = 0;
    _smsSegmentIndex = 0;
    _smsConcatRef = g_smsConcatReference++;
    if (!g_smsConcatReference) g_smsConcatReference = 1;
    return true;
}

bool SIM800L::buildSmsPduSegment() {
    if (_smsSegmentOffset >= _smsUtf16Count) return false;

    uint8_t pdu[180];
    size_t n = 0;
    const bool multipart = _smsSegmentCount > 1;
    const uint16_t maxUnits = multipart ? 67 : 70;
    uint16_t count = _smsUtf16Count - _smsSegmentOffset;
    if (count > maxUnits) count = maxUnits;
    if (multipart && _smsSegmentOffset + count < _smsUtf16Count &&
        _smsUtf16[_smsSegmentOffset + count - 1] >= 0xD800 &&
        _smsUtf16[_smsSegmentOffset + count - 1] <= 0xDBFF &&
        _smsUtf16[_smsSegmentOffset + count] >= 0xDC00 &&
        _smsUtf16[_smsSegmentOffset + count] <= 0xDFFF) {
        --count;
    }

    // SMSC length = 0 (use the SMSC configured on the SIM).
    pdu[n++] = 0x00;
    // SMS-SUBMIT; set UDHI for concatenated messages.
    pdu[n++] = multipart ? 0x41 : 0x01;
    pdu[n++] = 0x00; // message reference

    char digits[21];
    size_t digitCount = 0;
    bool international = (_smsSendNumberDisplay[0] == '+');
    for (size_t i = international ? 1 : 0; _smsSendNumberDisplay[i]; ++i) {
        if (_smsSendNumberDisplay[i] < '0' || _smsSendNumberDisplay[i] > '9' ||
            digitCount >= sizeof(digits)) return false;
        digits[digitCount++] = _smsSendNumberDisplay[i];
    }
    if (!digitCount) return false;

    pdu[n++] = static_cast<uint8_t>(digitCount);
    pdu[n++] = international ? 0x91 : 0x81;
    for (size_t i = 0; i < digitCount; i += 2) {
        uint8_t low = static_cast<uint8_t>(digits[i] - '0');
        uint8_t high = (i + 1 < digitCount)
            ? static_cast<uint8_t>(digits[i + 1] - '0') : 0x0F;
        pdu[n++] = static_cast<uint8_t>((high << 4) | low);
    }

    pdu[n++] = 0x00; // PID
    pdu[n++] = 0x08; // UCS2 DCS
    pdu[n++] = static_cast<uint8_t>((multipart ? 6 : 0) + count * 2);

    if (multipart) {
        pdu[n++] = 0x05; // UDH length
        pdu[n++] = 0x00; // concatenated SMS, 8-bit reference
        pdu[n++] = 0x03;
        pdu[n++] = _smsConcatRef;
        pdu[n++] = _smsSegmentCount;
        pdu[n++] = static_cast<uint8_t>(_smsSegmentIndex + 1);
    }

    for (uint16_t i = 0; i < count; ++i) {
        const uint16_t value = _smsUtf16[_smsSegmentOffset + i];
        pdu[n++] = static_cast<uint8_t>(value >> 8);
        pdu[n++] = static_cast<uint8_t>(value & 0xFF);
    }

    static const char hex[] = "0123456789ABCDEF";
    size_t out = 0;
    for (size_t i = 0; i < n; ++i) {
        if (out + 2 >= sizeof(_smsSendBody)) return false;
        _smsSendBody[out++] = hex[pdu[i] >> 4];
        _smsSendBody[out++] = hex[pdu[i] & 0x0F];
    }
    _smsSendBody[out] = '\0';
    // AT+CMGS length excludes the SMSC length octet and SMSC address.
    _smsSendPduLength = static_cast<uint16_t>(n - 1);
    _smsSegmentOffset += count;
    return true;
}

Result SIM800L::enqueueSmsPduSegment() {
    if (!buildSmsPduSegment()) return Result::INVALID_PARAM;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CMGS=%u", static_cast<unsigned>(_smsSendPduLength));
    return enqueue(cmd,
        RequestInfo(static_cast<uint16_t>(DriverRequest::SMS_SEND_CMGS), Source::SMS, TX_SMS_SEND),
        60000, CommandPriority::P_HIGH);
}

/* Result SIM800L::beginSmsSend(const char* number, const char* text, Source source) {
    if (!_core.isRunning()) return Result::NOT_READY;
    if (_smsSendActive) return Result::BUSY;

    if (!prepareSmsText(text)) return Result::INVALID_PARAM;

    strncpy(_smsSendText, text, sizeof(_smsSendText) - 1);
    _smsSendText[sizeof(_smsSendText) - 1] = '\0';
    strncpy(_smsSendNumberDisplay, number, sizeof(_smsSendNumberDisplay) - 1);
    _smsSendNumberDisplay[sizeof(_smsSendNumberDisplay) - 1] = '\0';

    if (!_core.beginTransaction(TX_SMS_SEND)) return Result::BUSY;
    _smsSendActive = true;
    _smsSendResult = Result::OK;
    Result r = enqueue("AT+CMGF=0",
        RequestInfo(static_cast<uint16_t>(DriverRequest::SMS_SEND_CMGF), source, TX_SMS_SEND),
        5000, CommandPriority::P_HIGH);
    if (r != Result::OK) finishSmsSend(r);
    return r;
} */

Result SIM800L::beginSmsSend(const char* number, const char* text, Source source) {
    if (!_core.isRunning()) {
        if (_cb.onError) _cb.onError(Result::NOT_READY, "SMS: modem not ready");
        return Result::NOT_READY;
    }

    if (_smsSendActive) {
        if (_cb.onError) _cb.onError(Result::BUSY, "SMS: another message is being sent");
        return Result::BUSY;
    }

    if (!prepareSmsText(text)) {
        if (_cb.onError) _cb.onError(Result::INVALID_PARAM, "SMS: failed to prepare text");
        return Result::INVALID_PARAM;
    }

    // Notify about multipart SMS.
    if (_smsSegmentCount > 1 && _cb.onError) {
        char message[64];
        snprintf(message, sizeof(message), "SMS: multipart message (%u SMS)", (unsigned)_smsSegmentCount);
        _cb.onError(Result::OK, message);
    }

    strncpy(_smsSendText, text, sizeof(_smsSendText) - 1);
    _smsSendText[sizeof(_smsSendText) - 1] = '\0';

    strncpy(_smsSendNumberDisplay, number, sizeof(_smsSendNumberDisplay) - 1);
    _smsSendNumberDisplay[sizeof(_smsSendNumberDisplay) - 1] = '\0';

    if (!_core.beginTransaction(TX_SMS_SEND)) {
        if (_cb.onError) _cb.onError(Result::BUSY, "SMS: transaction is busy");
        return Result::BUSY;
    }

    _smsSendActive = true;
    _smsSendResult = Result::OK;

    Result r = enqueue("AT+CMGF=0",
        RequestInfo(static_cast<uint16_t>(DriverRequest::SMS_SEND_CMGF),
                    source, TX_SMS_SEND),
        5000, CommandPriority::P_HIGH);

    if (r != Result::OK) {
        // finishSmsSend() handles the error callback.
        finishSmsSend(r);
    }

    return r;
}


void SIM800L::startNextOutgoingSms() {
    if (_smsSendActive || _smsOutCount == 0) return;

    OutgoingSMS& next = _smsOutQueue[_smsOutHead];
    char number[24];
    char text[SMS_MAX_UTF8_BYTES + 1];
    strncpy(number, next.number, sizeof(number) - 1);
    number[sizeof(number) - 1] = '\0';
    strncpy(text, next.text, sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';
    const Source source = next.source;

    _smsOutHead = static_cast<uint8_t>((_smsOutHead + 1) % SMS_OUT_QUEUE_SIZE);
    --_smsOutCount;

    // beginSmsSend calls finishSmsSend on enqueue failure, which re-enters here.
    // BUSY (transaction conflict) is transient — put the message back at the head.
    Result r = beginSmsSend(number, text, source);
    if (r == Result::BUSY && _smsOutCount < SMS_OUT_QUEUE_SIZE) {
        _smsOutHead = static_cast<uint8_t>((_smsOutHead + SMS_OUT_QUEUE_SIZE - 1) % SMS_OUT_QUEUE_SIZE);
        OutgoingSMS& slot = _smsOutQueue[_smsOutHead];
        strncpy(slot.number, number, sizeof(slot.number) - 1);
        slot.number[sizeof(slot.number) - 1] = '\0';
        strncpy(slot.text, text, sizeof(slot.text) - 1);
        slot.text[sizeof(slot.text) - 1] = '\0';
        slot.source = source;
        ++_smsOutCount;
    }
}

Result SIM800L::answerCall(const RequestInfo& request) {
    Result r = enqueue("ATA", RequestInfo(static_cast<uint16_t>(DriverRequest::CALL_ANSWER), request.source),
                       10000, CommandPriority::P_HIGH);
    if (r == Result::OK) setCallState(CallState::ANSWERING);
    else if (_cb.onError) _cb.onError(r, "CALL: failed to enqueue answer command");
    return r;
}

Result SIM800L::hangUp(const RequestInfo& request) {
    Result r = enqueue("ATH", RequestInfo(static_cast<uint16_t>(DriverRequest::CALL_HANGUP), request.source),
                       5000, CommandPriority::P_HIGH);
    if (r != Result::OK && _cb.onError) _cb.onError(r, "CALL: failed to enqueue hang-up command");
    return r;
}

/* 
Result SIM800L::dial(const char* number, const RequestInfo& request) {
    if (!number || !number[0]) return Result::INVALID_PARAM;
    for (size_t i = 0; number[i]; ++i) {
        const char c = number[i];
        if ((c < '0' || c > '9') && !(c == '+' && i == 0)) return Result::INVALID_PARAM;
    }
    if (strlen(number) > 20) return Result::INVALID_PARAM;
    char cmd[40];
    snprintf(cmd, sizeof(cmd), "ATD%s;", number);
    strncpy(_dialNumber, number, sizeof(_dialNumber) - 1);
    _dialNumber[sizeof(_dialNumber) - 1] = '\0';
    Result r = enqueue(cmd, RequestInfo(static_cast<uint16_t>(DriverRequest::CALL_DIAL), request.source),
                       10000, CommandPriority::P_HIGH);
    if (r == Result::OK) {
        strncpy(_callNumber, number, sizeof(_callNumber) - 1);
        _callNumber[sizeof(_callNumber) - 1] = '\0';
        setCallState(CallState::DIALING);
    }
    return r;
}
 */
Result SIM800L::dial(const char* number, const RequestInfo& request) {
    if (!number || !number[0]) {
        if (_cb.onError) _cb.onError(Result::INVALID_PARAM, "CALL: empty phone number");
        return Result::INVALID_PARAM;
    }

    if (strlen(number) > 20) {
        if (_cb.onError) _cb.onError(Result::INVALID_PARAM, "CALL: phone number too long");
        return Result::INVALID_PARAM;
    }

    for (size_t i = 0; number[i]; ++i) {
        const char c = number[i];

        if ((c < '0' || c > '9') && !(c == '+' && i == 0)) {
            if (_cb.onError) _cb.onError(Result::INVALID_PARAM, "CALL: invalid phone number");
            return Result::INVALID_PARAM;
        }
    }

    char cmd[40];
    snprintf(cmd, sizeof(cmd), "ATD%s;", number);

    strncpy(_dialNumber, number, sizeof(_dialNumber) - 1);
    _dialNumber[sizeof(_dialNumber) - 1] = '\0';

    Result r = enqueue(
        cmd,
        RequestInfo(static_cast<uint16_t>(DriverRequest::CALL_DIAL), request.source),
        10000,
        CommandPriority::P_HIGH
    );

    if (r == Result::OK) {
        strncpy(_callNumber, number, sizeof(_callNumber) - 1);
        _callNumber[sizeof(_callNumber) - 1] = '\0';
        setCallState(CallState::DIALING);
    } else {
        if (_cb.onError) _cb.onError(r, "CALL: failed to enqueue dial command");
    }

    return r;
}
/* void SIM800L::finishSmsSend(Result result) {
    _smsSendActive = false;
    _smsSendResult = result;
    _core.endTransaction(TX_SMS_SEND);

    if (_cb.onSmsSent) {
        _cb.onSmsSent(_smsSendNumberDisplay, _smsSendText, result);
    }

    // Chain the next queued outgoing SMS, if any.
    startNextOutgoingSms();
} */


void SIM800L::finishSmsSend(Result result) {
    _smsSendActive = false;
    _smsSendResult = result;
    _core.endTransaction(TX_SMS_SEND);

    if (result != Result::OK) {
        if (_cb.onError) {
            _cb.onError(result, "SMS: sending failed");
        }
    }

    if (_cb.onSmsSent) {
        _cb.onSmsSent(_smsSendNumberDisplay, _smsSendText, result);
    }

    // Chain the next queued outgoing SMS, if any.
    startNextOutgoingSms();
}


bool SIM800L::writeRaw(const char* data) {
    return _core.writeRaw(data);
}

bool SIM800L::writeRaw(const uint8_t* data, size_t length) {
    return _core.writeRaw(data, length);
}

void SIM800L::process() {
    // SIM800 may accept AT+CUSD but never return +CUSD. Release the single
    // outstanding slot after 60 seconds and report a local timeout (status 255).
    if (_ussdPending && static_cast<uint32_t>(millis() - _ussdStartedAt) >= 60000UL) {
        const RequestInfo request = _ussdRequest;
        _ussdPending = false;
        _ussdRequest = RequestInfo();
        _ussdStartedAt = 0;
        if (_cb.onUssdResponse) _cb.onUssdResponse(255, "", request);
    }
    processSms();
    // Give a due health check the opportunity to enter the queue before
    // routine network polling can occupy the command channel.
    processRecovery();
    processMonitoring();
}

void SIM800L::startMonitoring() {
    _monitoring = true;
    const uint32_t now = millis();
    // First poll each item immediately.
    _lastSimPoll = now - _simInterval;
    _lastRegistrationPoll = now - _registrationInterval;
    _lastSignalPoll = now - _signalInterval;
    _lastOperatorPoll = now - _operatorInterval;
    _pollCursor = 0;
}

void SIM800L::setPollIntervals(uint32_t simMs, uint32_t registrationMs,
                               uint32_t signalMs, uint32_t operatorMs) {
    // Zero disables an individual poll; minimum 1 second avoids queue flooding.
    _simInterval = simMs ? (simMs < 1000 ? 1000 : simMs) : 0;
    _registrationInterval = registrationMs ? (registrationMs < 1000 ? 1000 : registrationMs) : 0;
    _signalInterval = signalMs ? (signalMs < 1000 ? 1000 : signalMs) : 0;
    _operatorInterval = operatorMs ? (operatorMs < 1000 ? 1000 : operatorMs) : 0;
}

void SIM800L::setRecoveryEnabled(bool enabled) {
    _recoveryEnabled = enabled;
    _lastRecoveryCheck = millis();
    if (!enabled) {
        _recoveryCheckPending = false;
        _recoveryAttempts = 0;
    }
}

void SIM800L::setRecoveryTimeout(uint32_t intervalMs) {
    _recoveryTimeout = intervalMs < 5000 ? 5000 : intervalMs;
    _lastRecoveryCheck = millis();
}

void SIM800L::resetRecoveryAttempts() {
    _recoveryAttempts = 0;
    _recoveryCheckPending = false;
    _lastRecoveryCheck = millis();
}
void SIM800L::processRecovery() {
    if (!_recoveryEnabled || !_core.isRunning() || _initInProgress || _recoveryCheckPending) return;
    const uint32_t now = millis();
    if (static_cast<uint32_t>(now - _lastRecoveryCheck) < _recoveryTimeout) return;
    if (_core.isBusy() || _core.queuedCount() != 0 || _core.transactionActive()) return;

    Result r = enqueue("AT", RequestInfo(static_cast<uint16_t>(DriverRequest::RECOVERY_PING),
                                         Source::PERIODIC), 3000, CommandPriority::P_HIGH);
    if (r == Result::OK) {
        _recoveryCheckPending = true;
        _lastRecoveryCheck = now;
    }
}

void SIM800L::processMonitoring() {
    if (!_monitoring || !_core.isRunning() || _initInProgress || _recoveryAttempts) return;
    // Do not compete with user commands or fill the finite command queue.
    if (_core.isBusy() || _core.queuedCount() != 0 || _core.transactionActive()) return;

    const uint32_t now = millis();
    for (uint8_t n = 0; n < 4; ++n) {
        const uint8_t i = (_pollCursor + n) & 0x03;
        Result r = Result::BUSY;
        uint32_t* last = nullptr;
        uint32_t interval = 0;
        switch (i) {
            case 0: last = &_lastSignalPoll; interval = _signalInterval; break;
            case 1: last = &_lastRegistrationPoll; interval = _registrationInterval; break;
            case 2: last = &_lastSimPoll; interval = _simInterval; break;
            default: last = &_lastOperatorPoll; interval = _operatorInterval; break;
        }
        if (!interval || static_cast<uint32_t>(now - *last) < interval) continue;
        switch (i) {
            case 0: r = checkSignal(RequestInfo(0, Source::PERIODIC)); break;
            case 1: r = checkRegistration(RequestInfo(0, Source::PERIODIC)); break;
            case 2: r = checkSim(RequestInfo(0, Source::PERIODIC)); break;
            default: r = checkOperator(RequestInfo(0, Source::PERIODIC)); break;
        }
        if (r == Result::OK) {
            *last = now;
            _pollCursor = (i + 1) & 0x03;
        }
        return; // At most one command is added per process() call.
    }
}

void SIM800L::storeLastResponse(const char* line) {
    if (!line || !line[0] || strcmp(line, "OK") == 0 ||
        (line[0] == 'A' && line[1] == 'T')) return;
    strncpy(_lastResponse, line, sizeof(_lastResponse) - 1);
    _lastResponse[sizeof(_lastResponse) - 1] = '\0';
    _lastResponseAt = millis();
}

void SIM800L::trampolineRawLine(const char* line) {
    if (s_instance) s_instance->onCoreRawLine(line);
}

void SIM800L::trampolineURC(const char* line) {
    if (s_instance) s_instance->onCoreURC(line);
}

void SIM800L::trampolineResponseLine(const char* line, const RequestInfo& request) {
    if (s_instance) s_instance->onCoreResponseLine(line, request);
}

void SIM800L::trampolineCommandComplete(const CommandResult& result) {
    if (s_instance) s_instance->onCoreCommandComplete(result);
}

void SIM800L::trampolinePrompt(const RequestInfo& request) {
    if (s_instance) s_instance->onCorePrompt(request);
}

void SIM800L::onCoreRawLine(const char* line) {
    if (_cb.onRawLine) _cb.onRawLine(line);
}

void SIM800L::onCoreURC(const char* line) {
    handleURC(line);
}

void SIM800L::captureErrorLine(const char* line) {
    if (strncmp(line, "+CME ERROR:", 11) == 0) {
        _lastCme = static_cast<int16_t>(atoi(line + 11));
        _lastCms = -1;
    } else if (strncmp(line, "+CMS ERROR:", 11) == 0) {
        _lastCms = static_cast<int16_t>(atoi(line + 11));
        _lastCme = -1;
    }
}

void SIM800L::onCoreResponseLine(const char* line, const RequestInfo& request) {
    captureErrorLine(line);
    storeLastResponse(line);

    const uint16_t id = request.requestId;

    if (id == static_cast<uint16_t>(DriverRequest::SMS_READ)) {
        if (strncmp(line, "+CMGR:", 6) == 0) parseCmgrHeader(line);
        else if (_smsHeaderSeen && strcmp(line, "OK") != 0 &&
                 strncmp(line, "+CMS ERROR:", 11) != 0 &&
                 strncmp(line, "+CME ERROR:", 11) != 0) {
            // The body is a separate line in SIM800 text mode.
            strncpy(_smsCurrent.text, line, sizeof(_smsCurrent.text) - 1);
            _smsCurrent.text[sizeof(_smsCurrent.text) - 1] = '\0';
        }
    } else if (id == static_cast<uint16_t>(DriverRequest::SMS_MEMORY)) {
        if (strncmp(line, "+CPMS:", 6) == 0) parseSmsMemory(line);
    } else if (id == static_cast<uint16_t>(DriverRequest::SMS_SCAN_LIST)) {
        // AT+CMGL="ALL" returns one +CMGL header per stored message.
        // The body is intentionally ignored here: queue the index and read
        // each SMS through the normal AT+CMGR path after the scan completes.
        if (strncmp(line, "+CMGL:", 6) == 0) {
            const int index = atoi(line + 6);
            if (index > 0 && index <= 255) {
                const char* storage = (_smsScanStage == 0) ? "SM" : "ME";
                queueSms(storage, static_cast<uint8_t>(index));
            }
        }
    }

    if (id == static_cast<uint16_t>(DriverRequest::CHECK_CPIN) ||
        id == static_cast<uint16_t>(DriverRequest::INIT_CPIN)) {
        if (strncmp(line, "+CPIN:", 6) == 0) parseCpin(line);
    } else if (id == static_cast<uint16_t>(DriverRequest::CHECK_CREG) ||
               id == static_cast<uint16_t>(DriverRequest::INIT_CREG)) {
        if (strncmp(line, "+CREG:", 6) == 0) parseCreg(line);
    } else if (id == static_cast<uint16_t>(DriverRequest::CHECK_CSQ)) {
        if (strncmp(line, "+CSQ:", 5) == 0) parseCsq(line);
    } else if (id == static_cast<uint16_t>(DriverRequest::CHECK_COPS)) {
        if (strncmp(line, "+COPS:", 6) == 0) parseCops(line);
    }
}

void SIM800L::reportError(Result result, const char* context) {
    const char* msg = errorToString(result);
    if (result == Result::ERROR) {
        if (_lastCme >= 0) msg = cmeToStr(_lastCme);
        else if (_lastCms >= 0) msg = cmsToStr(_lastCms);
        else if (context && strcmp(context, "CPIN") == 0)
            msg = "SIM-карта не готова: проверьте PIN/PUK или наличие SIM";
        else if (context && (strcmp(context, "CMGF") == 0 || strcmp(context, "CNMI") == 0))
            msg = "Ошибка инициализации SMS";
    }

    _lastError.result  = result;
    _lastError.cme     = _lastCme;
    _lastError.cms     = _lastCms;
    _lastError.context = context ? context : "";
    setMessage(msg);
    _lastError.message = _messageBuf;

    if (_cb.onModemError) _cb.onModemError(_lastError);
    if (_cb.onError) _cb.onError(result, context);
}

void SIM800L::onCoreCommandComplete(const CommandResult& result) {
    const uint16_t id = result.request.requestId;
    if (result.result == Result::OK) _lastSuccessfulCommandAt = millis();

    if (id == static_cast<uint16_t>(DriverRequest::USSD_SEND)) {
        // AT+CUSD OK only means the request was accepted. The actual result
        // arrives separately as +CUSD; on command failure release the pending slot.
        if (result.result != Result::OK && _ussdPending) {
            _ussdPending = false;
            _ussdRequest = RequestInfo();
            _ussdStartedAt = 0;
        }
        return;
    }

    if (id == static_cast<uint16_t>(DriverRequest::SMS_SELECT)) {
        if (result.result == Result::OK) {
            char readCmd[24];
            snprintf(readCmd, sizeof(readCmd), "AT+CMGR=%u", _smsActiveIndex);
            Result r = enqueueSmsCommand(readCmd, static_cast<uint16_t>(DriverRequest::SMS_READ), 10000, TX_SMS_READ);
            if (r != Result::OK) finishSmsRead(r);
        } else {
            finishSmsRead(result.result);
        }
        return;
    }
    if (id == static_cast<uint16_t>(DriverRequest::SMS_READ)) {
        finishSmsRead(result.result);
        return;
    }
    if (id == static_cast<uint16_t>(DriverRequest::SMS_MEMORY_SELECT)) {
        if (result.result == Result::OK) {
            Result r = enqueueSmsCommand("AT+CPMS?", static_cast<uint16_t>(DriverRequest::SMS_MEMORY),
                                         5000, TX_SMS_MEMORY);
            if (r != Result::OK) _core.endTransaction(TX_SMS_MEMORY);
        } else _core.endTransaction(TX_SMS_MEMORY);
        return;
    }
    if (id == static_cast<uint16_t>(DriverRequest::SMS_MEMORY)) {
        _core.endTransaction(TX_SMS_MEMORY);
        return;
    }
    if (id == static_cast<uint16_t>(DriverRequest::SMS_DELETE_ALL_SELECT)) {
        if (result.result == Result::OK) {
            Result r = enqueueSmsCommand("AT+CMGD=1,4", static_cast<uint16_t>(DriverRequest::SMS_DELETE_ALL),
                                         15000, TX_SMS_DELETE);
            if (r != Result::OK) _core.endTransaction(TX_SMS_DELETE);
        } else _core.endTransaction(TX_SMS_DELETE);
        return;
    }
    if (id == static_cast<uint16_t>(DriverRequest::SMS_DELETE_SELECT)) {
        if (result.result == Result::OK) {
            char cmd[24];
            snprintf(cmd, sizeof(cmd), "AT+CMGD=%u", _smsActiveIndex);
            Result r = enqueueSmsCommand(cmd, static_cast<uint16_t>(DriverRequest::SMS_DELETE),
                                         10000, TX_SMS_DELETE);
            if (r != Result::OK) {
                _core.endTransaction(TX_SMS_DELETE);
                if (_cb.onSmsError) _cb.onSmsError(_smsActiveStorage, _smsActiveIndex, r);
            }
        } else {
            _core.endTransaction(TX_SMS_DELETE);
            setSmsProcessed(_smsActiveStorage, _smsActiveIndex, false);
            if (_cb.onSmsError) _cb.onSmsError(_smsActiveStorage, _smsActiveIndex, result.result);
        }
        return;
    }
    if (id == static_cast<uint16_t>(DriverRequest::SMS_DELETE)) {
        _core.endTransaction(TX_SMS_DELETE);
        setSmsProcessed(_smsActiveStorage, _smsActiveIndex, false);
        if (result.result != Result::OK && _cb.onSmsError)
            _cb.onSmsError(_smsActiveStorage, _smsActiveIndex, result.result);
        return;
    }
    if (id == static_cast<uint16_t>(DriverRequest::SMS_DELETE_ALL)) {
        _core.endTransaction(TX_SMS_DELETE);
        if (result.result != Result::OK && _cb.onSmsError)
            _cb.onSmsError(_smsActiveStorage, 0, result.result);
        return;
    }
    if (id == static_cast<uint16_t>(DriverRequest::SMS_SCAN_SELECT)) {
        if (!_smsScanActive) return;
        if (result.result == Result::OK) {
            Result r = enqueueSmsCommand("AT+CMGL=\"ALL\"", static_cast<uint16_t>(DriverRequest::SMS_SCAN_LIST), 15000, TX_SMS_SCAN);
            if (r != Result::OK) advanceSmsScan(r);
        } else advanceSmsScan(result.result);
        return;
    }
    if (id == static_cast<uint16_t>(DriverRequest::SMS_SCAN_LIST)) {
        advanceSmsScan(result.result);
        return;
    }

    // PDU-mode SMS sending. Multipart parts are submitted sequentially.
    if (id == static_cast<uint16_t>(DriverRequest::SMS_SEND_CMGF)) {
        if (result.result == Result::OK) {
            Result r = enqueueSmsPduSegment();
            if (r != Result::OK) {
                _smsSendResult = r;
                Result restore = enqueue("AT+CMGF=1",
                    RequestInfo(static_cast<uint16_t>(DriverRequest::SMS_SEND_RESTORE), Source::SMS, TX_SMS_SEND),
                    5000, CommandPriority::P_HIGH);
                if (restore != Result::OK) finishSmsSend(_smsSendResult);
            }
        } else finishSmsSend(result.result);
        return;
    }
    if (id == static_cast<uint16_t>(DriverRequest::SMS_SEND_CMGS)) {
        if (result.result != Result::OK) {
            _smsSendResult = result.result;
            Result r = enqueue("AT+CMGF=1",
                RequestInfo(static_cast<uint16_t>(DriverRequest::SMS_SEND_RESTORE), Source::SMS, TX_SMS_SEND),
                5000, CommandPriority::P_HIGH);
            if (r != Result::OK) finishSmsSend(_smsSendResult);
        } else if (_smsSegmentIndex + 1 < _smsSegmentCount) {
            ++_smsSegmentIndex;
            Result r = enqueueSmsPduSegment();
            if (r != Result::OK) {
                _smsSendResult = r;
                Result restore = enqueue("AT+CMGF=1",
                    RequestInfo(static_cast<uint16_t>(DriverRequest::SMS_SEND_RESTORE), Source::SMS, TX_SMS_SEND),
                    5000, CommandPriority::P_HIGH);
                if (restore != Result::OK) finishSmsSend(_smsSendResult);
            }
        } else {
            _smsSendResult = Result::OK;
            Result r = enqueue("AT+CMGF=1",
                RequestInfo(static_cast<uint16_t>(DriverRequest::SMS_SEND_RESTORE), Source::SMS, TX_SMS_SEND),
                5000, CommandPriority::P_HIGH);
            if (r != Result::OK) finishSmsSend(_smsSendResult);
        }
        return;
    }
    if (id == static_cast<uint16_t>(DriverRequest::SMS_SEND_RESTORE)) {
        if (result.result != Result::OK && _smsSendResult == Result::OK)
            _smsSendResult = result.result;
        finishSmsSend(_smsSendResult);
        return;
    }
    if (id == static_cast<uint16_t>(DriverRequest::CALL_ANSWER)) {
        if (result.result != Result::OK) {
            setCallState(CallState::FAILED);
            reportError(result.result, "ATA");
        }
        return;
    }
    if (id == static_cast<uint16_t>(DriverRequest::CALL_HANGUP)) {
        if (result.result != Result::OK) reportError(result.result, "ATH");
        else {
            const bool changed = _callState != CallState::ENDED;
            setCallState(CallState::ENDED);
            if (changed && _cb.onCallEnded) _cb.onCallEnded();
        }
        return;
    }
    if (id == static_cast<uint16_t>(DriverRequest::CALL_DIAL)) {
        if (result.result == Result::OK) {
            if (_cb.onDialStarted) _cb.onDialStarted(_dialNumber);
        } else {
            setCallState(CallState::FAILED);
            reportError(result.result, "ATD");
        }
        return;
    }

    if (id == static_cast<uint16_t>(DriverRequest::RECOVERY_PING)) {
        _recoveryCheckPending = false;
        if (result.result == Result::OK || result.result == Result::ERROR) {
            const bool wasRecovering = (_recoveryAttempts != 0);
            _recoveryAttempts = 0; // modem responded, even if AT returned ERROR
            // wasRecovering: modem returned after drop; !_available: first appearance after boot
            if ((wasRecovering || !_available) && !_initInProgress)
                startInit(RequestInfo(0, Source::PERIODIC));
        } else if (result.result == Result::TIMEOUT && _recoveryEnabled) {
            if (_recoveryAttempts < _maxRecoveryAttempts) {
                ++_recoveryAttempts;
                if (_cb.onRecoveryRequired) _cb.onRecoveryRequired(_recoveryAttempts);
            }
        }
        return;
    }

    if (_initInProgress &&
        id >= static_cast<uint16_t>(DriverRequest::INIT_AT) &&
        id <= static_cast<uint16_t>(DriverRequest::INIT_CNMI)) {
        advanceInit(result.result);
        return;
    }

    if (result.result != Result::OK) {
        const char* ctx = "command";
        if (id == static_cast<uint16_t>(DriverRequest::CHECK_CPIN)) ctx = "CPIN";
        if (id == static_cast<uint16_t>(DriverRequest::CHECK_CREG)) ctx = "CREG";
        if (id == static_cast<uint16_t>(DriverRequest::CHECK_CSQ))  ctx = "CSQ";
        if (id == static_cast<uint16_t>(DriverRequest::CHECK_COPS)) ctx = "COPS";
        reportError(result.result, ctx);
    }
}

void SIM800L::onCorePrompt(const RequestInfo& request) {
    if (request.requestId != static_cast<uint16_t>(DriverRequest::SMS_SEND_CMGS) || !_smsSendActive)
        return;
    if (!_core.writeRaw(_smsSendBody)) {
        _smsSendResult = Result::ERROR;
        return;
    }
    const uint8_t ctrlZ = 0x1A;
    if (!_core.writeRaw(&ctrlZ, 1)) _smsSendResult = Result::ERROR;
}

// Soft init (B):
// Required: AT, ATE0, CMEE, CLIP, CREG
// Then:     CPIN?
// Optional: CMGF, CNMI only if SIM READY
//
// available = AT answered OK
// initDone  = available && simReady && smsReady

void SIM800L::advanceInit(Result lastResult) {
    const uint8_t step = _initStep;

    if (step == 0) {
        if (lastResult != Result::OK) {
            _available = false;
            reportError(lastResult, "init");
            finishInit();
            return;
        }
        _available = true;
    }

    if (step >= 1 && step <= 4 && lastResult != Result::OK) {
        reportError(lastResult, "init");
        finishInit();
        return;
    }

    if (step == 5) {
        if (lastResult != Result::OK) {
            reportError(lastResult, "CPIN");
            _simReady = false;
            finishInit();
            return;
        }
        if (!_simReady) {
            reportError(Result::ERROR, "CPIN");
            finishInit();
            return;
        }
    }

    if (step == 6 && lastResult != Result::OK) {
        reportError(lastResult, "CMGF");
        _smsReady = false;
        finishInit();
        return;
    }
    if (step == 7 && lastResult != Result::OK) {
        reportError(lastResult, "CNMI");
        _smsReady = false;
        finishInit();
        return;
    }
    if (step == 7 && lastResult == Result::OK) {
        _smsReady = true;
        finishInit();
        return;
    }

    ++_initStep;
    Result r = Result::OK;

    switch (_initStep) {
        case 1:
            r = enqueue("ATE0",
                        RequestInfo(static_cast<uint16_t>(DriverRequest::INIT_ATE),
                                    _initRequest.source, TX_INIT),
                        2000, CommandPriority::P_HIGH);
            break;
        case 2:
            r = enqueue("AT+CMEE=1",
                        RequestInfo(static_cast<uint16_t>(DriverRequest::INIT_CMEE),
                                    _initRequest.source, TX_INIT),
                        2000, CommandPriority::P_HIGH);
            break;
        case 3:
            r = enqueue("AT+CLIP=1",
                        RequestInfo(static_cast<uint16_t>(DriverRequest::INIT_CLIP),
                                    _initRequest.source, TX_INIT),
                        2000, CommandPriority::P_HIGH);
            break;
        case 4:
            r = enqueue("AT+CREG=1",
                        RequestInfo(static_cast<uint16_t>(DriverRequest::INIT_CREG),
                                    _initRequest.source, TX_INIT),
                        2000, CommandPriority::P_HIGH);
            break;
        case 5:
            r = enqueue("AT+CPIN?",
                        RequestInfo(static_cast<uint16_t>(DriverRequest::INIT_CPIN),
                                    _initRequest.source, TX_INIT),
                        3000, CommandPriority::P_HIGH);
            break;
        case 6:
            r = enqueue("AT+CMGF=1",
                        RequestInfo(static_cast<uint16_t>(DriverRequest::INIT_CMGF),
                                    _initRequest.source, TX_INIT),
                        2000, CommandPriority::P_HIGH);
            break;
        case 7:
            r = enqueue("AT+CNMI=2,1,0,0,0",
                        RequestInfo(static_cast<uint16_t>(DriverRequest::INIT_CNMI),
                                    _initRequest.source, TX_INIT),
                        2000, CommandPriority::P_HIGH);
            break;
        default:
            finishInit();
            return;
    }

    if (r != Result::OK) {
        reportError(r, "init-enqueue");
        finishInit();
    }
}

void SIM800L::finishInit() {
    _initInProgress = false;
    _core.endTransaction(TX_INIT);
    _initDone       = _available && _simReady && _smsReady;

    InitResult ir;
    ir.available = _available;
    ir.simReady  = _simReady;
    ir.smsReady  = _smsReady;
    ir.cme       = _lastCme;

    if (!_available) {
        ir.result = (_lastError.result != Result::OK) ? _lastError.result : Result::TIMEOUT;
        setMessage(ir.result == Result::TIMEOUT ? "No modem response" : resultToStr(ir.result));
        ir.message = _messageBuf;
    } else if (!_simReady) {
        ir.result = Result::ERROR;
        if (_lastCme >= 0) setMessage(cmeToStr(_lastCme));
        else setMessage("SIM not ready");
        ir.message = _messageBuf;
    } else if (!_smsReady) {
        ir.result = Result::ERROR;
        if (_lastCme >= 0) setMessage(cmeToStr(_lastCme));
        else setMessage("SMS setup failed");
        ir.message = _messageBuf;
    } else {
        ir.result = Result::OK;
        setMessage("OK");
        ir.message = _messageBuf;
    }

    _lastInit = ir;

    if (_cb.onInitCompleteEx) _cb.onInitCompleteEx(ir);
    if (_cb.onInitComplete) _cb.onInitComplete(ir.result);

    if (_available) startMonitoring();
    if (ir.result == Result::OK) {
        _smsLastScan = millis() - _smsScanInterval;
        if (_cb.onReady) _cb.onReady();
    }
}

void SIM800L::setCallState(CallState state) {
    _callState = state;
    if (_cb.onCallState) _cb.onCallState(state, _callNumber);
}

void SIM800L::handleURC(const char* line) {
    if (strcmp(line, "RING") == 0) {
        if (_callState != CallState::ACTIVE && _callState != CallState::DIALING) {
            if (_callState == CallState::IDLE || _callState == CallState::ENDED || _callState == CallState::BUSY)
                _callNumber[0] = '\0';
            setCallState(CallState::RINGING);
        }
        return;
    }
    if (strcmp(line, "CONNECT") == 0 || strcmp(line, "VOICE CALL: BEGIN") == 0) {
        const bool incomingWasAnswered = (_callState == CallState::ANSWERING);
        setCallState(CallState::ACTIVE);
        if (incomingWasAnswered && _cb.onCallAnswered) _cb.onCallAnswered();
        return;
    }
    if (strcmp(line, "BUSY") == 0) {
        const bool changed = _callState != CallState::BUSY;
        setCallState(CallState::BUSY);
        if (changed && _cb.onCallEnded) _cb.onCallEnded();
        return;
    }
    if (strcmp(line, "NO CARRIER") == 0 || strcmp(line, "NO ANSWER") == 0 ||
        strcmp(line, "VOICE CALL: END") == 0) {
        const bool changed = _callState != CallState::ENDED;
        setCallState(CallState::ENDED);
        if (changed && _cb.onCallEnded) _cb.onCallEnded();
        return;
    }
    if (strncmp(line, "+CLIP:", 6) == 0) {
        parseClip(line);
        return;
    }
    if (strncmp(line, "+CMTI:", 6) == 0) {
        parseCmti(line);
        return;
    }
    if (strncmp(line, "+CUSD:", 6) == 0) {
        parseCusd(line);
        return;
    }
    if (strncmp(line, "+CREG:", 6) == 0) {
        parseCreg(line);
        return;
    }
    if (strncmp(line, "+CPIN:", 6) == 0) {
        parseCpin(line);
        return;
    }
}

void SIM800L::parseCreg(const char* line) {
    const char* p = strchr(line, ':');
    if (!p) return;
    ++p;
    while (*p == ' ') ++p;

    int n = 0, stat = 0;
    if (strchr(p, ',')) {
        n = atoi(p);
        p = strchr(p, ',');
        if (p) stat = atoi(p + 1);
    } else {
        stat = atoi(p);
    }

    if (stat < 0 || stat > 5) stat = 4;
    _registrationStatus = static_cast<uint8_t>(stat);
    _registrationValid = true;
    _registrationUpdatedAt = millis();

    if (_cb.onRegistration) {
        _cb.onRegistration(static_cast<uint8_t>(n), _registrationStatus);
    }
}

void SIM800L::parseCsq(const char* line) {
    const char* p = strchr(line, ':');
    if (!p) return;
    ++p;
    while (*p == ' ') ++p;

    int rssi = atoi(p);
    int ber  = 99;
    p = strchr(p, ',');
    if (p) ber = atoi(p + 1);

    if (rssi < 0 || rssi > 31 || rssi == 99) {
        _signalQuality = -1;
        _signalDbm = -1;
    } else {
        _signalQuality = static_cast<int8_t>(rssi);
        _signalDbm = static_cast<int8_t>(-113 + (rssi * 2));
    }

    _signalUpdatedAt = millis();

    if (_cb.onSignal) {
        _cb.onSignal(static_cast<uint8_t>(rssi), static_cast<uint8_t>(ber));
    }
}

void SIM800L::parseCpin(const char* line) {
    const char* p = strchr(line, ':');
    if (!p) return;
    ++p;
    while (*p == ' ') ++p;

    char status[sizeof(_simStatus)];
    size_t len = strlen(p);
    while (len && (p[len - 1] == ' ' || p[len - 1] == '\t')) --len;
    if (len >= sizeof(status)) len = sizeof(status) - 1;
    memcpy(status, p, len);
    status[len] = '\0';
    strncpy(_simStatus, status, sizeof(_simStatus) - 1);
    _simStatus[sizeof(_simStatus) - 1] = '\0';
    _simReady = (strcmp(_simStatus, "READY") == 0);
    _simUpdatedAt = millis();

    if (_cb.onSimStatus) _cb.onSimStatus(_simStatus);
}

void SIM800L::parseCops(const char* line) {
    const char* p = strchr(line, '"');
    if (!p) {
        _operatorName[0] = '\0';
        _operatorValid = false;
        _operatorUpdatedAt = millis();
        if (_cb.onOperator) _cb.onOperator("");
        return;
    }
    ++p;
    size_t i = 0;
    while (*p && *p != '"' && i < sizeof(_operatorName) - 1) _operatorName[i++] = *p++;
    _operatorName[i] = '\0';
    _operatorValid = (i > 0);
    _operatorUpdatedAt = millis();
    if (_cb.onOperator) _cb.onOperator(_operatorName);
}

void SIM800L::parseClip(const char* line) {
    const char* p = strchr(line, '"');
    if (!p) return;
    ++p;
    char number[24];
    size_t i = 0;
    while (*p && *p != '"' && i < sizeof(number) - 1) number[i++] = *p++;
    number[i] = '\0';
    strncpy(_callNumber, number, sizeof(_callNumber) - 1);
    _callNumber[sizeof(_callNumber) - 1] = '\0';
    if (_callState != CallState::ACTIVE && _callState != CallState::DIALING) {
        if (_callState != CallState::RINGING) setCallState(CallState::RINGING);
        else if (_cb.onCallState) _cb.onCallState(CallState::RINGING, _callNumber);
    }
    if (_cb.onIncomingCall) _cb.onIncomingCall(number);
}

void SIM800L::parseCmti(const char* line) {
    const char* first = strchr(line, '"');
    if (!first) return;
    const char* last = strchr(first + 1, '"');
    if (!last) return;
    char storage[4] = {0};
    size_t n = (size_t)(last - first - 1);
    if (n == 0 || n >= sizeof(storage)) return;
    memcpy(storage, first + 1, n);
    const char* comma = strchr(last, ',');
    if (!comma) return;
    int index = atoi(comma + 1);
    if (index < 0 || index > 255) return;
    if (_cb.onSmsReceived) _cb.onSmsReceived(index);
    queueSms(storage, (uint8_t)index);
}


Result SIM800L::enqueueSmsCommand(const char* cmd, uint16_t requestId, uint16_t timeout,
                                  uint16_t transactionId) {
    return enqueue(cmd, RequestInfo(requestId, Source::SMS, transactionId), timeout, CommandPriority::P_HIGH);
}

void SIM800L::setSmsScanInterval(uint32_t intervalMs) {
    _smsScanInterval = intervalMs;
    if (intervalMs) _smsLastScan = millis() - intervalMs; // next process() may scan immediately
}

bool SIM800L::smsWasProcessed(const char* storage, uint8_t index) const {
    const uint8_t* map = strcmp(storage, "SM") == 0 ? _smsProcessedSM : _smsProcessedME;
    return (map[index >> 3] & (uint8_t)(1U << (index & 7))) != 0;
}

void SIM800L::setSmsProcessed(const char* storage, uint8_t index, bool value) {
    uint8_t* map = strcmp(storage, "SM") == 0 ? _smsProcessedSM : _smsProcessedME;
    const uint8_t mask = (uint8_t)(1U << (index & 7));
    if (value) map[index >> 3] |= mask;
    else map[index >> 3] &= (uint8_t)~mask;
}

void SIM800L::removeQueuedSms(const char* storage, int index) {
    PendingSMS kept[60];
    uint8_t count = 0;
    for (uint8_t i = 0; i < _smsCount; ++i) {
        const PendingSMS& item = _smsQueue[(_smsHead + i) % 60];
        const bool match = strcmp(item.storage, storage) == 0 &&
                           (index < 0 || item.index == (uint8_t)index);
        if (!match && count < 60) kept[count++] = item;
    }
    memset(_smsQueue, 0, sizeof(_smsQueue));
    for (uint8_t i = 0; i < count; ++i) _smsQueue[i] = kept[i];
    _smsHead = 0;
    _smsCount = count;
}

Result SIM800L::deleteSMS(const char* storage, uint8_t index) {
    if (!storage || (strcmp(storage, "SM") != 0 && strcmp(storage, "ME") != 0)) {
        if (_cb.onError) _cb.onError(Result::INVALID_PARAM, "SMS: invalid storage; use SM or ME");
        return Result::INVALID_PARAM;
    }
    if (!_core.beginTransaction(TX_SMS_DELETE)) {
        if (_cb.onError) _cb.onError(Result::BUSY, "SMS: cannot start delete SMS; command channel is busy");
        return Result::BUSY;
    }

    _smsScanActive = false;
    setSmsProcessed(storage, index, false);
    removeQueuedSms(storage, index);
    strncpy(_smsActiveStorage, storage, sizeof(_smsActiveStorage) - 1);
    _smsActiveStorage[sizeof(_smsActiveStorage) - 1] = '\0';
    _smsActiveIndex = index;

    char cmd[48];
    snprintf(cmd, sizeof(cmd), "AT+CPMS=\"%s\",\"%s\",\"%s\"", storage, storage, storage);
    Result r = enqueueSmsCommand(cmd, static_cast<uint16_t>(DriverRequest::SMS_DELETE_SELECT),
                                 5000, TX_SMS_DELETE);
    if (r != Result::OK) {
        if (_cb.onError) _cb.onError(r, "SMS: failed to enqueue delete SMS command");
        _core.endTransaction(TX_SMS_DELETE);
    }
    return r;
}

Result SIM800L::deleteAllSMS(const char* storage) {
    if (!storage || (strcmp(storage, "SM") != 0 && strcmp(storage, "ME") != 0)) {
        if (_cb.onError) _cb.onError(Result::INVALID_PARAM, "SMS: invalid storage; use SM or ME");
        return Result::INVALID_PARAM;
    }
    if (!_core.beginTransaction(TX_SMS_DELETE)) {
        if (_cb.onError) _cb.onError(Result::BUSY, "SMS: cannot start delete all SMS; command channel is busy");
        return Result::BUSY;
    }

    _smsScanActive = false;
    if (strcmp(storage, "SM") == 0) memset(_smsProcessedSM, 0, sizeof(_smsProcessedSM));
    else memset(_smsProcessedME, 0, sizeof(_smsProcessedME));
    removeQueuedSms(storage, -1);
    strncpy(_smsActiveStorage, storage, sizeof(_smsActiveStorage) - 1);
    _smsActiveStorage[sizeof(_smsActiveStorage) - 1] = '\0';

    char cmd[48];
    snprintf(cmd, sizeof(cmd), "AT+CPMS=\"%s\",\"%s\",\"%s\"", storage, storage, storage);
    Result r = enqueueSmsCommand(cmd, static_cast<uint16_t>(DriverRequest::SMS_DELETE_ALL_SELECT),
                                 5000, TX_SMS_DELETE);
    if (r != Result::OK) {
        if (_cb.onError) _cb.onError(r, "SMS: failed to enqueue delete all SMS command");
        _core.endTransaction(TX_SMS_DELETE);
    }
    return r;
}

Result SIM800L::checkSmsMemory(const char* storage) {
    if (!storage || (strcmp(storage, "SM") != 0 && strcmp(storage, "ME") != 0)) {
        if (_cb.onError) _cb.onError(Result::INVALID_PARAM, "SMS: invalid storage; use SM or ME");
        return Result::INVALID_PARAM;
    }
    if (!_core.beginTransaction(TX_SMS_MEMORY)) {
        if (_cb.onError) _cb.onError(Result::BUSY, "SMS: cannot start check SMS memory; command channel is busy");
        return Result::BUSY;
    }
    strncpy(_smsMemory.storage, storage, sizeof(_smsMemory.storage) - 1);
    _smsMemory.storage[sizeof(_smsMemory.storage) - 1] = '\0';
    strncpy(_smsActiveStorage, storage, sizeof(_smsActiveStorage) - 1);
    _smsActiveStorage[sizeof(_smsActiveStorage) - 1] = '\0';
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "AT+CPMS=\"%s\",\"%s\",\"%s\"", storage, storage, storage);
    Result r = enqueueSmsCommand(cmd, static_cast<uint16_t>(DriverRequest::SMS_MEMORY_SELECT),
                                 5000, TX_SMS_MEMORY);
    if (r != Result::OK) {
        if (_cb.onError) _cb.onError(r, "SMS: failed to enqueue check SMS memory command");
        _core.endTransaction(TX_SMS_MEMORY);
    }
    return r;
}

bool SIM800L::queueSms(const char* storage, uint8_t index) {
    if (!storage || (strcmp(storage, "SM") != 0 && strcmp(storage, "ME") != 0)) return false;
    if (smsWasProcessed(storage, index)) return true;
    for (uint8_t i = 0; i < _smsCount; ++i) {
        const PendingSMS& item = _smsQueue[(_smsHead + i) % 60];
        if (item.index == index && strcmp(item.storage, storage) == 0) return true;
    }
    if (_smsReading && _smsActiveIndex == index && strcmp(_smsActiveStorage, storage) == 0) return true;
    if (_smsCount >= 60) {
        if (_cb.onSmsError) _cb.onSmsError(storage, index, Result::QUEUE_FULL);
        return false;
    }
    PendingSMS& item = _smsQueue[(_smsHead + _smsCount) % 60];
    strncpy(item.storage, storage, sizeof(item.storage) - 1);
    item.storage[sizeof(item.storage) - 1] = '\0';
    item.index = index;
    ++_smsCount;
    setSmsProcessed(storage, index, true);
    return true;
}

void SIM800L::processSms() {
    if (!_core.isRunning() || !_smsReady || _initInProgress || _recoveryAttempts) return;

    // Defer auto-delete until after the application SMS callback has returned.
    // This lets the callback enqueue a reply SMS without transaction conflicts.
    if (_smsDeletePending && !_core.isBusy() && !_core.queuedCount() &&
        !_core.transactionActive()) {
        if (_core.beginTransaction(TX_SMS_DELETE)) {
            char cmd[48];
            snprintf(cmd, sizeof(cmd), "AT+CPMS=\"%s\",\"%s\",\"%s\"",
                     _smsActiveStorage, _smsActiveStorage, _smsActiveStorage);
            Result r = enqueueSmsCommand(cmd, static_cast<uint16_t>(DriverRequest::SMS_DELETE_SELECT),
                                         5000, TX_SMS_DELETE);
            if (r == Result::OK) _smsDeletePending = false;
            else {
                _core.endTransaction(TX_SMS_DELETE);
                setSmsProcessed(_smsActiveStorage, _smsActiveIndex, false);
                _smsDeletePending = false;
                if (_cb.onSmsError) _cb.onSmsError(_smsActiveStorage, _smsActiveIndex, r);
            }
            return;
        }
    }

    // Only periodic storage scanning follows the monitoring switch.
    // +CMTI notifications and queued SMS reads continue while monitoring is off.
    if (_monitoring && !_smsScanActive && _smsScanInterval &&
        static_cast<uint32_t>(millis() - _smsLastScan) >= _smsScanInterval &&
        !_core.isBusy() && _core.queuedCount() == 0) {
        startSmsScan();
    }

    if (_smsReading || !_smsCount || !_core.isRunning()) return;
    if (_core.isBusy() || _core.queuedCount() || _core.transactionActive()) return;
    startNextSms();
}

void SIM800L::startSmsScan() {
    if (_smsScanActive || !_smsReady || !_core.isRunning() || _initInProgress ||
        _core.isBusy() || _core.queuedCount()) return;
    if (!_core.beginTransaction(TX_SMS_SCAN)) return;
    _smsScanActive = true;
    _smsScanStage = 0;
    _smsLastScan = millis();
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "AT+CPMS=\"SM\",\"SM\",\"SM\"");
    Result r = enqueueSmsCommand(cmd, static_cast<uint16_t>(DriverRequest::SMS_SCAN_SELECT), 5000, TX_SMS_SCAN);
    if (r != Result::OK) advanceSmsScan(r);
}

void SIM800L::advanceSmsScan(Result result) {
    if (!_smsScanActive) return;
    if (result != Result::OK) {
        if (_cb.onSmsError) _cb.onSmsError(_smsScanStage == 0 ? "SM" : "ME", 0, result);
    }
    if (_smsScanStage == 0) {
        _smsScanStage = 1;
        char cmd[48];
        snprintf(cmd, sizeof(cmd), "AT+CPMS=\"ME\",\"ME\",\"ME\"");
        Result r = enqueueSmsCommand(cmd, static_cast<uint16_t>(DriverRequest::SMS_SCAN_SELECT), 5000, TX_SMS_SCAN);
        if (r != Result::OK) advanceSmsScan(r);
        return;
    }
    _smsScanActive = false;
    _core.endTransaction(TX_SMS_SCAN);
}

void SIM800L::startNextSms() {
    if (!_smsCount) return;
    PendingSMS item = _smsQueue[_smsHead];
    _smsHead = (_smsHead + 1) % 60;
    --_smsCount;
    strncpy(_smsActiveStorage, item.storage, sizeof(_smsActiveStorage) - 1);
    _smsActiveStorage[sizeof(_smsActiveStorage) - 1] = '\0';
    _smsActiveIndex = item.index;
    memset(&_smsCurrent, 0, sizeof(_smsCurrent));
    strncpy(_smsCurrent.storage, item.storage, sizeof(_smsCurrent.storage) - 1);
    _smsCurrent.index = item.index;
    _smsHeaderSeen = false;
    _smsReading = true;

    char cmd[48];
    snprintf(cmd, sizeof(cmd), "AT+CPMS=\"%s\",\"%s\",\"%s\"", item.storage, item.storage, item.storage);
    if (!_core.beginTransaction(TX_SMS_READ)) {
        // Put the item back at the head so it is not lost while another transaction owns Core.
        _smsHead = (_smsHead + 59) % 60;
        _smsQueue[_smsHead] = item;
        ++_smsCount;
        _smsReading = false;
        return;
    }
    Result r = enqueueSmsCommand(cmd, static_cast<uint16_t>(DriverRequest::SMS_SELECT), 5000, TX_SMS_READ);
    if (r != Result::OK) finishSmsRead(r);
}

static void copyQuotedField(const char*& p, char* out, size_t size) {
    if (!size) return;
    out[0] = '\0';
    while (*p && *p != '\"') ++p;
    if (!*p) return;
    ++p;
    size_t i = 0;
    while (*p && *p != '\"') { if (i + 1 < size) out[i++] = *p; ++p; }
    out[i] = '\0';
    if (*p == '\"') ++p;
}

void SIM800L::parseCmgrHeader(const char* line) {
    const char* p = strchr(line, ':');
    if (!p) return;
    ++p;
    copyQuotedField(p, _smsCurrent.status, sizeof(_smsCurrent.status));
    copyQuotedField(p, _smsCurrent.sender, sizeof(_smsCurrent.sender));
    // Alpha identifier (may be empty).
    char alpha[24];
    copyQuotedField(p, alpha, sizeof(alpha));
    copyQuotedField(p, _smsCurrent.timestamp, sizeof(_smsCurrent.timestamp));
    _smsHeaderSeen = true;
}

static bool isHexString(const char* s) {
    size_t n = strlen(s);
    if (!n || (n & 3)) return false;
    for (size_t i = 0; i < n; ++i) if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'A' && s[i] <= 'F') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}

static uint16_t hexWord(const char* p) {
    uint16_t v = 0;
    for (uint8_t i = 0; i < 4; ++i) {
        char c = p[i];
        uint8_t x = c <= '9' ? c - '0' : (c | 32) - 'a' + 10;
        v = (uint16_t)((v << 4) | x);
    }
    return v;
}

static size_t appendUtf8(char* out, size_t pos, size_t cap, uint32_t cp) {
    if (cp < 0x80) { if (pos + 1 < cap) out[pos++] = (char)cp; }
    else if (cp < 0x800) { if (pos + 2 < cap) { out[pos++] = 0xC0 | (cp >> 6); out[pos++] = 0x80 | (cp & 0x3F); } }
    else { if (pos + 3 < cap) { out[pos++] = 0xE0 | (cp >> 12); out[pos++] = 0x80 | ((cp >> 6) & 0x3F); out[pos++] = 0x80 | (cp & 0x3F); } }
    out[pos] = '\0';
    return pos;
}

static void decodeSmsBody(char* text, size_t capacity) {
    if (!isHexString(text)) return;
    char decoded[241] = {0};
    size_t pos = 0;
    const size_t n = strlen(text);
    for (size_t i = 0; i + 3 < n; i += 4) {
        uint32_t cp = hexWord(text + i);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 7 < n) {
            uint16_t low = hexWord(text + i + 4);
            if (low >= 0xDC00 && low <= 0xDFFF) { cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00)); i += 4; }
        }
        // UTF-8 4-byte code points are not expected in typical Russian SMS; replace safely.
        if (cp > 0xFFFF) cp = '?';
        pos = appendUtf8(decoded, pos, sizeof(decoded), cp);
    }
    strncpy(text, decoded, capacity - 1);
    text[capacity - 1] = '\0';
}

void SIM800L::parseSmsMemory(const char* line) {
    const char* p = strchr(line, ':');
    if (!p) return;
    ++p;
    while (*p == ' ') ++p;
    char storage[4] = {0};
    if (*p == '\"') { ++p; size_t i = 0; while (*p && *p != '\"' && i < 3) storage[i++] = *p++; storage[i] = '\0'; if (*p == '\"') ++p; }
    else strncpy(storage, _smsActiveStorage, sizeof(storage) - 1);
    if (*p == ',') ++p;
    _smsMemory.used = (uint8_t)atoi(p);
    p = strchr(p, ','); if (!p) return; ++p;
    _smsMemory.total = (uint8_t)atoi(p);
    strncpy(_smsMemory.storage, storage, sizeof(_smsMemory.storage) - 1);
    _smsMemory.storage[sizeof(_smsMemory.storage) - 1] = '\0';
    if (_cb.onSmsMemory) _cb.onSmsMemory(_smsMemory);
    if (_smsMemory.total && (uint16_t)_smsMemory.used * 100 >= (uint16_t)_smsMemory.total * 80 &&
        _smsMemory.used < _smsMemory.total && _cb.onSmsMemoryWarning) _cb.onSmsMemoryWarning(_smsMemory);
    if (_smsMemory.total && _smsMemory.used >= _smsMemory.total && _cb.onSmsMemoryFull) _cb.onSmsMemoryFull(_smsMemory);
}

void SIM800L::finishSmsRead(Result result) {
    if (!_smsReading) return;

    const bool validMessage = (result == Result::OK && _smsHeaderSeen);
    if (validMessage) decodeSmsBody(_smsCurrent.text, sizeof(_smsCurrent.text));
    else {
        setSmsProcessed(_smsActiveStorage, _smsActiveIndex, false);
        if (_cb.onSmsError)
            _cb.onSmsError(_smsActiveStorage, _smsActiveIndex,
                           result == Result::OK ? Result::ERROR : result);
    }

    _smsReading = false;
    _smsHeaderSeen = false;
    _core.endTransaction(TX_SMS_READ);

    // Release Core before calling user code: the SMS callback may send a reply.
    bool handled = false;
    if (validMessage && _cb.onSmsMessage) handled = _cb.onSmsMessage(_smsCurrent);
    if (validMessage && _smsAutoDelete && handled) _smsDeletePending = true;
}

void SIM800L::parseCusd(const char* line) {
    const char* p = strchr(line, ':');
    if (!p) return;
    ++p;
    while (*p == ' ') ++p;

    const int m = atoi(p);
    const char* firstQuote = strchr(p, '"');
    if (!firstQuote) {
        const RequestInfo request = _ussdPending ? _ussdRequest : RequestInfo();
        _ussdPending = false;
        _ussdRequest = RequestInfo();
        _ussdStartedAt = 0;
        if (_cb.onUssdResponse)
            _cb.onUssdResponse(static_cast<uint8_t>(m), "", request);
        return;
    }

    const char* textStart = firstQuote + 1;
    const char* textEnd = strchr(textStart, '"');
    if (!textEnd) return;

    char text[241];
    size_t len = static_cast<size_t>(textEnd - textStart);
    if (len >= sizeof(text)) len = sizeof(text) - 1;
    memcpy(text, textStart, len);
    text[len] = '\0';

    // DCS follows the quoted response. SIM800 commonly reports 72 (0x48)
    // for UCS2. Decode UTF-16BE HEX to UTF-8 so Russian text is usable by app.
    const char* dcsPos = strchr(textEnd + 1, ',');
    const int dcs = dcsPos ? atoi(dcsPos + 1) : -1;
    if ((dcs == 72) && isHexString(text)) {
        char decoded[241] = {0};
        size_t out = 0;
        const size_t hexLen = strlen(text);
        for (size_t i = 0; i + 3 < hexLen; i += 4) {
            uint32_t cp = hexWord(text + i);
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 7 < hexLen) {
                const uint16_t low = hexWord(text + i + 4);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
                    i += 4;
                }
            }
            if (cp > 0xFFFF) cp = '?';
            out = appendUtf8(decoded, out, sizeof(decoded), cp);
        }
        strncpy(text, decoded, sizeof(text) - 1);
        text[sizeof(text) - 1] = '\0';
    }

    const RequestInfo request = _ussdPending ? _ussdRequest : RequestInfo();
    _ussdPending = false;
    _ussdRequest = RequestInfo();
    _ussdStartedAt = 0;
    if (_cb.onUssdResponse)
        _cb.onUssdResponse(static_cast<uint8_t>(m), text, request);
}

} // namespace gsm
