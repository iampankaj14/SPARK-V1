#include "spark_portal_travel.h"
#include "spark_face.h"
#include "spark_animation.h"
#include "spark_ui_objects.h"
#include <math.h>
#include <stdlib.h>

static lv_obj_t *portal_container = NULL;
static lv_obj_t *portal_obj = NULL;
static lv_obj_t *stars[20];
static lv_timer_t *portal_timer = NULL;
static int anim_state = 0;
static int32_t portal_x = 0;
static int32_t portal_y = 0;

static int32_t start_x = 0;
static int32_t start_y = 0;
static int look_count = 0;

// Waves are created/destroyed on demand — NOT pre-allocated
static lv_obj_t *waves[3];
static int waves_count = 0;
static bool waves_created = false;

// Track whether the spinning animation is currently active
static bool spin_active = false;

extern const lv_img_dsc_t* portal_frames[15];

static void set_portal_frame_cb(void *var, int32_t v) {
  if (v < 0) v = 0;
  if (v > 14) v = 14;
  lv_img_set_src((lv_obj_t *)var, portal_frames[v]);
}

static void set_opa_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, v, 0);
}

// --- Spin Animation Control ---
static void portal_spin_start(void) {
  if (spin_active) return;
  lv_anim_t spin_anim;
  lv_anim_init(&spin_anim);
  lv_anim_set_var(&spin_anim, portal_obj);
  lv_anim_set_exec_cb(&spin_anim, set_portal_frame_cb);
  lv_anim_set_values(&spin_anim, 0, 14);
  lv_anim_set_time(&spin_anim, 500);
  lv_anim_set_repeat_count(&spin_anim, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&spin_anim);
  spin_active = true;
}

static void portal_spin_stop(void) {
  if (!spin_active) return;
  lv_anim_del(portal_obj, set_portal_frame_cb);
  spin_active = false;
}

// --- Wave Management ---
static void set_wave_size_cb(void *var, int32_t v) {
  lv_obj_set_size((lv_obj_t *)var, v, v);
}

static void set_wave_opa_cb(void *var, int32_t v) {
  lv_obj_set_style_border_opa((lv_obj_t *)var, v, 0);
}

static void waves_create(void) {
  if (waves_created) return;
  for (int i = 0; i < 3; i++) {
    waves[i] = lv_obj_create(portal_container);
    lv_obj_remove_style_all(waves[i]);
    lv_obj_set_style_border_width(waves[i], 2, 0);
    lv_obj_set_style_border_color(waves[i], lv_color_hex(0x00FFFF), 0);
    lv_obj_set_style_radius(waves[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(waves[i], LV_OPA_TRANSP, 0);
    lv_obj_add_flag(waves[i], LV_OBJ_FLAG_HIDDEN);
  }
  waves_created = true;
}

static void waves_destroy(void) {
  if (!waves_created) return;
  for (int i = 0; i < 3; i++) {
    if (waves[i]) {
      lv_anim_del(waves[i], set_wave_size_cb);
      lv_anim_del(waves[i], set_wave_opa_cb);
      lv_obj_del(waves[i]);
      waves[i] = NULL;
    }
  }
  waves_created = false;
}

// ==========================================
// STATE MACHINE
// ==========================================
// RULE: Eyes are ONLY animated when portal is visible on screen.
//       When portal is hidden, this code does NOTHING to the eyes.
//       That means zero tearing during non-portal phases.
// ==========================================
static void portal_sm_cb(lv_timer_t *t) {
  lv_obj_t *eye_l = Spark_UI_GetObj(SPARK_UI_EYE_CONTAINER_L);
  lv_obj_t *eye_r = Spark_UI_GetObj(SPARK_UI_EYE_CONTAINER_R);

  switch (anim_state) {

  // -------------------------------------------------------
  // State 0: Look around BEFORE portal (NORMAL face style: fixed 100x165 = zero tearing)
  // -------------------------------------------------------
  case 0:
    if (look_count == 0) { start_x = -50; start_y = 0; }
    else if (look_count == 1) { start_x = 50; start_y = 0; }
    else if (look_count == 2) { start_x = 0; start_y = -30; }
    else {
      start_x = (rand() % 2 == 0) ? -50 : 50;
      start_y = (rand() % 2 == 0) ? -30 : 30;
      portal_x = -start_x;
      portal_y = -start_y;
    }

    // ONLY animate tx/ty. Do NOT animate width/height/angle (they're already 100/165/0).
    // AnimateEyeBase creates 5 animations — 3 are no-ops that still trigger lv_obj_invalidate every frame = CHOPPY.
    // Direct tx/ty = 2 animations per eye = 4 total = SMOOTH.
    Spark_Anim_Prop(eye_l, Spark_Anim_SetTxCb, lv_obj_get_style_translate_x(eye_l, 0), start_x, 400);
    Spark_Anim_Prop(eye_l, Spark_Anim_SetTyCb, lv_obj_get_style_translate_y(eye_l, 0), start_y, 400);
    Spark_Anim_Prop(eye_r, Spark_Anim_SetTxCb, lv_obj_get_style_translate_x(eye_r, 0), start_x, 400);
    Spark_Anim_Prop(eye_r, Spark_Anim_SetTyCb, lv_obj_get_style_translate_y(eye_r, 0), start_y, 400);

    look_count++;
    if (look_count <= 3) {
      lv_timer_set_period(portal_timer, 1200);
      anim_state = 0;
    } else {
      look_count = 0;
      lv_timer_set_period(portal_timer, 800);
      anim_state = 1;
    }
    break;

  // -------------------------------------------------------
  // State 1: Portal APPEARS — from here tearing is OK
  // -------------------------------------------------------
  case 1:
    lv_obj_clear_flag(portal_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(portal_container); // Eyes must render ON TOP of stars/portal
    lv_obj_align(portal_obj, LV_ALIGN_CENTER, portal_x, portal_y);
    lv_obj_clear_flag(portal_obj, LV_OBJ_FLAG_HIDDEN);
    portal_spin_start();
    Spark_Anim_Prop(portal_obj, set_opa_cb, 0, 255, 1000);

    lv_timer_set_period(portal_timer, 1200);
    anim_state = 2;
    break;

  // -------------------------------------------------------
  // State 2: Eyes shrink + move toward portal (PORTAL IS VISIBLE — tearing OK)
  // -------------------------------------------------------
  case 2:
    Spark_Anim_AnimateEyeBase(eye_l, 40, 66, 0, portal_x + 33, portal_y, 1000);
    Spark_Anim_AnimateEyeBase(eye_r, 40, 66, 0, portal_x - 33, portal_y, 1000);

    lv_timer_set_period(portal_timer, 1000);
    anim_state = 3;
    break;

  // -------------------------------------------------------
  // State 3: Absorbed into portal (PORTAL VISIBLE — tearing OK)
  // -------------------------------------------------------
  case 3:
    Spark_Anim_AnimateEyeBase(eye_l, 4, 4, 0, portal_x, portal_y, 800);
    Spark_Anim_AnimateEyeBase(eye_r, 4, 4, 0, portal_x, portal_y, 800);
    Spark_Anim_Fade(eye_l, false, 800);
    Spark_Anim_Fade(eye_r, false, 800);

    lv_timer_set_period(portal_timer, 1000);
    anim_state = 4;
    break;

  // -------------------------------------------------------
  // State 4: Portal fades out (spin continues for smooth visuals)
  // -------------------------------------------------------
  case 4:
    Spark_Anim_Prop(portal_obj, set_opa_cb, 255, 0, 1000);

    lv_timer_set_period(portal_timer, 1200);
    anim_state = 5;
    break;

  // -------------------------------------------------------
  // State 5: Waves in empty space
  // -------------------------------------------------------
  case 5:
    portal_spin_stop();
    lv_obj_add_flag(portal_obj, LV_OBJ_FLAG_HIDDEN);

    if (waves_count == 0) {
      waves_create();
    }

    if (waves_count < 6) {
      int w_idx = waves_count % 3;
      lv_obj_align(waves[w_idx], LV_ALIGN_CENTER, (rand() % 360) - 180, (rand() % 360) - 180);
      lv_obj_clear_flag(waves[w_idx], LV_OBJ_FLAG_HIDDEN);
      Spark_Anim_Prop(waves[w_idx], set_wave_size_cb, 0, 180, 1000);
      Spark_Anim_Prop(waves[w_idx], set_wave_opa_cb, 255, 0, 1000);

      waves_count++;
      lv_timer_set_period(portal_timer, 400);
      anim_state = 5;
    } else {
      waves_count = 0;
      waves_destroy();
      lv_timer_set_period(portal_timer, 1000);
      anim_state = 6;
    }
    break;

  // -------------------------------------------------------
  // State 6: New portal opens (PORTAL VISIBLE AGAIN — tearing OK)
  // -------------------------------------------------------
  case 6:
    lv_obj_clear_flag(portal_obj, LV_OBJ_FLAG_HIDDEN);

    portal_x = (rand() % 2 == 0) ? -70 : 70;
    portal_y = (rand() % 2 == 0) ? -40 : 40;
    lv_obj_align(portal_obj, LV_ALIGN_CENTER, portal_x, portal_y);

    portal_spin_start();
    Spark_Anim_Prop(portal_obj, set_opa_cb, 0, 255, 1000);

    lv_timer_set_period(portal_timer, 1200);
    anim_state = 7;
    break;

  // -------------------------------------------------------
  // State 7: Emerge from singularity (PORTAL VISIBLE — tearing OK)
  // -------------------------------------------------------
  case 7:
    Spark_Anim_AnimateEyeBase(eye_l, 4, 4, 0, portal_x, portal_y, 0);
    Spark_Anim_AnimateEyeBase(eye_r, 4, 4, 0, portal_x, portal_y, 0);

    Spark_Anim_AnimateEyeBase(eye_l, 40, 66, 0, portal_x + 33, portal_y, 800);
    Spark_Anim_AnimateEyeBase(eye_r, 40, 66, 0, portal_x - 33, portal_y, 800);
    Spark_Anim_Fade(eye_l, true, 800);
    Spark_Anim_Fade(eye_r, true, 800);

    lv_timer_set_period(portal_timer, 1000);
    anim_state = 8;
    break;

  // -------------------------------------------------------
  // State 8: Grow back to full size (PORTAL STILL VISIBLE — tearing OK)
  // -------------------------------------------------------
  case 8:
    Spark_Anim_AnimateEyeBase(eye_l, 100, 165, 0, -portal_x, -portal_y, 1000);
    Spark_Anim_AnimateEyeBase(eye_r, 100, 165, 0, -portal_x, -portal_y, 1000);

    lv_timer_set_period(portal_timer, 1400);
    anim_state = 9;
    break;

  // -------------------------------------------------------
  // State 9: Portal fades out
  // -------------------------------------------------------
  case 9:
    Spark_Anim_Prop(portal_obj, set_opa_cb, 255, 0, 1000);

    lv_timer_set_period(portal_timer, 1200);
    anim_state = 10;
    break;

  // -------------------------------------------------------
  // State 10: Portal gone. Look around (NORMAL face style = zero tearing) then loop.
  // -------------------------------------------------------
  case 10:
    portal_spin_stop();
    lv_obj_add_flag(portal_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(portal_container, LV_OBJ_FLAG_HIDDEN);

    if (look_count == 0) { start_x = 0; start_y = -30; }
    else if (look_count == 1) { start_x = 50; start_y = 0; }
    else if (look_count == 2) { start_x = -50; start_y = 0; }
    else { start_x = 0; start_y = 0; }

    // ONLY tx/ty — no width/height/angle invalidation = SMOOTH
    Spark_Anim_Prop(eye_l, Spark_Anim_SetTxCb, lv_obj_get_style_translate_x(eye_l, 0), start_x, 400);
    Spark_Anim_Prop(eye_l, Spark_Anim_SetTyCb, lv_obj_get_style_translate_y(eye_l, 0), start_y, 400);
    Spark_Anim_Prop(eye_r, Spark_Anim_SetTxCb, lv_obj_get_style_translate_x(eye_r, 0), start_x, 400);
    Spark_Anim_Prop(eye_r, Spark_Anim_SetTyCb, lv_obj_get_style_translate_y(eye_r, 0), start_y, 400);

    look_count++;
    if (look_count <= 3) {
      lv_timer_set_period(portal_timer, 1200);
      anim_state = 10;
    } else {
      look_count = 0;
      lv_timer_set_period(portal_timer, 2000);
      anim_state = 0; // LOOP
    }
    break;
  }
}

void Spark_PortalTravel_Init(lv_obj_t *parent) {
  if (portal_container)
    return;

  portal_container = lv_obj_create(parent);
  lv_obj_remove_style_all(portal_container);
  lv_obj_set_size(portal_container, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(portal_container, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(portal_container, LV_OPA_COVER, 0);
  lv_obj_add_flag(portal_container, LV_OBJ_FLAG_HIDDEN);

  // Create Static Stars
  for (int i = 0; i < 20; i++) {
    stars[i] = lv_obj_create(portal_container);
    lv_obj_remove_style_all(stars[i]);

    int size = rand() % 4 + 3;
    lv_obj_set_size(stars[i], size, size);

    lv_obj_set_style_bg_color(stars[i], lv_color_white(), 0);
    lv_obj_set_style_bg_opa(stars[i], rand() % 100 + 155, 0);
    lv_obj_set_style_radius(stars[i], LV_RADIUS_CIRCLE, 0);

    lv_obj_align(stars[i], LV_ALIGN_CENTER, (rand() % 450) - 225,
                 (rand() % 450) - 225);
  }

  // Create Image Portal
  portal_obj = lv_img_create(portal_container);
  lv_img_set_src(portal_obj, portal_frames[0]);
  lv_img_set_antialias(portal_obj, false);
  lv_obj_set_style_opa(portal_obj, 0, 0);
  lv_obj_add_flag(portal_obj, LV_OBJ_FLAG_HIDDEN);

  // Create State Machine Timer (suspended initially)
  portal_timer = lv_timer_create(portal_sm_cb, 100, NULL);
  lv_timer_pause(portal_timer);
}

void Spark_PortalTravel_Show(void) {
  if (portal_container) {
    // Don't show container yet — State 1 will show it when portal appears
    anim_state = 0;
    waves_count = 0;
    spin_active = false;
    if (portal_timer)
      lv_timer_resume(portal_timer);
  }
}

void Spark_PortalTravel_Hide(void) {
  if (portal_container) {
    lv_obj_add_flag(portal_container, LV_OBJ_FLAG_HIDDEN);
    portal_spin_stop();
    waves_destroy();
    if (portal_timer)
      lv_timer_pause(portal_timer);
  }
}
