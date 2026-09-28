#pragma once
#include <esp_log.h>
#include <array>
#include <cstdio>
#include "factory_upstream/ftservo/SCSCL.h"
#include "servo_tx_frame.h"
// Keep the factory packet codec and UART initialization; adapt transfer granularity
// to avoid scheduling gaps inside one 1 Mbps servo request.
class BufferedServoBus : public SCSCL {
public:
    // Called only after a failed transaction, by the sole UART owner. Preserve
    // the wire bytes before the normal recovery flush discards them.
    void DumpFailure() {
        uint8_t tail[64];
        const int count = uart_read_bytes(uart_num, tail, sizeof(tail), 0);
        if (count > 0)
            Capture(rx_trace_, rx_size_, tail, count);
        const auto tx = Hex(tx_trace_, tx_size_);
        const auto rx = Hex(rx_trace_, rx_size_);
        ESP_LOGW("StackchanHead", "Bus trace tx=[%s] rx=[%s] tx_ok=%d", tx.data(), rx.data(),
                 tx_ok_);
    }

protected:
    int writeSCS(unsigned char* p, int n) override { return tx_.Append(p, n); }
    int writeSCS(unsigned char b) override { return tx_.Append(&b, 1); }
    void rFlushSCS() override {
        tx_.Clear();
        tx_size_ = rx_size_ = 0;
        uart_flush_input(uart_num);
    }
    void wFlushSCS() override {
        tx_ok_ = tx_.Flush([this](const uint8_t* p, size_t n) {
            Capture(tx_trace_, tx_size_, p, n);
            return uart_write_bytes(uart_num, p, n);
        });
        if (tx_ok_)
            tx_ok_ = uart_wait_tx_done(uart_num, pdMS_TO_TICKS(30)) == ESP_OK;
    }
    int readSCS(unsigned char* p, int n) override { return readSCS(p, n, 20); }
    int readSCS(unsigned char* p, int n, unsigned long timeout) override {
        if (!tx_ok_ || !p || n <= 0)
            return 0;
        const auto ticks = std::max<TickType_t>(1, pdMS_TO_TICKS(timeout));
        const int count = uart_read_bytes(uart_num, p, n, ticks);
        if (count > 0)
            Capture(rx_trace_, rx_size_, p, count);
        return count < 0 ? 0 : count;
    }

private:
    static void Capture(std::array<uint8_t, 64>& trace, size_t& size, const uint8_t* p, size_t n) {
        for (size_t i = 0; i < n && size < trace.size(); ++i)
            trace[size++] = p[i];
    }
    static std::array<char, 193> Hex(const std::array<uint8_t, 64>& trace, size_t size) {
        std::array<char, 193> result{};
        for (size_t i = 0; i < size; ++i)
            std::snprintf(result.data() + i * 3, 4, "%02x ", trace[i]);
        return result;
    }
    std::array<uint8_t, 64> tx_trace_{}, rx_trace_{};
    size_t tx_size_ = 0, rx_size_ = 0;
    stackchan::ServoTxFrame tx_;
    bool tx_ok_ = false;
};
