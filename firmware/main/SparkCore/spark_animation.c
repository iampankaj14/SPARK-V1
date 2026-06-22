#include "spark_animation.h"
#include "esp_log.h"

#define TAG "SparkAnimation"

// LVGL callback adapters
void Spark_Anim_SetWidthCb(void * var, int32_t v) { lv_obj_set_width((lv_obj_t *)var, v); }
void Spark_Anim_SetHeightCb(void * var, int32_t v) { lv_obj_set_height((lv_obj_t *)var, v); }
void Spark_Anim_SetAngleCb(void * var, int32_t v) { lv_obj_set_style_transform_angle((lv_obj_t *)var, v, 0); }
void Spark_Anim_SetTxCb(void * var, int32_t v) { lv_obj_set_style_translate_x((lv_obj_t *)var, v, 0); }
void Spark_Anim_SetTyCb(void * var, int32_t v) { lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0); }
void Spark_Anim_SetOpaCb(void * var, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)var, v, 0); }

void Spark_Anim_Init(void) {
    ESP_LOGI(TAG, "Animation Registry initialized");
}

void Spark_Anim_Prop(lv_obj_t *obj, lv_anim_exec_xcb_t cb, int32_t start, int32_t end, uint32_t time) {
    if (!obj) return;
    lv_anim_del(obj, cb);
    if (time == 0) {
        cb(obj, end);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, start, end);
    lv_anim_set_time(&a, time);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

void Spark_Anim_Fade(lv_obj_t *obj, bool show, uint32_t time) {
    if (!obj) return;
    lv_anim_del(obj, Spark_Anim_SetOpaCb);
    int32_t start = lv_obj_get_style_opa(obj, 0);
    int32_t end = show ? 255 : 0;
    if (start != end) {
        Spark_Anim_Prop(obj, Spark_Anim_SetOpaCb, start, end, time);
    }
}

void Spark_Anim_FadeAura(lv_obj_t *obj, bool show, uint32_t time) {
    if (!obj) return;
    lv_anim_del(obj, Spark_Anim_SetOpaCb);
    int32_t start = lv_obj_get_style_opa(obj, 0);
    int32_t end = show ? LV_OPA_20 : 0;
    if (start != end) {
        Spark_Anim_Prop(obj, Spark_Anim_SetOpaCb, start, end, time);
    }
}

void Spark_Anim_AnimateEyeBase(lv_obj_t *eye, int32_t w, int32_t h, int32_t angle, int32_t tx, int32_t ty, uint32_t time) {
    if (!eye) return;
    Spark_Anim_Prop(eye, Spark_Anim_SetWidthCb, lv_obj_get_width(eye), w, time);
    Spark_Anim_Prop(eye, Spark_Anim_SetHeightCb, lv_obj_get_height(eye), h, time);
    Spark_Anim_Prop(eye, Spark_Anim_SetAngleCb, lv_obj_get_style_transform_angle(eye, 0), angle, time);
    Spark_Anim_Prop(eye, Spark_Anim_SetTxCb, lv_obj_get_style_translate_x(eye, 0), tx, time);
    Spark_Anim_Prop(eye, Spark_Anim_SetTyCb, lv_obj_get_style_translate_y(eye, 0), ty, time);
}

void Spark_Anim_Play(spark_anim_t anim, lv_obj_t *target, uint32_t duration_ms) {
    if (!target) return;
    
    switch (anim) {
        case SPARK_ANIM_BLINK: {
            int32_t h = lv_obj_get_height(target);
            Spark_Anim_Prop(target, Spark_Anim_SetHeightCb, h, h / 10, duration_ms / 2);
            Spark_Anim_Prop(target, Spark_Anim_SetHeightCb, h / 10, h, duration_ms / 2);
            break;
        }
        case SPARK_ANIM_WINK: {
            int32_t h = lv_obj_get_height(target);
            Spark_Anim_Prop(target, Spark_Anim_SetHeightCb, h, h / 10, duration_ms / 2);
            Spark_Anim_Prop(target, Spark_Anim_SetHeightCb, h / 10, h, duration_ms / 2);
            break;
        }
        case SPARK_ANIM_BOUNCE: {
            int32_t ty = lv_obj_get_style_translate_y(target, 0);
            Spark_Anim_Prop(target, Spark_Anim_SetTyCb, ty, ty - 15, duration_ms / 2);
            Spark_Anim_Prop(target, Spark_Anim_SetTyCb, ty - 15, ty, duration_ms / 2);
            break;
        }
        case SPARK_ANIM_SHAKE: {
            int32_t tx = lv_obj_get_style_translate_x(target, 0);
            Spark_Anim_Prop(target, Spark_Anim_SetTxCb, tx, tx - 10, duration_ms / 4);
            Spark_Anim_Prop(target, Spark_Anim_SetTxCb, tx - 10, tx + 10, duration_ms / 2);
            Spark_Anim_Prop(target, Spark_Anim_SetTxCb, tx + 10, tx, duration_ms / 4);
            break;
        }
        case SPARK_ANIM_FLOAT: {
            int32_t ty = lv_obj_get_style_translate_y(target, 0);
            Spark_Anim_Prop(target, Spark_Anim_SetTyCb, ty, ty + 8, duration_ms / 2);
            Spark_Anim_Prop(target, Spark_Anim_SetTyCb, ty + 8, ty, duration_ms / 2);
            break;
        }
        default:
            ESP_LOGW(TAG, "Animation type %d not procedural yet", anim);
            break;
    }
}

void Spark_Anim_Stop(lv_obj_t *target) {
    if (!target) return;
    lv_anim_del(target, Spark_Anim_SetWidthCb);
    lv_anim_del(target, Spark_Anim_SetHeightCb);
    lv_anim_del(target, Spark_Anim_SetAngleCb);
    lv_anim_del(target, Spark_Anim_SetTxCb);
    lv_anim_del(target, Spark_Anim_SetTyCb);
    lv_anim_del(target, Spark_Anim_SetOpaCb);
}
