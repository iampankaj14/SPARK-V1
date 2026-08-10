#include "assets.h"
#include <esp_log.h>

#define TAG "Assets"

Assets::Assets() {
    UseBuiltInTextFontCapability();
    partition_valid_ = false;
}

Assets::~Assets() {}

bool Assets::FindPartition(Assets* assets) {
    return false;
}

bool Assets::Apply(bool refresh_display_theme) {
    return false;
}

bool Assets::InitializePartition() {
    return false;
}

void Assets::UnApplyPartition() {
    UseBuiltInTextFontCapability();
}

void Assets::UseBuiltInTextFontCapability() {
    text_font_capability_ = {
        .glyph_push = false,
        .bundle = "default",
        .charset = "basic",
        .size = 14,
        .bpp = 1,
    };
}

void Assets::DisableTextFontGlyphPush() {
    text_font_capability_ = {};
}

bool Assets::GetAssetData(const std::string& name, void*& ptr, size_t& size) {
    return false;
}

bool Assets::Download(std::string url, std::function<void(int progress, size_t speed)> progress_callback) {
    return false;
}

bool Assets::LoadSrmodelsFromIndex(Assets* assets, cJSON* root) {
    return false;
}
