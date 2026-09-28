#pragma once
#include "factory_upstream/ftservo/SCSCL.h"
#include "servo_tx_frame.h"
// Keep the factory packet codec and UART initialization; adapt transfer granularity
// to avoid scheduling gaps inside one 1 Mbps servo request.
class BufferedServoBus : public SCSCL {
protected:
    int writeSCS(unsigned char* p, int n) override { return tx_.Append(p, n); }
    int writeSCS(unsigned char b) override { return tx_.Append(&b, 1); }
    void rFlushSCS() override {
        tx_.Clear();
        uart_flush_input(uart_num);
    }
    void wFlushSCS() override {
        tx_ok_ = tx_.Flush(
            [this](const uint8_t* p, size_t n) { return uart_write_bytes(uart_num, p, n); });
        if (tx_ok_)
            tx_ok_ = uart_wait_tx_done(uart_num, pdMS_TO_TICKS(30)) == ESP_OK;
    }
    int readSCS(unsigned char* p, int n) override { return readSCS(p, n, 20); }
    int readSCS(unsigned char* p, int n, unsigned long timeout) override {
        if (!tx_ok_ || !p || n <= 0)
            return 0;
        const auto ticks = std::max<TickType_t>(1, pdMS_TO_TICKS(timeout));
        const int count = uart_read_bytes(uart_num, p, n, ticks);
        return count < 0 ? 0 : count;
    }

private:
    stackchan::ServoTxFrame tx_;
    bool tx_ok_ = false;
};
