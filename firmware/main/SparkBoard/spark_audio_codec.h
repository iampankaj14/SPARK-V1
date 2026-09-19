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

    void StartRecordingDiagnostic();
    void StopAndDumpRecordingDiagnostic();

    // Virtual Mic Pipeline (Phone Background Voice Sync)
    void InjectVirtualMicAudio(const int16_t* data, int samples);

protected:
    virtual int Read(int16_t* dest, int samples) override;
    virtual int Write(const int16_t* data, int samples) override;

private:
    void InitializeI2sTx();
    void InitializeI2sRx();
    void DeinitializeI2sTx();
    void DeinitializeI2sRx();
    void RunI2sHardwareTest();

    std::mutex mutex_;
    int32_t* rx_temp_buf_ = nullptr;
    int rx_temp_buf_samples_ = 0;

    int16_t* diag_rec_buf_ = nullptr;
    int diag_rec_index_ = 0;
    bool diag_recording_active_ = false;

    // Auto-detected active mic channel: 0=LEFT, 1=RIGHT (set by RunI2sHardwareTest)
    int active_mic_channel_ = 0;

    // I2S channel handles
    i2s_chan_handle_t tx_handle_ = nullptr;
    i2s_chan_handle_t rx_handle_ = nullptr;

    // Ring buffer for virtual microphone audio injected from WebSockets/Phone
    std::vector<int16_t> virtual_mic_buf_;
};

#ifdef __cplusplus
extern "C" {
#endif
void InjectVirtualMicAudioFromC(const int16_t* data, int samples);
#ifdef __cplusplus
}
#endif

#endif // SPARK_AUDIO_CODEC_H
