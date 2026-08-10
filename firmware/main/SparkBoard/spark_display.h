#ifndef SPARK_DISPLAY_H
#define SPARK_DISPLAY_H

#include "display.h"

class SparkDisplay : public Display {
public:
    SparkDisplay();
    virtual ~SparkDisplay();

    virtual void SetStatus(const char* status) override;
    virtual void ShowNotification(const char* notification, int duration_ms = 3000) override;
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void UpdateStatusBar(bool update_all = false) override;
    virtual void SetPowerSaveMode(bool on) override;

protected:
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;
};

#endif // SPARK_DISPLAY_H
