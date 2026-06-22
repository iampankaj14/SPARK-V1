#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SPARK_FACE_BOOT = 0,
    SPARK_FACE_NORMAL,
    SPARK_FACE_BORED,
    SPARK_FACE_HAPPY,
    SPARK_FACE_ANGRY,
    SPARK_FACE_SLEEP,
    SPARK_FACE_BLUSH,
    SPARK_FACE_BORING,
    SPARK_FACE_CHILL,
    SPARK_FACE_CRY,
    SPARK_FACE_CRYING_MOUTH,
    SPARK_FACE_EYES_CLOSED,
    SPARK_FACE_HAPPY_CRY,
    SPARK_FACE_IGNORE,
    SPARK_FACE_INSECURE,
    SPARK_FACE_INTEREST,
    SPARK_FACE_OOH,
    SPARK_FACE_WTF,
    SPARK_FACE_LAUGH,
    SPARK_FACE_WINK,
    SPARK_FACE_SKEPTICAL,
    SPARK_FACE_DIZZY,
    SPARK_FACE_LOVE,
    SPARK_FACE_MAX
} spark_face_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    int16_t translate_x;
    int16_t translate_y;
    int16_t mask_top_y;
    int16_t mask_moon_y;
    bool is_visible;
} spark_eye_layout_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    int16_t translate_x;
    int16_t translate_y;
    bool is_visible;
    uint8_t shape_type; // 0: Arc, 1: Circle, 2: Triangle, 3: Capsule, 4: Flat Line
} spark_mouth_layout_t;

typedef struct {
    const char *name;
    spark_eye_layout_t left_eye;
    spark_eye_layout_t right_eye;
    spark_mouth_layout_t mouth;
    bool tears_visible;
    uint32_t default_transition_ms;
} spark_face_config_t;

void Spark_Face_Init(void);
void Spark_Face_Set(spark_face_t face);
spark_face_t Spark_Face_Get(void);
void Spark_Face_SetColor(uint32_t color_hex);
const char* Spark_Face_GetName(spark_face_t face);
const spark_face_config_t* Spark_Face_GetConfig(spark_face_t face);
