#ifndef CONFIG_H
#define CONFIG_H

// --- Development & Debug Mode ---
// שים בהערה (//) ברגע שאתה מחבר את מודול ה-MAX485 הפיזי למנוע קריסות מפינים צפים
#define MODBUS_MOCK_MODE

// --- MQTT Server Settings ---
#define MQTT_SERVER "10.100.102.27"
#define MQTT_PORT   1883

// --- Pin Definitions ---
#define RELAY_PUMP_PIN   26
#define BOOT_BUTTON_PIN  0

// --- Aquagem Modbus RS485 Settings ---
#define RS485_RX_PIN     14     // פין RO במודול MAX485
#define RS485_TX_PIN     13     // פין DI במודול MAX485
#define RS485_DIR_PIN    12     // פינים DE + RE מחוברים יחד
#define MODBUS_SLAVE_ID  0xAA   // Aquagem default slave address (170 in decimal)
#define MODBUS_BAUDRATE  9600

// --- Aquagem Registers (Model 1.05kW) ---
#define REG_READ_STATE   0x07D2 // Running status & RS485 control flag
#define REG_READ_CAP     0x07D3 // Running capacity %
#define REG_READ_POWER   0x07D4 // Power in Watts
#define REG_READ_FLOW    0x07D5 // Actual Flow rate m3/h
#define REG_READ_ENERGY  0x07D7 // kWh (value / 1000)

// --- Arduino Nano Serial UART Settings ---
#define NANO_UART_NUM    1
#define NANO_RX_PIN      15     // GPIO15 (connected to Nano TX / D1)
#define NANO_TX_PIN      2      // GPIO2 (connected to Nano RX / D0 via voltage divider)
#define NANO_BAUDRATE    19200

#define REG_WRITE_CAP    0x0BB9 // Capacity (30-120%, 10 = OFF)
#define REG_WRITE_FLOW   0x0BBD // Flow rate m3/h (8-25 m3/h for 1.05kW, 0 = OFF)

#endif