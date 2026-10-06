#pragma once
#include <esp_log.h>
#include <array>
#include <cstdio>
#include "factory_upstream/ftservo/SCSCL.h"
#include "hal/uart_ll.h"
#include "servo_diagnostics.h"
#include "servo_tx_frame.h"
// Keep the factory packet codec and UART initialization; adapt transfer granularity
// to avoid scheduling gaps inside one 1 Mbps servo request.
class BufferedServoBus : public SCSCL {
public:
    // Same UART configuration as factory SCSerial::begin, adding an event queue
    // so hardware framing/overflow errors can be distinguished from FTServo errors.
    bool begin(uart_port_t port, int baud, int tx_pin, int rx_pin, int buf_size = 1024) {
        uart_config_t config{};
        config.baud_rate = baud;
        config.data_bits = UART_DATA_8_BITS;
        config.parity = UART_PARITY_DISABLE;
        config.stop_bits = UART_STOP_BITS_1;
        config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        config.source_clk = UART_SCLK_DEFAULT;
        if (uart_driver_install(port, buf_size, buf_size, 64, &events_, 0) != ESP_OK)
            return false;
        if (uart_param_config(port, &config) != ESP_OK ||
            uart_set_pin(port, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK ||
            // IDF's default interrupt mask omits FRAME_ERR; explicitly observe it.
            uart_clear_intr_status(port, UART_INTR_FRAM_ERR) != ESP_OK ||
            uart_enable_intr_mask(port, UART_INTR_FRAM_ERR) != ESP_OK) {
            uart_driver_delete(port);
            events_ = nullptr;
            return false;
        }
        uart_num = port;
        ESP_LOGI("StackchanHead",
                 "UART diagnostics ready: event queue=64; frame errors enabled; baud=%d", baud);
        return true;
    }
    void RecordOutcome(uint8_t id, bool read, bool ok) {
        DrainEvents();
        stats_.Transaction(id, read, ok, getLastError(), getState());
    }
    void PrintDiagnostics(int64_t now) {
        DrainEvents();
        uint32_t baud = 0;
        const bool baud_valid = uart_get_baudrate(uart_num, &baud) == ESP_OK;
        ESP_LOGI(
            "StackchanHead",
            "UART_STATS "
            "{\"at_ms\":%lld,\"baud\":%lu,\"baud_valid\":%s,\"data_events\":%lu,\"data_bytes\":%lu,"
            "\"frame_errors\":%lu,\"parity_errors\":%lu,\"fifo_overflows\":%lu,"
            "\"buffer_full\":%lu,\"breaks\":%lu,\"queue_full_observations\":%lu}",
            static_cast<long long>(now), static_cast<unsigned long>(baud),
            baud_valid ? "true" : "false", static_cast<unsigned long>(stats_.data_events),
            static_cast<unsigned long>(stats_.data_bytes),
            static_cast<unsigned long>(stats_.frame_errors),
            static_cast<unsigned long>(stats_.parity_errors),
            static_cast<unsigned long>(stats_.fifo_overflows),
            static_cast<unsigned long>(stats_.buffer_full),
            static_cast<unsigned long>(stats_.breaks),
            static_cast<unsigned long>(queue_full_observations_));
        for (unsigned i = 0; i < 2; ++i) {
            const auto& a = stats_.axis[i];
            ESP_LOGI(
                "StackchanHead",
                "SERVO_STATS {\"at_ms\":%lld,\"id\":%u,\"reads\":%lu,"
                "\"read_failures\":%lu,\"writes\":%lu,\"write_failures\":%lu,\"missing\":%lu,"
                "\"checksum\":%lu,\"wrong_id\":%lu,\"wrong_length\":%lu,\"alarms\":%lu,\"other\":%"
                "lu}",
                static_cast<long long>(now), i + 1, static_cast<unsigned long>(a.reads),
                static_cast<unsigned long>(a.read_failures), static_cast<unsigned long>(a.writes),
                static_cast<unsigned long>(a.write_failures), static_cast<unsigned long>(a.missing),
                static_cast<unsigned long>(a.checksum), static_cast<unsigned long>(a.wrong_id),
                static_cast<unsigned long>(a.wrong_length), static_cast<unsigned long>(a.alarms),
                static_cast<unsigned long>(a.other));
        }
    }
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
    QueueHandle_t events_ = nullptr;
    stackchan::ServoDiagnostics stats_;
    uint32_t queue_full_observations_ = 0;
    void DrainEvents() {
        if (!events_)
            return;
        if (uxQueueMessagesWaiting(events_) >= 64)
            ++queue_full_observations_;  // A full queue may have dropped events; counts are lower
                                         // bounds.
        uart_event_t event;
        // A bounded drain preserves motion timing even if UART_DATA is busy.
        for (unsigned i = 0; i < 64 && xQueueReceive(events_, &event, 0) == pdTRUE; ++i) {
            using stackchan::RxError;
            switch (event.type) {
                case UART_DATA:
                    stats_.Data(event.size);
                    break;
                case UART_FRAME_ERR:
                    stats_.Error(RxError::Frame);
                    break;
                case UART_PARITY_ERR:
                    stats_.Error(RxError::Parity);
                    break;
                case UART_FIFO_OVF:
                    stats_.Error(RxError::FifoOverflow);
                    break;
                case UART_BUFFER_FULL:
                    stats_.Error(RxError::BufferFull);
                    break;
                case UART_BREAK:
                    stats_.Error(RxError::Break);
                    break;
                default:
                    break;
            }
        }
    }
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
