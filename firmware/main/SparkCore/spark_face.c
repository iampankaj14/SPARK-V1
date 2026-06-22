#include "spark_face.h"
#include "spark_animation.h"
#include "spark_ui_objects.h"
#include "esp_log.h"
#include <string.h>

#define TAG "SparkFace"

static spark_face_t s_current_face = SPARK_FACE_BOOT;
static uint32_t s_eye_color_hex = 0x1AC8DB;

// Static configuration database for all faces
static const spark_face_config_t SPARK_FACES[SPARK_FACE_MAX] = {
    [SPARK_FACE_NORMAL] = {
        .name = "NORMAL",
        .left_eye  = { .width = 100, .height = 165, .translate_x = 0, .translate_y = 0, .mask_top_y = -400, .mask_moon_y = -400, .is_visible = true },
        .right_eye = { .width = 100, .height = 165, .translate_x = 0, .translate_y = 0, .mask_top_y = -400, .mask_moon_y = -400, .is_visible = true },
        .mouth     = { .width = 0, .height = 0, .translate_x = 0, .translate_y = 0, .is_visible = false },
        .tears_visible = false,
        .default_transition_ms = 400
    },
    [SPARK_FACE_BORED] = {
        .name = "BORED",
        .left_eye  = { .width = 130, .height = 180, .translate_x = -15, .translate_y = -40, .mask_top_y = -40, .mask_moon_y = -400, .is_visible = true },
        .right_eye = { .width = 130, .height = 180, .translate_x = 15, .translate_y = -40, .mask_top_y = -40, .mask_moon_y = -400, .is_visible = true },
        .mouth     = { .width = 0, .height = 0, .translate_x = 0, .translate_y = 0, .is_visible = false },
        .tears_visible = false,
        .default_transition_ms = 500
    },
    [SPARK_FACE_HAPPY] = {
        .name = "HAPPY",
        .left_eye  = { .width = 100, .height = 165, .translate_x = 0, .translate_y = 0, .mask_top_y = -400, .mask_moon_y = 40, .is_visible = true },
        .right_eye = { .width = 100, .height = 165, .translate_x = 0, .translate_y = 0, .mask_top_y = -400, .mask_moon_y = 40, .is_visible = true },
        .mouth     = { .width = 0, .height = 0, .translate_x = 0, .translate_y = 0, .is_visible = false },
        .tears_visible = false,
        .default_transition_ms = 400
    },
    [SPARK_FACE_ANGRY] = {
        .name = "ANGRY",
        .left_eye  = { .width = 130, .height = 180, .translate_x = -15, .translate_y = -40, .mask_top_y = -40, .mask_moon_y = -400, .is_visible = true },
        .right_eye = { .width = 130, .height = 180, .translate_x = 15, .translate_y = -40, .mask_top_y = -40, .mask_moon_y = -400, .is_visible = true },
        .mouth     = { .width = 0, .height = 0, .translate_x = 0, .translate_y = 0, .is_visible = false },
        .tears_visible = false,
        .default_transition_ms = 300
    },
    [SPARK_FACE_SLEEP] = {
        .name = "SLEEPY",
        .left_eye  = { .width = 90, .height = 25, .translate_x = 0, .translate_y = 40, .mask_top_y = -400, .mask_moon_y = -400, .is_visible = true },
        .right_eye = { .width = 90, .height = 25, .translate_x = 0, .translate_y = 40, .mask_top_y = -400, .mask_moon_y = -400, .is_visible = true },
        .mouth     = { .width = 0, .height = 0, .translate_x = 0, .translate_y = 0, .is_visible = false },
        .tears_visible = false,
        .default_transition_ms = 800
    },
    [SPARK_FACE_BLUSH] = {
        .name = "BLUSH",
        .left_eye  = { .width = 100, .height = 165, .translate_x = 0, .translate_y = 0, .mask_top_y = -400, .mask_moon_y = 40, .is_visible = true },
        .right_eye = { .width = 100, .height = 165, .translate_x = 0, .translate_y = 0, .mask_top_y = -400, .mask_moon_y = 40, .is_visible = true },
        .mouth     = { .width = 40, .height = 40, .translate_x = 0, .translate_y = 60, .is_visible = true, .shape_type = 0 },
        .tears_visible = false,
        .default_transition_ms = 300
    },
    [SPARK_FACE_BORING] = {
        .name = "BORING",
        .left_eye  = { .width = 100, .height = 165, .translate_x = 0, .translate_y = -50, .mask_top_y = -50, .mask_moon_y = -400, .is_visible = true },
        .right_eye = { .width = 100, .height = 165, .translate_x = 0, .translate_y = -50, .mask_top_y = -50, .mask_moon_y = -400, .is_visible = true },
        .mouth     = { .width = 50, .height = 70, .translate_x = 0, .translate_y = 50, .is_visible = true, .shape_type = 1 },
        .tears_visible = false,
        .default_transition_ms = 500
    },
    [SPARK_FACE_CHILL] = {
        .name = "CHILL",
        .left_eye  = { .width = 100, .height = 165, .translate_x = 0, .translate_y = -50, .mask_top_y = -50, .mask_moon_y = -400, .is_visible = true },
        .right_eye = { .width = 100, .height = 165, .translate_x = 0, .translate_y = -50, .mask_top_y = -50, .mask_moon_y = -400, .is_visible = true },
        .mouth     = { .width = 40, .height = 40, .translate_x = 0, .translate_y = 60, .is_visible = true, .shape_type = 0 },
        .tears_visible = false,
        .default_transition_ms = 400
    },
    [SPARK_FACE_CRY] = {
        .name = "CRYING",
        .left_eye  = { .width = 100, .height = 15, .translate_x = 0, .translate_y = -20, .mask_top_y = -400, .mask_moon_y = -400, .is_visible = true },
        .right_eye = { .width = 100, .height = 15, .translate_x = 0, .translate_y = -20, .mask_top_y = -400, .mask_moon_y = -400, .is_visible = true },
        .mouth     = { .width = 0, .height = 0, .translate_x = 0, .translate_y = 0, .is_visible = false },
        .tears_visible = true,
        .default_transition_ms = 300
    },
    [SPARK_FACE_CRYING_MOUTH] = {
        .name = "CRYING MOUTH",
        .left_eye  = { .width = 100, .height = 15, .translate_x = 0, .translate_y = -20, .mask_top_y = -400, .mask_moon_y = -400, .is_visible = true },
        .right_eye = { .width = 100, .height = 15, .translate_x = 0, .translate_y = -20, .mask_top_y = -400, .mask_moon_y = -400, .is_visible = true },
        .mouth     = { .width = 50, .height = 70, .translate_x = 0, .translate_y = 50, .is_visible = true, .shape_type = 1 },
        .tears_visible = true,
        .default_transition_ms = 300
    },
    [SPARK_FACE_EYES_CLOSED] = {
        .name = "EYES CLOSED",
        .left_eye  = { .width = 20, .height = 100, .translate_x = 0, .translate_y = 0, .mask_top_y = -400, .mask_moon_y = -400, .is_visible = false },
        .right_eye = { .width = 20, .height = 100, .translate_x = 0, .translate_y = 0, .mask_top_y = -400, .mask_moon_y = -400, .is_visible = false },
        .mouth     = { .width = 0, .height = 0, .translate_x = 0, .translate_y = 0, .is_visible = false },
        .tears_visible = false,
        .default_transition_ms = 300
    },
    [SPARK_FACE_HAPPY_CRY] = {
        .name = "HAPPY CRY",
        .left_eye  = { .width = 100, .height = 15, .translate_x = 0, .translate_y = -30, .mask_top_y = -400, .mask_moon_y = -400, .is_visible = true },
        .right_eye = { .width = 100, .height = 15, .translate_x = 0, .translate_y = -30, .mask_top_y = -400, .mask_moon_y = -400, .is_visible = true },
        .mouth     = { .width = 50, .height = 30, .translate_x = 0, .translate_y = 80, .is_visible = true, .shape_type = 2 },
        .tears_visible = true,
        .default_transition_ms = 300
    },
    [SPARK_FACE_IGNORE] = {
        .name = "IGNORE",
        .left_eye  = { .width = 130, .height = 20, .translate_x = -15, .translate_y = 20, .mask_top_y = -400, .mask_moon_y = -400, .is_visible = false },
        .right_eye = { .width = 130, .height = 20, .translate_x = 15, .translate_y = 20, .mask_top_y = -400, .mask_moon_y = -400, .is_visible = false },
        .mouth     = { .width = 0, .height = 0, .translate_x = 0, .translate_y = 0, .is_visible = false },
        .tears_visible = false,
        .default_transition_ms = 300
    },
    [SPARK_FACE_INSECURE] = {
        .name = "INSECURE",
        .left_eye  = { .width = 110, .height = 110, .translate_x = -65, .translate_y = -20, .mask_top_y = -400, .mask_moon_y = -400, .is_visible = false },
        .right_eye = { .width = 110, .height = 110, .translate_x = 65, .translate_y = -20, .mask_top_y = -400, .mask_moon_y = -400, .is_visible = false },
        .mouth     = { .width = 40, .height = 40, .translate_x = 0, .translate_y = 60, .is_visible = true, .shape_type = 1 },
        .tears_visible = false,
        .default_transition_ms = 300
    },
    [SPARK_FACE_INTEREST] = {
        .name = "INTEREST",
        .left_eye  = { .width = 110, .height = 110, .translate_x = -65, .translate_y = -20, .mask_top_y = -400, .mask_moon_y = -400, .is_visible = false },
        .right_eye = { .width = 110, .height = 110, .translate_x = 65, .translate_y = -20, .mask_top_y = -400, .mask_moon_y = -400, .is_visible = false },
        .mouth     = { .width = 50, .height = 50, .translate_x = 0, .translate_y = 60, .is_visible = true, .shape_type = 0 },
        .tears_visible = false,
        .default_transition_ms = 300
    },
    [SPARK_FACE_OOH] = {
        .name = "OOH",
        .left_eye  = { .width = 105, .height = 130, .translate_x = 0, .translate_y = -10, .mask_top_y = -400, .mask_moon_y = -400, .is_visible = true },
        .right_eye = { .width = 105, .height = 130, .translate_x = 0, .translate_y = -10, .mask_top_y = -400, .mask_moon_y = -400, .is_visible = true },
        .mouth     = { .width = 32, .height = 32, .translate_x = 0, .translate_y = 80, .is_visible = true, .shape_type = 1 },
        .tears_visible = false,
        .default_transition_ms = 500
    },
    [SPARK_FACE_WTF] = {
        .name = "WTF",
        .left_eye  = { .width = 100, .height = 16, .translate_x = 0, .translate_y = -45, .mask_top_y = -400, .mask_moon_y = -400, .is_visible = true },
        .right_eye = { .width = 100, .height = 16, .translate_x = 0, .translate_y = -45, .mask_top_y = -400, .mask_moon_y = -400, .is_visible = true },
        .mouth     = { .width = 40, .height = 30, .translate_x = 0, .translate_y = 75, .is_visible = true, .shape_type = 2 },
        .tears_visible = false,
        .default_transition_ms = 500
    },
    [SPARK_FACE_LAUGH] = {
        .name = "LAUGH",
        .left_eye  = { .width = 100, .height = 165, .translate_x = 0, .translate_y = -120, .mask_top_y = -30, .mask_moon_y = -400, .is_visible = true },
        .right_eye = { .width = 100, .height = 165, .translate_x = 0, .translate_y = -120, .mask_top_y = -30, .mask_moon_y = -400, .is_visible = true },
        .mouth     = { .width = 140, .height = 70, .translate_x = 0, .translate_y = 50, .is_visible = true, .shape_type = 3 },
        .tears_visible = false,
        .default_transition_ms = 400
    }
};

void Spark_Face_Init(void) {
    s_current_face = SPARK_FACE_BOOT;
    ESP_LOGI(TAG, "Face Manager initialized successfully");
}

spark_face_t Spark_Face_Get(void) {
    return s_current_face;
}

const char* Spark_Face_GetName(spark_face_t face) {
    if (face < SPARK_FACE_MAX) {
        return SPARK_FACES[face].name;
    }
    return "UNKNOWN";
}

const spark_face_config_t* Spark_Face_GetConfig(spark_face_t face) {
    if (face < SPARK_FACE_MAX) {
        return &SPARK_FACES[face];
    }
    return NULL;
}

static void hide_all_masks(uint32_t time) {
    Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_MASK_TOP_L), Spark_Anim_SetTyCb, lv_obj_get_style_translate_y(Spark_UI_GetObj(SPARK_UI_MASK_TOP_L), 0), -400, time);
    Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_MASK_TOP_R), Spark_Anim_SetTyCb, lv_obj_get_style_translate_y(Spark_UI_GetObj(SPARK_UI_MASK_TOP_R), 0), -400, time);
    Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_MASK_MOON_L), Spark_Anim_SetTyCb, lv_obj_get_style_translate_y(Spark_UI_GetObj(SPARK_UI_MASK_MOON_L), 0), -400, time);
    Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_MASK_MOON_R), Spark_Anim_SetTyCb, lv_obj_get_style_translate_y(Spark_UI_GetObj(SPARK_UI_MASK_MOON_R), 0), -400, time);
}

static void hide_all_accessories(uint32_t time) {
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_MOUTH_ARC_L), false, time);
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_MOUTH_ARC_R), false, time);
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_INTEREST_MOUTH_L), false, time);
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_INTEREST_MOUTH_R), false, time);
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_TEAR_L), false, time);
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_TEAR_R), false, time);
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_EYE_CLOSED_L), false, time);
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_EYE_CLOSED_R), false, time);
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_MOUTH_TRIANGLE), false, time);
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_INSECURE_MOUTH), false, time);
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_INSECURE_EYE_CONTAINER_L), false, time);
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_INSECURE_EYE_CONTAINER_R), false, time);
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_MOUTH_OOH), false, time);
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_MOUTH_WTF), false, time);
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_MOUTH_WTF_CIRCLE), false, time);
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_LAUGH_MOUTH), false, time);
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_LAUGH_HEMI_L), false, time);
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_LAUGH_HEMI_R), false, time);
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_IGNORE_LINE_L), false, time);
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_IGNORE_LINE_R), false, time);
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_IGNORE_HEMI_L), false, time);
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_IGNORE_HEMI_R), false, time);
    Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_MOUTH_YAWN), false, time);
    
    Spark_Anim_Stop(Spark_UI_GetObj(SPARK_UI_INSEC_COVER_L));
    lv_obj_set_style_opa(Spark_UI_GetObj(SPARK_UI_INSEC_COVER_L), 0, 0);
    Spark_Anim_Stop(Spark_UI_GetObj(SPARK_UI_INSEC_COVER_R));
    lv_obj_set_style_opa(Spark_UI_GetObj(SPARK_UI_INSEC_COVER_R), 0, 0);
}

void Spark_Face_Set(spark_face_t face) {
    if (face == s_current_face) return;

    // Handle ignore eye containers fading
    if (s_current_face == SPARK_FACE_IGNORE) {
        Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_EYE_CONTAINER_L), true, 300);
        Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_EYE_CONTAINER_R), true, 300);
    }
    
    s_current_face = face;
    const spark_face_config_t *cfg = &SPARK_FACES[face];

    // Hide base eyes ONLY if we are switching to dedicated visual layouts
    if (face == SPARK_FACE_INSECURE || face == SPARK_FACE_INTEREST || 
        face == SPARK_FACE_IGNORE || face == SPARK_FACE_EYES_CLOSED) {
        Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_EYE_CONTAINER_L), false, 300);
        Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_EYE_CONTAINER_R), false, 300);
    } else {
        Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_EYE_CONTAINER_L), true, 300);
        Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_EYE_CONTAINER_R), true, 300);
    }
    
    hide_all_masks(300);
    hide_all_accessories(300);

    // Apply the static eye layout transitions
    if (cfg->left_eye.is_visible) {
        Spark_Anim_AnimateEyeBase(
            Spark_UI_GetObj(SPARK_UI_EYE_CONTAINER_L),
            cfg->left_eye.width, cfg->left_eye.height, 0,
            cfg->left_eye.translate_x, cfg->left_eye.translate_y,
            cfg->default_transition_ms
        );
    }
    if (cfg->right_eye.is_visible) {
        Spark_Anim_AnimateEyeBase(
            Spark_UI_GetObj(SPARK_UI_EYE_CONTAINER_R),
            cfg->right_eye.width, cfg->right_eye.height, 0,
            cfg->right_eye.translate_x, cfg->right_eye.translate_y,
            cfg->default_transition_ms
        );
    }

    // Apply masks
    if (cfg->left_eye.mask_top_y != -400) {
        Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_MASK_TOP_L), Spark_Anim_SetTyCb, lv_obj_get_style_translate_y(Spark_UI_GetObj(SPARK_UI_MASK_TOP_L), 0), cfg->left_eye.mask_top_y, cfg->default_transition_ms);
    }
    if (cfg->right_eye.mask_top_y != -400) {
        Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_MASK_TOP_R), Spark_Anim_SetTyCb, lv_obj_get_style_translate_y(Spark_UI_GetObj(SPARK_UI_MASK_TOP_R), 0), cfg->right_eye.mask_top_y, cfg->default_transition_ms);
    }
    if (cfg->left_eye.mask_moon_y != -400) {
        Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_MASK_MOON_L), Spark_Anim_SetTyCb, lv_obj_get_style_translate_y(Spark_UI_GetObj(SPARK_UI_MASK_MOON_L), 0), cfg->left_eye.mask_moon_y, cfg->default_transition_ms);
    }
    if (cfg->right_eye.mask_moon_y != -400) {
        Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_MASK_MOON_R), Spark_Anim_SetTyCb, lv_obj_get_style_translate_y(Spark_UI_GetObj(SPARK_UI_MASK_MOON_R), 0), cfg->right_eye.mask_moon_y, cfg->default_transition_ms);
    }

    // Apply specific visual state overrides for visual accessories
    switch (face) {
        case SPARK_FACE_BLUSH:
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_MOUTH_ARC_L), true, 300);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_MOUTH_ARC_R), true, 300);
            break;
            
        case SPARK_FACE_BORING:
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_MOUTH_YAWN), true, 500);
            break;
            
        case SPARK_FACE_CHILL:
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_MOUTH_ARC_L), true, 400);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_MOUTH_ARC_R), true, 400);
            break;
            
        case SPARK_FACE_CRY:
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_TEAR_L), true, 300);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_TEAR_R), true, 300);
            break;
            
        case SPARK_FACE_CRYING_MOUTH:
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_TEAR_L), true, 300);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_TEAR_R), true, 300);
            Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_TEAR_L), Spark_Anim_SetHeightCb, 0, 80, 500);
            Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_TEAR_L), Spark_Anim_SetTyCb, 0, 40, 500);
            Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_TEAR_R), Spark_Anim_SetHeightCb, 0, 80, 500);
            Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_TEAR_R), Spark_Anim_SetTyCb, 0, 40, 500);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_MOUTH_YAWN), true, 300);
            break;
            
        case SPARK_FACE_EYES_CLOSED:
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_EYE_CLOSED_L), true, 300);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_EYE_CLOSED_R), true, 300);
            break;
            
        case SPARK_FACE_HAPPY_CRY:
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_TEAR_L), true, 300);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_TEAR_R), true, 300);
            Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_TEAR_L), Spark_Anim_SetHeightCb, 0, 80, 500);
            Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_TEAR_L), Spark_Anim_SetTyCb, 0, 40, 500);
            Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_TEAR_R), Spark_Anim_SetHeightCb, 0, 80, 500);
            Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_TEAR_R), Spark_Anim_SetTyCb, 0, 40, 500);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_MOUTH_TRIANGLE), true, 300);
            break;
            
        case SPARK_FACE_IGNORE:
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_IGNORE_LINE_L), true, 300);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_IGNORE_LINE_R), true, 300);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_IGNORE_HEMI_L), true, 300);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_IGNORE_HEMI_R), true, 300);
            break;
            
        case SPARK_FACE_INSECURE:
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_INSECURE_EYE_CONTAINER_L), true, 300);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_INSECURE_EYE_CONTAINER_R), true, 300);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_INSECURE_MOUTH), true, 300);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_INSEC_COVER_L), true, 300);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_INSEC_COVER_R), true, 300);
            break;
            
        case SPARK_FACE_INTEREST:
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_INSECURE_EYE_CONTAINER_L), true, 300);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_INSECURE_EYE_CONTAINER_R), true, 300);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_INTEREST_MOUTH_L), true, 300);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_INTEREST_MOUTH_R), true, 300);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_INSEC_COVER_L), true, 300);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_INSEC_COVER_R), true, 300);
            break;
            
        case SPARK_FACE_OOH:
            lv_obj_set_size(Spark_UI_GetObj(SPARK_UI_EYE_CONTAINER_L), 70, 90);
            lv_obj_set_size(Spark_UI_GetObj(SPARK_UI_EYE_CONTAINER_R), 70, 90);
            Spark_Anim_AnimateEyeBase(Spark_UI_GetObj(SPARK_UI_EYE_CONTAINER_L), 105, 130, 0, 0, -10, 500);
            Spark_Anim_AnimateEyeBase(Spark_UI_GetObj(SPARK_UI_EYE_CONTAINER_R), 105, 130, 0, 0, -10, 500);
            
            lv_obj_set_size(Spark_UI_GetObj(SPARK_UI_MOUTH_OOH), 10, 5);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_MOUTH_OOH), true, 300);
            Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_MOUTH_OOH), Spark_Anim_SetWidthCb, 10, 32, 500);
            Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_MOUTH_OOH), Spark_Anim_SetHeightCb, 5, 32, 500);
            break;
            
        case SPARK_FACE_WTF:
            lv_obj_set_size(Spark_UI_GetObj(SPARK_UI_EYE_CONTAINER_L), 20, 16);
            lv_obj_set_size(Spark_UI_GetObj(SPARK_UI_EYE_CONTAINER_R), 20, 16);
            Spark_Anim_AnimateEyeBase(Spark_UI_GetObj(SPARK_UI_EYE_CONTAINER_L), 100, 16, 0, 0, -45, 500);
            Spark_Anim_AnimateEyeBase(Spark_UI_GetObj(SPARK_UI_EYE_CONTAINER_R), 100, 16, 0, 0, -45, 500);
            
            lv_obj_set_size(Spark_UI_GetObj(SPARK_UI_MOUTH_WTF_CIRCLE), 35, 35);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_MOUTH_WTF_CIRCLE), true, 0);
            Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_MOUTH_WTF_CIRCLE), Spark_Anim_SetWidthCb, 35, 0, 500);
            Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_MOUTH_WTF_CIRCLE), Spark_Anim_SetHeightCb, 35, 0, 500);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_MOUTH_WTF_CIRCLE), false, 500);
            
            lv_obj_set_size(Spark_UI_GetObj(SPARK_UI_MOUTH_WTF), 0, 0);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_MOUTH_WTF), true, 500);
            Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_MOUTH_WTF), Spark_Anim_SetWidthCb, 0, 40, 500);
            Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_MOUTH_WTF), Spark_Anim_SetHeightCb, 0, 30, 500);
            break;
            
        case SPARK_FACE_LAUGH:
            lv_obj_set_size(Spark_UI_GetObj(SPARK_UI_LAUGH_MOUTH), 140, 5);
            Spark_Anim_Fade(Spark_UI_GetObj(SPARK_UI_LAUGH_MOUTH), true, 300);
            Spark_Anim_Prop(Spark_UI_GetObj(SPARK_UI_LAUGH_MOUTH), Spark_Anim_SetHeightCb, 5, 70, 400);
            break;
            
        default:
            break;
    }
}

void Spark_Face_SetColor(uint32_t color_hex) {
    s_eye_color_hex = color_hex;
    // Runtime update triggers pending refresh (via logic_timer_cb in deskimon.c)
}
