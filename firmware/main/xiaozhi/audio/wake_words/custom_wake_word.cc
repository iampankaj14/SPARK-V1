#include "custom_wake_word.h"
#include "audio_service.h"
#include "system_info.h"
#include "assets.h"

#include <esp_log.h>
#include <esp_mn_iface.h>
#include <esp_mn_models.h>
#include <esp_mn_speech_commands.h>
#include <cJSON.h>

#define TAG "CustomWakeWord"

CustomWakeWord::CustomWakeWord()
    : wake_word_opus_() {
}

CustomWakeWord::~CustomWakeWord() {
    if (multinet_model_data_ != nullptr && multinet_ != nullptr) {
        multinet_->destroy(multinet_model_data_);
        multinet_model_data_ = nullptr;
    }

    if (wake_word_encode_task_stack_ != nullptr) {
        heap_caps_free(wake_word_encode_task_stack_);
    }

    if (wake_word_encode_task_buffer_ != nullptr) {
        heap_caps_free(wake_word_encode_task_buffer_);
    }

    if (owns_models_ && models_ != nullptr) {
        esp_srmodel_deinit(models_);
    }
}

void CustomWakeWord::ParseWakenetModelConfig() {
    // Read index.json
    auto& assets = Assets::GetInstance();
    void* ptr = nullptr;
    size_t size = 0;
    if (!assets.GetAssetData("index.json", ptr, size)) {
        ESP_LOGE(TAG, "Failed to read index.json");
        return;
    }
    cJSON* root = cJSON_ParseWithLength(static_cast<char*>(ptr), size);
    if (root == nullptr) {
        ESP_LOGE(TAG, "Failed to parse index.json");
        return;
    }
    cJSON* multinet_model = cJSON_GetObjectItem(root, "multinet_model");
    if (cJSON_IsObject(multinet_model)) {
        cJSON* language = cJSON_GetObjectItem(multinet_model, "language");
        cJSON* duration = cJSON_GetObjectItem(multinet_model, "duration");
        cJSON* threshold = cJSON_GetObjectItem(multinet_model, "threshold");
        cJSON* commands = cJSON_GetObjectItem(multinet_model, "commands");
        if (cJSON_IsString(language)) {
            language_ = language->valuestring;
        }
        if (cJSON_IsNumber(duration)) {
            duration_ = duration->valueint;
        }
        if (cJSON_IsNumber(threshold)) {
            threshold_ = threshold->valuedouble;
        }
        if (cJSON_IsArray(commands)) {
            for (int i = 0; i < cJSON_GetArraySize(commands); i++) {
                cJSON* command = cJSON_GetArrayItem(commands, i);
                if (cJSON_IsObject(command)) {
                    cJSON* command_name = cJSON_GetObjectItem(command, "command");
                    cJSON* text = cJSON_GetObjectItem(command, "text");
                    cJSON* action = cJSON_GetObjectItem(command, "action");
                    if (cJSON_IsString(command_name) && cJSON_IsString(text) && cJSON_IsString(action)) {
                        commands_.push_back({command_name->valuestring, text->valuestring, action->valuestring});
                        ESP_LOGI(TAG, "Command: %s, Text: %s, Action: %s", command_name->valuestring, text->valuestring, action->valuestring);
                    }
                }
            }
        }
    }
    cJSON_Delete(root);
}


bool CustomWakeWord::Initialize(AudioCodec* codec, srmodel_list_t* models_list) {
    codec_ = codec;
    commands_.clear();

    if (models_list == nullptr) {
        models_ = esp_srmodel_init("model");
        owns_models_ = models_ != nullptr;
    } else {
        models_ = models_list;
        ParseWakenetModelConfig();
    }

    if (commands_.empty()) {
        language_ = "en";
        duration_ = 3000;
        threshold_ = 0.010f; // Ultra-sensitive threshold for relaxed whispers and far-field speech
        
        // Commands mapping phonemes for MultiNet 5 English:
        // "Spark" calm, relaxed & whisper variants:
        commands_.push_back(Command{"SPnRK", "Spark", "wake"});
        commands_.push_back(Command{"SPaRK", "Spark", "wake"});
        commands_.push_back(Command{"SPcRK", "Spark", "wake"});
        commands_.push_back(Command{"SPnK", "Spark", "wake"});
        commands_.push_back(Command{"SPaK", "Spark", "wake"});
        commands_.push_back(Command{"SPcK", "Spark", "wake"});
        commands_.push_back(Command{"SPkK", "Spark", "wake"});
        commands_.push_back(Command{"gSPnRK", "Spark", "wake"});
        commands_.push_back(Command{"gSPaRK", "Spark", "wake"});
        commands_.push_back(Command{"fSPnRK", "Spark", "wake"});
        commands_.push_back(Command{"fSPaRK", "Spark", "wake"});
        commands_.push_back(Command{"cSPnRK", "Spark", "wake"});
        commands_.push_back(Command{"SPeRK", "Spark", "wake"});
        commands_.push_back(Command{"hSPnRK", "Spark", "wake"});
        commands_.push_back(Command{"hSPaRK", "Spark", "wake"});
        commands_.push_back(Command{"h SPnK", "Spark", "wake"});
        commands_.push_back(Command{"h SPaK", "Spark", "wake"});
        commands_.push_back(Command{"h SPcK", "Spark", "wake"});
        commands_.push_back(Command{"s PnRK", "Spark", "wake"});
        commands_.push_back(Command{"s PaRK", "Spark", "wake"});
        commands_.push_back(Command{"s PnK", "Spark", "wake"});
        commands_.push_back(Command{"s PaK", "Spark", "wake"});
        commands_.push_back(Command{"s P c R K", "Spark", "wake"});
        commands_.push_back(Command{"s P c K", "Spark", "wake"});
        commands_.push_back(Command{"eS PnRK", "Spark", "wake"});
        commands_.push_back(Command{"eS PaRK", "Spark", "wake"});
        commands_.push_back(Command{"eS PnK", "Spark", "wake"});
        commands_.push_back(Command{"eS PaK", "Spark", "wake"});
        commands_.push_back(Command{"iS PnRK", "Spark", "wake"});
        commands_.push_back(Command{"iS PaRK", "Spark", "wake"});
        commands_.push_back(Command{"iS PnK", "Spark", "wake"});
        commands_.push_back(Command{"iS PaK", "Spark", "wake"});
        commands_.push_back(Command{"S BnRK", "Spark", "wake"});
        commands_.push_back(Command{"S BaRK", "Spark", "wake"});
        commands_.push_back(Command{"S BnK", "Spark", "wake"});
        commands_.push_back(Command{"S BaK", "Spark", "wake"});
        commands_.push_back(Command{"o SPaRK", "Spark", "wake"});
        commands_.push_back(Command{"o SPnRK", "Spark", "wake"});

        // "Sparky" variants:
        commands_.push_back(Command{"SPnRKm", "Sparky", "wake"});
        commands_.push_back(Command{"SPaRKm", "Sparky", "wake"});

        // "OK Spark" calm variants:
        commands_.push_back(Command{"bKd SPnRK", "OK Spark", "wake"});
        commands_.push_back(Command{"bKdSPnRK", "OK Spark", "wake"});
        commands_.push_back(Command{"bKd SPaRK", "OK Spark", "wake"});
        commands_.push_back(Command{"bKdSPaRK", "OK Spark", "wake"});
        commands_.push_back(Command{"bKd SPcRK", "OK Spark", "wake"});
        commands_.push_back(Command{"bKdSPcRK", "OK Spark", "wake"});
        commands_.push_back(Command{"eKd SPnRK", "OK Spark", "wake"});
        commands_.push_back(Command{"eKd SPaRK", "OK Spark", "wake"});
        commands_.push_back(Command{"cKd SPnRK", "OK Spark", "wake"});
        commands_.push_back(Command{"cKd SPaRK", "OK Spark", "wake"});
        commands_.push_back(Command{"bKf SPnRK", "OK Spark", "wake"});
        commands_.push_back(Command{"bKf SPaRK", "OK Spark", "wake"});
        commands_.push_back(Command{"bKfSPnRK", "OK Spark", "wake"});
        commands_.push_back(Command{"Kd SPnRK", "OK Spark", "wake"});
        commands_.push_back(Command{"Kd SPaRK", "OK Spark", "wake"});

        // "Hey Spark" calm variants:
        commands_.push_back(Command{"hd SPnRK", "Hey Spark", "wake"});
        commands_.push_back(Command{"hdSPnRK", "Hey Spark", "wake"});
        commands_.push_back(Command{"hd SPaRK", "Hey Spark", "wake"});
        commands_.push_back(Command{"hdSPaRK", "Hey Spark", "wake"});
        commands_.push_back(Command{"hd SPcRK", "Hey Spark", "wake"});
        commands_.push_back(Command{"hd d SPnRK", "Hey Spark", "wake"});
        commands_.push_back(Command{"hd d SPaRK", "Hey Spark", "wake"});
        commands_.push_back(Command{"hf SPnRK", "Hey Spark", "wake"});
        commands_.push_back(Command{"hfSPnRK", "Hey Spark", "wake"});
        commands_.push_back(Command{"hf SPaRK", "Hey Spark", "wake"});
        commands_.push_back(Command{"hfSPaRK", "Hey Spark", "wake"});
        commands_.push_back(Command{"hd SPnRKm", "Hey Spark", "wake"});
        commands_.push_back(Command{"hd SPaRKm", "Hey Spark", "wake"});

        // "Hi Spark" calm variants:
        commands_.push_back(Command{"hi SPnRK", "Hi Spark", "wake"});
        commands_.push_back(Command{"hiSPnRK", "Hi Spark", "wake"});
        commands_.push_back(Command{"hi SPaRK", "Hi Spark", "wake"});
        commands_.push_back(Command{"hiSPaRK", "Hi Spark", "wake"});
        commands_.push_back(Command{"hi SPcRK", "Hi Spark", "wake"});
        commands_.push_back(Command{"hiSPcRK", "Hi Spark", "wake"});
        commands_.push_back(Command{"hi i SPnRK", "Hi Spark", "wake"});
        commands_.push_back(Command{"hi i SPaRK", "Hi Spark", "wake"});
        commands_.push_back(Command{"hi SPnRKm", "Hi Spark", "wake"});
        commands_.push_back(Command{"hi SPaRKm", "Hi Spark", "wake"});

        // "Hello Spark" calm variants:
        commands_.push_back(Command{"hcLb SPnRK", "Hello Spark", "wake"});
        commands_.push_back(Command{"hcLb SPaRK", "Hello Spark", "wake"});
        commands_.push_back(Command{"hfLb SPnRK", "Hello Spark", "wake"});
        commands_.push_back(Command{"hfLb SPaRK", "Hello Spark", "wake"});
        commands_.push_back(Command{"hcLbSPnRK", "Hello Spark", "wake"});
        commands_.push_back(Command{"hfLbSPnRK", "Hello Spark", "wake"});
        commands_.push_back(Command{"hLb SPnRK", "Hello Spark", "wake"});

        // "Yo Spark" variants:
        commands_.push_back(Command{"Yo SPnRK", "Yo Spark", "wake"});
        commands_.push_back(Command{"Yo SPaRK", "Yo Spark", "wake"});

        // "Suno Spark / Sun Spark" (Hinglish):
        commands_.push_back(Command{"SoNb SPnRK", "Suno Spark", "wake"});
        commands_.push_back(Command{"SoNb SPaRK", "Suno Spark", "wake"});
        commands_.push_back(Command{"ScN SPnRK", "Sun Spark", "wake"});
        commands_.push_back(Command{"ScN SPaRK", "Sun Spark", "wake"});

        // "Spark are you here" calm variants:
        commands_.push_back(Command{"SPnRK nR Yo hmR", "Spark are you here", "wake"});
        commands_.push_back(Command{"SPnRK nR Yo hgR", "Spark are you here", "wake"});
        commands_.push_back(Command{"SPaRK nR Yo hmR", "Spark are you here", "wake"});
        commands_.push_back(Command{"SPaRK nR Yo hgR", "Spark are you here", "wake"});
        commands_.push_back(Command{"SPnRK nR Yo hk", "Spark are you here", "wake"});
        commands_.push_back(Command{"SPaRK nR Yo hk", "Spark are you here", "wake"});

        ESP_LOGI(TAG, "Configured %d custom English wake word phrases with threshold %.3f", (int)commands_.size(), threshold_);
    }

    if (models_ == nullptr || models_->num == -1) {
        ESP_LOGE(TAG, "Failed to initialize wakenet model");
        return false;
    }

    // 初始化 multinet (命令词识别)
    mn_name_ = esp_srmodel_filter(models_, ESP_MN_PREFIX, language_.c_str());
    if (mn_name_ == nullptr) {
        ESP_LOGW(TAG, "Language '%s' multinet not found, falling back to any multinet model", language_.c_str());
        mn_name_ = esp_srmodel_filter(models_, ESP_MN_PREFIX, NULL);
    }
    if (mn_name_ == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize multinet, mn_name is nullptr");
        ESP_LOGI(TAG, "Please refer to https://pcn7cs20v8cr.feishu.cn/wiki/CpQjwQsCJiQSWSkYEvrcxcbVnwh to add custom wake word");
        return false;
    }

    multinet_ = esp_mn_handle_from_name(mn_name_);
    multinet_model_data_ = multinet_->create(mn_name_, duration_);
    multinet_->set_det_threshold(multinet_model_data_, threshold_);
    multinet_->open_log(multinet_model_data_);
    input_buffer_.reserve(multinet_->get_samp_chunksize(multinet_model_data_));
    esp_mn_commands_clear();
    for (int i = 0; i < commands_.size(); i++) {
        esp_err_t err = esp_mn_commands_add(i + 1, commands_[i].command.c_str());
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add command %d: %s (%s)", i + 1, commands_[i].text.c_str(), commands_[i].command.c_str());
        }
    }
    esp_mn_commands_update();
    
    multinet_->print_active_speech_commands(multinet_model_data_);
#if CONFIG_SEND_WAKE_WORD_DATA
    if (!wake_word_audio_cache_.Initialize(16000 * 2)) {
        ESP_LOGW(TAG, "Wake-word audio upload disabled: PSRAM cache allocation failed");
    }
#endif
    return true;
}

void CustomWakeWord::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = callback;
}

void CustomWakeWord::Start() {
    running_ = true;
    std::lock_guard<std::mutex> lock(input_buffer_mutex_);
    input_buffer_.clear();
}

void CustomWakeWord::Stop() {
    running_ = false;

    std::lock_guard<std::mutex> lock(input_buffer_mutex_);
    input_buffer_.clear();
}

void CustomWakeWord::Feed(const std::vector<int16_t>& data) {
    FeedSamples(data.data(), data.size(), false);
}

void CustomWakeWord::FeedMono(const int16_t* data, size_t samples) {
    FeedSamples(data, samples, true);
}

void CustomWakeWord::FeedSamples(const int16_t* data, size_t samples, bool mono) {
    if (multinet_model_data_ == nullptr || data == nullptr || samples == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(input_buffer_mutex_);
    // Check running state inside lock to avoid TOCTOU race with Stop()
    if (!running_) {
        return;
    }

    // If input channels is 2, we need to fetch the left channel data
    if (!mono && codec_->input_channels() > 1) {
        for (size_t i = 0; i < samples; i += codec_->input_channels()) {
            input_buffer_.push_back(data[i]);
        }
    } else {
        input_buffer_.insert(input_buffer_.end(), data, data + samples);
    }
    
    int chunksize = multinet_->get_samp_chunksize(multinet_model_data_);
    static int feed_heartbeat = 0;
    while (input_buffer_.size() >= chunksize) {
#if CONFIG_SEND_WAKE_WORD_DATA
        wake_word_audio_cache_.Store(input_buffer_.data(), chunksize);
#endif

        esp_mn_state_t mn_state = multinet_->detect(multinet_model_data_, input_buffer_.data());

        if (++feed_heartbeat >= 100) { // every ~3 seconds
            ESP_LOGI(TAG, "[MultiNet Heartbeat] running=%d, buf_sz=%d, chunk=%d, mn_state=%d",
                     running_.load() ? 1 : 0, (int)input_buffer_.size(), chunksize, (int)mn_state);
            feed_heartbeat = 0;
        }

        if (mn_state == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t *mn_result = multinet_->get_results(multinet_model_data_);
            ESP_LOGI(TAG, ">>> MULTINET CANDIDATE DETECTED! num=%d, raw='%s', str='%s'", 
                     mn_result ? mn_result->num : 0,
                     mn_result ? mn_result->raw_string : "",
                     mn_result ? mn_result->string : "");
            if (mn_result != nullptr) {
                bool is_speaker_playing = codec_ && codec_->IsSpeakerActive();
                for (int i = 0; i < mn_result->num && running_; i++) {
                    ESP_LOGI(TAG, "  Candidate[%d]: cmd_id=%d, prob=%f, str=%s (speaker_active=%d)", 
                            i, mn_result->command_id[i], mn_result->prob[i], mn_result->string, is_speaker_playing ? 1 : 0);
                    int idx = mn_result->command_id[i] - 1;
                    if (idx >= 0 && idx < (int)commands_.size()) {
                        auto& command = commands_[idx];
                        if (command.action == "wake") {
                            // Zero-threshold barge-in:
                            // MultiNet's internal det_threshold (0.010f) is the sole gatekeeper.
                            // Without hardware AEC, raising prob during speaker playback just
                            // makes barge-in impossible. The 97-phoneme coverage ensures
                            // speaker audio alone can't false-trigger "Spark".
                            float min_prob = 0.0f;
                            (void)is_speaker_playing; // acknowledged but not gated

                            if (mn_result->prob[i] < min_prob) {
                                ESP_LOGW(TAG, "Ignoring candidate '%s' (prob %f < min_prob %f)",
                                         command.text.c_str(), mn_result->prob[i], min_prob);
                                continue;
                            }

                            ESP_LOGI(TAG, "*** WAKE TRIGGERED by command '%s' (phrase '%s', prob %f) ***",
                                     command.text.c_str(), command.command.c_str(), mn_result->prob[i]);
                            last_detected_wake_word_ = command.text;
                            running_ = false;
                            input_buffer_.clear();
                            
                            if (wake_word_detected_callback_) {
                                wake_word_detected_callback_(last_detected_wake_word_);
                            }
                        }
                    }
                }
            }
            multinet_->clean(multinet_model_data_);
        } else if (mn_state == ESP_MN_STATE_TIMEOUT) {
            ESP_LOGI(TAG, "MultiNet timeout -> clean state");
            multinet_->clean(multinet_model_data_);
        }
        
        if (!running_) {
            break;
        }
        input_buffer_.erase(input_buffer_.begin(), input_buffer_.begin() + chunksize);
    }
}

size_t CustomWakeWord::GetFeedSize() {
    if (multinet_model_data_ == nullptr) {
        return 0;
    }
    return multinet_->get_samp_chunksize(multinet_model_data_);
}

void CustomWakeWord::EncodeWakeWordData() {
    const size_t stack_size = 4096 * 7;
    wake_word_opus_.clear();
    if (wake_word_encode_task_stack_ == nullptr) {
        wake_word_encode_task_stack_ = (StackType_t*)heap_caps_malloc(stack_size, MALLOC_CAP_SPIRAM);
        assert(wake_word_encode_task_stack_ != nullptr);
    }
    if (wake_word_encode_task_buffer_ == nullptr) {
        wake_word_encode_task_buffer_ = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
        assert(wake_word_encode_task_buffer_ != nullptr);
    }

    wake_word_encode_task_ = xTaskCreateStatic([](void* arg) {
        auto this_ = (CustomWakeWord*)arg;
        {
            auto start_time = esp_timer_get_time();
            // Create encoder
            esp_opus_enc_config_t opus_enc_cfg = AS_OPUS_ENC_CONFIG();
            void* encoder_handle = nullptr;
            auto ret = esp_opus_enc_open(&opus_enc_cfg, sizeof(esp_opus_enc_config_t), &encoder_handle);
            if (encoder_handle == nullptr) {
                ESP_LOGE(TAG, "Failed to create audio encoder, error code: %d", ret);
                this_->wake_word_audio_cache_.Clear();
                std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
                this_->wake_word_opus_.push_back(std::vector<uint8_t>());
                this_->wake_word_cv_.notify_all();
                vTaskDelete(nullptr);
                return;
            }
            // Get frame size
            int frame_size = 0;
            int outbuf_size = 0;
            esp_opus_enc_get_frame_size(encoder_handle, &frame_size, &outbuf_size);
            frame_size = frame_size / sizeof(int16_t);
            // Encode all PCM data
            int packets = 0;
            std::vector<int16_t> in_buffer(frame_size);
            esp_audio_enc_in_frame_t in = {};
            esp_audio_enc_out_frame_t out = {};
            const size_t cached_samples = this_->wake_word_audio_cache_.Size();
            for (size_t offset = 0;
                 offset + static_cast<size_t>(frame_size) <= cached_samples;
                 offset += frame_size) {
                if (this_->wake_word_audio_cache_.Read(
                        offset, in_buffer.data(), frame_size) != static_cast<size_t>(frame_size)) {
                    break;
                }
                std::vector<uint8_t> opus_buf(outbuf_size);
                in.buffer = reinterpret_cast<uint8_t*>(in_buffer.data());
                in.len = frame_size * sizeof(int16_t);
                out.buffer = opus_buf.data();
                out.len = outbuf_size;
                out.encoded_bytes = 0;
                ret = esp_opus_enc_process(encoder_handle, &in, &out);
                if (ret == ESP_AUDIO_ERR_OK) {
                    std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
                    this_->wake_word_opus_.emplace_back(opus_buf.data(), opus_buf.data() + out.encoded_bytes);
                    this_->wake_word_cv_.notify_all();
                    packets++;
                } else {
                    ESP_LOGE(TAG, "Failed to encode audio, error code: %d", ret);
                }
            }
            this_->wake_word_audio_cache_.Clear();
            // Close encoder
            esp_opus_enc_close(encoder_handle);
            auto end_time = esp_timer_get_time();
            ESP_LOGI(TAG, "Encode wake word opus %d packets in %ld ms", packets, (long)((end_time - start_time) / 1000));

            std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
            this_->wake_word_opus_.push_back(std::vector<uint8_t>());
            this_->wake_word_cv_.notify_all();
        }
        vTaskDelete(NULL);
    }, "encode_wake_word", stack_size, this, 2, wake_word_encode_task_stack_, wake_word_encode_task_buffer_);
}

bool CustomWakeWord::GetWakeWordOpus(std::vector<uint8_t>& opus) {
    std::unique_lock<std::mutex> lock(wake_word_mutex_);
    wake_word_cv_.wait(lock, [this]() {
        return !wake_word_opus_.empty();
    });
    opus.swap(wake_word_opus_.front());
    wake_word_opus_.pop_front();
    return !opus.empty();
}
