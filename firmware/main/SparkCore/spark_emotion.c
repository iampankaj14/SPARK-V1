#include "spark_emotion.h"
#include "spark_face.h"
#include "esp_log.h"
#include <string.h>

#define TAG "SparkEmotion"

void Spark_Emotion_Init(void) {
    ESP_LOGI(TAG, "Emotion Manager initialized");
}

void Spark_Emotion_Set(const char *emotion_tag) {
    if (!emotion_tag) return;
    
    spark_face_t face = SPARK_FACE_NORMAL;
    if (strcmp(emotion_tag, "happy") == 0 || strcmp(emotion_tag, "speaking") == 0) {
        face = SPARK_FACE_HAPPY;
    } else if (strcmp(emotion_tag, "angry") == 0) {
        face = SPARK_FACE_ANGRY;
    } else if (strcmp(emotion_tag, "sleepy") == 0 || strcmp(emotion_tag, "sleep") == 0 || strcmp(emotion_tag, "tired") == 0) {
        face = SPARK_FACE_SLEEP;
    } else if (strcmp(emotion_tag, "crying") == 0 || strcmp(emotion_tag, "cry") == 0 || strcmp(emotion_tag, "sad") == 0) {
        face = SPARK_FACE_CRY;
    } else if (strcmp(emotion_tag, "crying_mouth") == 0 || strcmp(emotion_tag, "sorrow") == 0) {
        face = SPARK_FACE_CRYING_MOUTH;
    } else if (strcmp(emotion_tag, "happy_cry") == 0 || strcmp(emotion_tag, "moved") == 0) {
        face = SPARK_FACE_HAPPY_CRY;
    } else if (strcmp(emotion_tag, "interest") == 0 || strcmp(emotion_tag, "listening") == 0) {
        face = SPARK_FACE_INTEREST;
    } else if (strcmp(emotion_tag, "ooh") == 0 || strcmp(emotion_tag, "thinking") == 0 || strcmp(emotion_tag, "wonder") == 0) {
        face = SPARK_FACE_OOH;
    } else if (strcmp(emotion_tag, "wtf") == 0 || strcmp(emotion_tag, "surprised") == 0 || strcmp(emotion_tag, "shocked") == 0) {
        face = SPARK_FACE_WTF;
    } else if (strcmp(emotion_tag, "laugh") == 0 || strcmp(emotion_tag, "funny") == 0 || strcmp(emotion_tag, "joke") == 0) {
        face = SPARK_FACE_LAUGH;
    } else if (strcmp(emotion_tag, "bored") == 0) {
        face = SPARK_FACE_BORED;
    } else if (strcmp(emotion_tag, "boring") == 0 || strcmp(emotion_tag, "yawn") == 0) {
        face = SPARK_FACE_BORING;
    } else if (strcmp(emotion_tag, "blush") == 0 || strcmp(emotion_tag, "love") == 0 || strcmp(emotion_tag, "shy") == 0) {
        face = SPARK_FACE_BLUSH;
    } else if (strcmp(emotion_tag, "chill") == 0 || strcmp(emotion_tag, "relaxed") == 0) {
        face = SPARK_FACE_CHILL;
    } else if (strcmp(emotion_tag, "insecure") == 0) {
        face = SPARK_FACE_INSECURE;
    } else if (strcmp(emotion_tag, "normal") == 0 || strcmp(emotion_tag, "neutral") == 0 || strcmp(emotion_tag, "standby") == 0 || strcmp(emotion_tag, "idle") == 0) {
        face = SPARK_FACE_NORMAL;
    }
    
    ESP_LOGI(TAG, "Emotion resolved: %s -> Face: %s", emotion_tag, Spark_Face_GetName(face));
    Spark_Face_Set(face);
}

void Spark_Emotion_ProcessIntent(const char *intent_name) {
    if (!intent_name) return;
    
    // Direct intent to emotion mapping helper
    const char *emotion = "normal";
    if (strstr(intent_name, "GREETING")) {
        emotion = "happy";
    } else if (strstr(intent_name, "COMPANION_TELL_JOKE") || strstr(intent_name, "FUN_GUESS_WHAT")) {
        emotion = "laugh";
    } else if (strstr(intent_name, "RELATIONSHIP_YOU_ARE_ANNOYING")) {
        emotion = "angry";
    } else if (strstr(intent_name, "COMPANION_SAD") || strstr(intent_name, "RELATIONSHIP_SORRY")) {
        emotion = "crying";
    } else if (strstr(intent_name, "BORED") || strstr(intent_name, "TIRED")) {
        emotion = "bored";
    } else if (strstr(intent_name, "LOVE") || strstr(intent_name, "I_LIKE_YOU")) {
        emotion = "blush"; // blush first, then auto-transitions to love face after 2s
    }
    
    ESP_LOGI(TAG, "Intent matched: %s -> Emotion: %s", intent_name, emotion);
    Spark_Emotion_Set(emotion);
}
