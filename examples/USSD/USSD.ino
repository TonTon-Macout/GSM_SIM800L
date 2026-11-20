#include <GSM.h>

gsm::GSM modem(Serial1);

void onUssdResponse(uint8_t status, const char* text, const gsm::RequestInfo& request) {
    Serial.print(F("USSD status: "));
    Serial.println(status);
    Serial.print(F("USSD answer: "));
    Serial.println(text); // Russian UCS2 answers are converted to UTF-8
    Serial.print(F("Request ID: "));
    Serial.println(request.requestId);
    Serial.print(F("Source: "));
    Serial.println(static_cast<uint8_t>(request.source));
}

void setup() {
    Serial.begin(115200);
    Serial1.begin(9600);

    gsm::ModemCallbacks callbacks = {};
    callbacks.onUssdResponse = onUssdResponse;
    modem.setCallbacks(callbacks);

    modem.begin();
    modem.init();
}

void loop() {
    modem.process();

    // After modem initialization and network registration:
    // modem.sendUSSD("*100#");
}
