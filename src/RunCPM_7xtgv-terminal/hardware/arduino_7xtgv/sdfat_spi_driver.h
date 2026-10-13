#pragma once

// One transport implementation, not a board selector: pins, SPI instance and
// mounting remain in each complete board header. Only the SD-owner task calls
// this driver. SdFat invokes setSckSpeed during its low-speed initialization.
#include <SPI.h>
#include <SdFat.h>
#include <cstring>

#if SPI_DRIVER_SELECT != 3
#error "Runtime SPI clock selection requires build-wide SPI_DRIVER_SELECT=3"
#endif

class XtgvSdSpiDriver : public SdSpiBaseClass
{
public:
    explicit XtgvSdSpiDriver(SPIClass& spi) : spi_(spi)
    {
    }

    void begin(SdSpiConfig) override
    {
        spi_.begin();
    }

    void end() override
    {
        deactivate();
    }

    void activate() override
    {
        spi_.beginTransaction(SPISettings(hz_, MSBFIRST, SPI_MODE0));
        active_ = true;
    }

    void deactivate() override
    {
        if (active_)
            spi_.endTransaction();
        active_ = false;
    }

    void setSckSpeed(uint32_t hz) override
    {
        hz_ = hz;
    }

    uint32_t requestedHz() const
    {
        return hz_;
    }

    // Caller first finishes any card multi-block operation via syncDevice().
    bool changeClock(uint32_t hz)
    {
        if (active_ || hz < 1000000UL || hz > 60000000UL)
            return false;
        hz_ = hz;
        return true;
    }

    uint8_t receive() override
    {
        return spi_.transfer(0xff);
    }

    void send(uint8_t value) override
    {
        spi_.transfer(value);
    }

    uint8_t receive(uint8_t* data, size_t size) override
    {
#if USE_SPI_ARRAY_TRANSFER == 0
        for (size_t i = 0; i < size; ++i)
            data[i] = receive();
#elif USE_SPI_ARRAY_TRANSFER == 1
        memset(data, 0xff, size);
        spi_.transfer(data, size);
#elif USE_SPI_ARRAY_TRANSFER == 2
        spi_.transfer(nullptr, data, size);
#else
#error "Unsupported SPI buffer policy"
#endif
        return 0;
    }

    void send(const uint8_t* data, size_t size) override
    {
#if USE_SPI_ARRAY_TRANSFER == 0
        for (size_t i = 0; i < size; ++i)
            send(data[i]);
#elif USE_SPI_ARRAY_TRANSFER == 1
        uint8_t scratch[512];
        while (size)
        {
            const size_t count = size < sizeof(scratch) ? size : sizeof(scratch);
            memcpy(scratch, data, count);
            spi_.transfer(scratch, count);
            data += count;
            size -= count;
        }
#elif USE_SPI_ARRAY_TRANSFER == 2
        spi_.transfer(const_cast<uint8_t*>(data), nullptr, size);
#else
#error "Unsupported SPI buffer policy"
#endif
    }

private:
    SPIClass& spi_;
    uint32_t hz_ = 20000000UL;
    bool active_ = false;
};
