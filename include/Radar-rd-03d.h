#pragma once
#include <Arduino.h>

// RD-03D 24GHz radar sensor — UART frame protocol
// Frame: AA FF 03 00 [target_count:1] [x:2] [y:2] [speed:2] [resolution:2] [crc:2] 55 CC

struct RadarTarget {
    int16_t x;          // mm
    int16_t y;          // mm
    int16_t speed;      // cm/s
};

class RadarRD03D {
public:
    static constexpr uint32_t BAUD_RATE = 256000;
    static constexpr uint8_t  MAX_TARGETS = 3;

    bool    person_detected = false;
    uint8_t targetCount = 0;
    RadarTarget targets[MAX_TARGETS];

    void begin(HardwareSerial &serial, uint8_t rxPin, uint8_t txPin) {
        _serial = &serial;
        _serial->begin(BAUD_RATE, SERIAL_8N1, rxPin, txPin);
        _bufLen = 0;
    }

    void poll() {
        while (_serial && _serial->available()) {
            uint8_t b = _serial->read();
            //put byte into ring buffer
            if (_bufLen < BUF_SIZE) {
                _buf[_bufLen++] = b;
            } else {
                memmove(_buf, _buf + 1, BUF_SIZE - 1);
                _buf[BUF_SIZE - 1] = b;
            }
            _tryParse();
        }
    }

private:
    static constexpr uint8_t BUF_SIZE  = 30;
 
    HardwareSerial *_serial = nullptr;
    uint8_t _buf[BUF_SIZE];
    uint8_t _bufLen = 0;

    void _tryParse() {
        //scan for header AA FF 03 00
        for (int i = 0; i + BUF_SIZE <= _bufLen; i++) {
            if (_buf[i]   == 0xAA && _buf[i+1] == 0xFF &&
                _buf[i+2] == 0x03 && _buf[i+3] == 0x00) {
                //check tail 55 CC
                if (_buf[i + BUF_SIZE-2] == 0x55 &&
                    _buf[i + BUF_SIZE-1] == 0xCC) {
                    _parseFrame(&_buf[i]);
                    //consume parsed frame
                    uint8_t end = i + BUF_SIZE;
                    memmove(_buf, _buf + end, _bufLen - end);
                    _bufLen -= end;
                    return;
                }
            }
        }
    }

    void _parseFrame(const uint8_t *f) {
        targetCount = min((uint8_t)f[4], (uint8_t)MAX_TARGETS);
        person_detected    = targetCount > 0;
        for (uint8_t t = 0; t < targetCount; t++) {
            const uint8_t *p = &f[5 + t * 6];
            targets[t].x          = (int16_t)((p[1] << 8) | p[0]);
            targets[t].y          = (int16_t)((p[3] << 8) | p[2]);
            targets[t].speed      = (int16_t)((p[5] << 8) | p[4]);
        }
    }
};
