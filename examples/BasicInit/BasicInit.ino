#include <Arduino.h>
#include <GSM.h>

gsm::GSM modem(Serial1);

void onReady() {
    Serial.println(F("[GSM] Modem fully ready (SIM + SMS)"));
}

void onInitCompleteEx(const gsm::InitResult& r) {
    Serial.println(F("--------- GSM init ---------"));
    Serial.print(F("  result:    "));
    Serial.println(r.message);
    Serial.print(F("  available: "));
    Serial.println(r.available ? F("yes") : F("no"));
    Serial.print(F("  simReady:  "));
    Serial.println(r.simReady ? F("yes") : F("no"));
    Serial.print(F("  smsReady:  "));
    Serial.println(r.smsReady ? F("yes") : F("no"));
    if (r.cme >= 0) {
        Serial.print(F("  CME:       "));
        Serial.print(r.cme);
        Serial.print(F(" ("));
        Serial.print(gsm::cmeToStr(r.cme));
        Serial.println(F(")"));
    }
    Serial.println(F("----------------------------"));
}

void onModemError(const gsm::ModemError& err) {
    Serial.print(F("[GSM] Error ["));
    Serial.print(err.context);
    Serial.print(F("]: "));
    Serial.println(err.message);
}

void onSimStatus(const char* status) {
    Serial.print(F("[GSM] SIM: "));
    Serial.println(status);
}

void onRegistration(uint8_t n, uint8_t stat) {
    Serial.print(F("[GSM] CREG n="));
    Serial.print(n);
    Serial.print(F(" stat="));
    Serial.println(stat);
}

void onSignal(uint8_t rssi, uint8_t ber) {
    Serial.print(F("[GSM] CSQ "));
    Serial.print(rssi);
    Serial.print(',');
    Serial.println(ber);
}

void onOperator(const char* name) {
    Serial.print(F("[GSM] Operator: "));
    Serial.println(name);
}

void onIncomingCall(const char* number) {
    Serial.print(F("[GSM] Incoming: "));
    Serial.println(number);
}

void onCallEnded() {
    Serial.println(F("[GSM] Call ended"));
}

void onSmsReceived(int index) {
    Serial.print(F("[GSM] SMS index "));
    Serial.println(index);
}

void onRawLine(const char* line) {
    Serial.print(F("[GSM][Raw] "));
    Serial.println(line);
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000) {}

#if defined(ESP32)
    Serial1.begin(9600, SERIAL_8N1, 16, 17);
#else
    Serial1.begin(9600);
#endif

    gsm::ModemCallbacks cb = {};
    cb.onReady          = onReady;
    cb.onInitCompleteEx = onInitCompleteEx;
    cb.onModemError     = onModemError;
    cb.onSimStatus      = onSimStatus;
    cb.onRegistration   = onRegistration;
    cb.onSignal         = onSignal;
    cb.onOperator       = onOperator;
    cb.onIncomingCall   = onIncomingCall;
    cb.onCallEnded      = onCallEnded;
    cb.onSmsReceived    = onSmsReceived;
    cb.onRawLine        = onRawLine;
    modem.setCallbacks(cb);

    modem.begin();
    modem.init();
}

void loop() {
    modem.process();

    static uint32_t lastPrint = 0;
    if (millis() - lastPrint >= 5000) {
        lastPrint = millis();
        Serial.print(F("[GSM] responsive="));
        Serial.print(modem.isResponsive() ? F("yes") : F("no"));
        Serial.print(F(" registered="));
        Serial.print(modem.isNetworkConnected() ? F("yes") : F("no"));
        Serial.print(F(" signal="));
        Serial.print(modem.signalQuality());
        Serial.print(F(" ("));
        Serial.print(modem.signalDbm());
        Serial.print(F(" dBm) operator="));
        Serial.print(modem.hasOperatorData() ? modem.operatorName() : "unknown");
        Serial.print(F(" SIM="));
        Serial.println(modem.simStatus());
    }
}
