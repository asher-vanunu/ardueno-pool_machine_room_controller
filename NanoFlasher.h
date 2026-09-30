#ifndef NANO_FLASHER_H
#define NANO_FLASHER_H

#include <Arduino.h>
#include <Print.h>

// מחלקה ללכידת ההדפסות גם ל-USB וגם לפורטל מרחוק
class WebSerialPrint : public Print {
public:
    String buffer;
    bool webOutputEnabled = false; // ברירת מחדל: מבוטל (ידפיס רק ל-USB)
    
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buf, size_t size) override;
    void begin(unsigned long baud);
};

extern WebSerialPrint WebSerial;

void setupNanoFlasher();
void handleNanoFlasher();
void triggerNanoReset();

bool flashNanoHex(const uint8_t* hexData, size_t hexLen);

#endif