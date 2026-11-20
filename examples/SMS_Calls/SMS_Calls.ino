#include <GSM.h>

gsm::GSM modem(Serial1);

void onSmsSent(const char* number, gsm::Result result) {
    Serial.print(F("SMS to "));
    Serial.print(number);
    Serial.print(F(": "));
    Serial.println(gsm::resultToStr(result));
}

void onIncomingCall(const char* number) {
    Serial.print(F("Incoming call: "));
    Serial.println(number);
    // Answer automatically if required:
    // modem.answerCall();
}

void onCallAnswered() {
    Serial.println(F("Call answered"));
}

void onDialStarted(const char* number) {
    Serial.print(F("Dial command accepted: "));
    Serial.println(number);
}

void onCallEnded() {
    Serial.println(F("Call ended"));
}

void onCallState(gsm::CallState state, const char* number) {
    Serial.print(F("Call state: "));
    switch (state) {
        case gsm::CallState::IDLE:       Serial.print(F("IDLE")); break;
        case gsm::CallState::RINGING:    Serial.print(F("RINGING")); break;
        case gsm::CallState::ANSWERING:  Serial.print(F("ANSWERING")); break;
        case gsm::CallState::ACTIVE:     Serial.print(F("ACTIVE")); break;
        case gsm::CallState::DIALING:    Serial.print(F("DIALING")); break;
        case gsm::CallState::ENDED:      Serial.print(F("ENDED")); break;
        case gsm::CallState::BUSY:       Serial.print(F("BUSY")); break;
        case gsm::CallState::FAILED:     Serial.print(F("FAILED")); break;
    }
    Serial.print(F("; number: "));
    Serial.println(number);
}

void onRecoveryRequired(uint8_t attempt) {
    Serial.print(F("Modem is not responding. Recovery attempt: "));
    Serial.println(attempt);
    // Do hardware RESET / PWRKEY / power control here.
    // Do not call modem.init() here; the driver does it after AT responds.
}

void onError(gsm::Result result, const char* context) {
    Serial.print(F("GSM error in "));
    Serial.print(context);
    Serial.print(F(": "));
    Serial.println(gsm::resultToStr(result));
}

void setup() {
    Serial.begin(115200);
    Serial1.begin(9600);

    gsm::ModemCallbacks callbacks = {};
    callbacks.onSmsSent = onSmsSent;
    callbacks.onIncomingCall = onIncomingCall;
    callbacks.onCallAnswered = onCallAnswered;
    callbacks.onDialStarted = onDialStarted;
    callbacks.onCallEnded = onCallEnded;
    callbacks.onCallState = onCallState;
    callbacks.onRecoveryRequired = onRecoveryRequired;
    callbacks.onError = onError;
    modem.setCallbacks(callbacks);

    modem.begin();
    modem.init();

    modem.setRecoveryTimeout(60000);
    modem.setMaxRecoveryAttempts(3);
    modem.setRecoveryEnabled(true);
}

void loop() {
    modem.process();

    // Use after modem initialisation and network registration:
    // modem.sendSMS("+79091234567", "Проверка SMS на русском");
    // modem.answerCall();
    // modem.hangUp();
    // modem.dial("+79091234567");
}
