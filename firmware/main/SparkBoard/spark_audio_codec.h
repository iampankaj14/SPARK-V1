#ifndef SPARK_AUDIO_CODEC_H
#define SPARK_AUDIO_CODEC_H

#include "audio_codec.h"
#include <driver/i2s_std.h>
#include <mutex>

class SparkAudioCodec : public AudioCodec {
public:
    SparkAudioCodec(int input_sample_rate, int output_sample_rate);
    virtual ~SparkAudioCodec();

    virtual void SetOutputVolume(int volume) override;
    virtual void EnableInput(bool enable) override;
    virtual void EnableOutput(bool enable) override;

protected:
    virtual int Read(int16_t* dest, int samples) override;
    virtual int Write(const int16_t* data, int samples) override;

private:
    void InitializeI2sTx();
    void InitializeI2sRx();
    void DeinitializeI2sTx();
    void DeinitializeI2sRx();

    std::mutex mutex_;
    int32_t* rx_temp_buf_ = nullptr;
    int rx_temp_buf_samples_ = 0;
    float dc_offset_ = 0.0f;  // DC offset tracker for high-pass filter
};

#endif // SPARK_AUDIO_CODEC_H
