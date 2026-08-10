#ifndef ASSETS_H
#define ASSETS_H

#include <functional>
#include <memory>
#include <string>

#include <esp_partition.h>
#include <cJSON.h>
#include <model_path.h>
#include <map>
#include <string>

#if HAVE_LVGL
#include <spi_flash_mmap.h>
#endif

struct Asset {
    size_t size;
    size_t offset;
};

struct TextFontCapability {
    bool glyph_push = false;
    std::string bundle;
    std::string charset;
    int size = 0;
    int bpp = 0;
};

class Assets {
public:
    static Assets& GetInstance() {
        static Assets instance;
        return instance;
    }
    ~Assets();

    bool Download(std::string url,
                  std::function<void(int progress, size_t speed)> progress_callback);
    bool Apply(bool refresh_display_theme = true);
    bool GetAssetData(const std::string& name, void*& ptr, size_t& size);

    inline bool partition_valid() const { return partition_valid_; }
    inline std::string default_assets_url() const { return default_assets_url_; }
    inline TextFontCapability text_font_capability() const { return text_font_capability_; }

private:
    Assets();
    Assets(const Assets&) = delete;
    Assets& operator=(const Assets&) = delete;

    bool InitializePartition();
    void UnApplyPartition();
    static bool FindPartition(Assets* assets);
    static bool LoadSrmodelsFromIndex(Assets* assets, cJSON* root = nullptr);
    void UseBuiltInTextFontCapability();
    void DisableTextFontGlyphPush();



protected:
    const esp_partition_t* partition_ = nullptr;
    bool partition_valid_ = false;
    std::string default_assets_url_;
    TextFontCapability text_font_capability_;
    srmodel_list_t* models_list_ = nullptr;
};

#endif
