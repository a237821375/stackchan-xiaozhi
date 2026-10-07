#pragma once
struct TouchPoint {
    int num = 0, x = -1, y = -1;
    template <class Reader>
    bool Read(Reader reader) {
        unsigned char data[6] = {};
        if (!reader(data) || (data[0] & 15) > 2) {
            num = 0;
            x = y = -1;
            return false;
        }
        num = data[0] & 15;
        x = ((data[1] & 15) << 8) | data[2];
        y = ((data[3] & 15) << 8) | data[4];
        return true;
    }
};
