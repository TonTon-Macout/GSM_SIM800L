/**
 * Example: using the Auth module with SMS and incoming calls.
 *
 * - Numbers are added once in setup() (you load them from EEPROM / Preferences yourself).
 * - Unauthorized SMS are ignored.
 * - Unauthorized calls are immediately hung up.
 */

#include <GSM.h>

gsm::GSM modem(Serial1);   // change to your Serial
gsm::Auth auth;

// ---------- callbacks ----------

bool onSmsMessage(const gsm::SMSMessage& sms) {
    if (!auth.isAllowed(sms)) {
        // unauthorized — ignore (and optionally delete)
        Serial.print(F("SMS rejected from "));
        Serial.println(sms.sender);
        return true;   // mark as handled → auto-delete if enabled
    }

    Serial.print(F("SMS from authorized "));
    Serial.print(sms.sender);
    Serial.print(F(": "));
    Serial.println(sms.text);

    // here you can parse commands, reply, etc.
    return true;
}

void onIncomingCall(const char* number) {
    if (!auth.isAllowedCall(number)) {
        Serial.print(F("Call rejected from "));
        Serial.println(number);
        modem.hangUp();
        return;
    }

    Serial.print(F("Incoming call from authorized "));
    Serial.println(number);
    // modem.answerCall(); // if you want to answer automatically
}

void onInitCompleteEx(const gsm::InitResult& result) {
    Serial.print(F("Init finished: "));
    Serial.println(result.message ? result.message : "?");
}

// ---------- Arduino ----------

void setup() {
    Serial.begin(115200);
    while (!Serial) {}

    // ---- load your numbers here (from EEPROM, Preferences, hard-coded, etc.) ----
    // Fixed slot indexes: removing/replacing one number never shifts others.
    auth.set(0, "+79091234567");
    auth.set(1, "89098887766");
    auth.set(5, "79161234567");

    // Add to the first free slot:
    auth.add("+79090000000");

    // Remove only slot 1; slots 0 and 5 keep their indexes:
    // auth.removeAt(1);

    auth.setMode(gsm::AuthMode::WHITELIST);
    // auth.setMode(gsm::AuthMode::DISABLE); // temporarily allow everyone

    Serial.print(F("Authorized numbers: "));
    Serial.println(auth.count());

    // Iterate over all fixed slots, not count() slots:
    for (uint8_t i = 0; i < auth.capacity(); ++i) {
        const char* number = auth.get(i);
        if (!number) continue;
        Serial.print(i);
        Serial.print(F(": "));
        Serial.println(number);
    }

    // modem setup
    gsm::ModemCallbacks cb = {};
    cb.onInitCompleteEx = onInitCompleteEx;
    cb.onSmsMessage     = onSmsMessage;
    cb.onIncomingCall   = onIncomingCall;

    modem.setCallbacks(cb);
    modem.begin();
    modem.init();
}

void loop() {
    modem.process();
}
