#pragma once
#include "lvgl.h"

typedef enum {
    SPARK_ANIM_BLINK,
    SPARK_ANIM_WINK,
    SPARK_ANIM_FLOAT,
    SPARK_ANIM_BOUNCE,
    SPARK_ANIM_ORBIT,
    SPARK_ANIM_SUPERNOVA,
    SPARK_ANIM_SHAKE,
    SPARK_ANIM_IDLE_DRIFT
} spark_anim_t;

void Spark_Anim_Init(void);
void Spark_Anim_Play(spark_anim_t anim, lv_obj_t *target, uint32_t duration_ms);
void Spark_Anim_Stop(lv_obj_t *target);

// Procedural and transition animation helpers
void Spark_Anim_Prop(lv_obj_t *obj, lv_anim_exec_xcb_t cb, int32_t start, int32_t end, uint32_t time);
void Spark_Anim_Fade(lv_obj_t *obj, bool show, uint32_t time);
void Spark_Anim_FadeAura(lv_obj_t *obj, bool show, uint32_t time);
void Spark_Anim_AnimateEyeBase(lv_obj_t *eye, int32_t w, int32_t h, int32_t angle, int32_t tx, int32_t ty, uint32_t time);

// Callback wrappers matching lv_anim_exec_xcb_t
void Spark_Anim_SetWidthCb(void * var, int32_t v);
void Spark_Anim_SetHeightCb(void * var, int32_t v);
void Spark_Anim_SetAngleCb(void * var, int32_t v);
void Spark_Anim_SetTxCb(void * var, int32_t v);
void Spark_Anim_SetTyCb(void * var, int32_t v);
void Spark_Anim_SetOpaCb(void * var, int32_t v);
