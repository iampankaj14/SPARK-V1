#ifndef SPARK_BOARD_H
#define SPARK_BOARD_H

#include "wifi_board.h"
#include "spark_audio_codec.h"
#include "spark_display.h"

class SparkBoard : public WifiBoard {
public:
    SparkBoard();
    virtual ~SparkBoard();

    virtual std::string GetBoardType() override;
    virtual AudioCodec* GetAudioCodec() override;
    virtual Display* GetDisplay() override;
    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override;
    virtual std::string GetBoardJson() override;
};

#endif // SPARK_BOARD_H
