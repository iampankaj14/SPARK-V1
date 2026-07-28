#include "spark_solar_system.h"
#include "esp_log.h"
#include <math.h>

#define PI 3.14159265f

typedef struct {
    const char *name;
    uint32_t color_hex;
    uint16_t radius;
    uint16_t orbit_radius;
    float speed;
    float current_angle;
    lv_obj_t *obj;
    lv_obj_t *ring_obj;
} planet_t;

static planet_t planets[] = {
    // Name     Color     Rad  Orbit Speed  Angle  Obj   Ring
    {"Mercury", 0xA9A9A9,  6,  28,  1.20f,   0.0f, NULL, NULL},
    {"Venus",   0xE3BB76, 10,  45,  0.80f,  45.0f, NULL, NULL},
    {"Earth",   0x4B95DF, 12,  65,  0.60f, 120.0f, NULL, NULL},
    {"Mars",    0xE27B58,  8,  83,  0.50f, 200.0f, NULL, NULL},
    {"Jupiter", 0xC88B3A, 22, 110,  0.25f,  60.0f, NULL, NULL},
    {"Saturn",  0xEAD6B8, 18, 140,  0.15f, 270.0f, NULL, NULL},
    {"Uranus",  0xC6D3E3, 14, 162,  0.10f, 330.0f, NULL, NULL},
    {"Neptune", 0x4B70DD, 12, 178,  0.08f, 180.0f, NULL, NULL}
};
#define NUM_PLANETS (sizeof(planets)/sizeof(planets[0]))

static lv_obj_t *solar_container = NULL;
static lv_obj_t *sun_obj = NULL;

static void set_wave_opa_cb(void * var, int32_t v) {
    lv_obj_set_style_border_opa((lv_obj_t *)var, v, 0);
}
static void set_wave_width_cb(void * var, int32_t v) {
    lv_obj_set_width((lv_obj_t *)var, v);
}
static void set_wave_height_cb(void * var, int32_t v) {
    lv_obj_set_height((lv_obj_t *)var, v);
}

static void planet_anim_cb(void * var, int32_t v) {
    planet_t *p = (planet_t *)var;
    if (!p->obj) return;
    
    float angle = ((float)v / 10.0f) + p->current_angle;
    if (angle >= 360.0f) angle -= 360.0f;
    
    float rad = angle * PI / 180.0f;
    lv_coord_t x = (lv_coord_t)(p->orbit_radius * cosf(rad));
    lv_coord_t y = (lv_coord_t)(p->orbit_radius * sinf(rad));

    // Absolute positioning instead of layout-heavy lv_obj_align
    lv_coord_t cx = 206; // 412/2
    lv_coord_t cy = 206; // 412/2
    lv_coord_t w = p->radius * 10;
    
    lv_obj_set_pos(p->obj, cx + x - (w / 2), cy + y - (w / 2));
}

void Spark_SolarSystem_Init(lv_obj_t *parent) {
    // 1. Create main container (pure black background, full size)
    solar_container = lv_obj_create(parent);
    lv_obj_remove_style_all(solar_container);
    lv_obj_set_size(solar_container, 412, 412); // Example display size
    lv_obj_align(solar_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(solar_container, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(solar_container, LV_OPA_COVER, 0);
    lv_obj_clear_flag(solar_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(solar_container, LV_OBJ_FLAG_OVERFLOW_VISIBLE); // Prevent cropping of outer planets
    lv_obj_add_flag(solar_container, LV_OBJ_FLAG_HIDDEN); // Hidden by default

    // Create Sun
    sun_obj = lv_obj_create(solar_container);
    lv_obj_remove_style_all(sun_obj);
    lv_obj_set_size(sun_obj, 36, 36);
    lv_obj_set_style_radius(sun_obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sun_obj, lv_color_hex(0xFFEEAA), 0); // Lighter sun color
    lv_obj_set_style_bg_opa(sun_obj, LV_OPA_COVER, 0);
    // Add glow to the Sun (Reduced shadow width massively speeds up ESP32 software rendering)
    // Remove heavy shadow blur to guarantee 60fps software rendering
    // lv_obj_set_style_shadow_color(sun_obj, lv_color_hex(0xFFCC00), 0);
    // lv_obj_set_style_shadow_width(sun_obj, 20, 0);
    // lv_obj_set_style_shadow_spread(sun_obj, 5, 0);
    lv_obj_align(sun_obj, LV_ALIGN_CENTER, 0, 0);

    // Create Orbits and Planets
    for (int i = 0; i < NUM_PLANETS; i++) {
        // Orbit arc (thin circle)
        lv_obj_t *orbit_arc = lv_arc_create(solar_container);
        lv_obj_remove_style_all(orbit_arc);
        // Add +2 to size to perfectly center the 2px arc line on the exact orbit_radius
        lv_obj_set_size(orbit_arc, (planets[i].orbit_radius * 2) + 2, (planets[i].orbit_radius * 2) + 2);
        lv_obj_align(orbit_arc, LV_ALIGN_CENTER, 0, 0);
        lv_arc_set_bg_angles(orbit_arc, 0, 360);
        lv_obj_remove_style(orbit_arc, NULL, LV_PART_INDICATOR);
        lv_obj_remove_style(orbit_arc, NULL, LV_PART_KNOB);
        
        // Make the line white and slightly thick (visible from far)
        lv_obj_set_style_arc_width(orbit_arc, 2, LV_PART_MAIN);
        lv_obj_set_style_arc_color(orbit_arc, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_arc_opa(orbit_arc, LV_OPA_30, LV_PART_MAIN);

        // Planet Container
        lv_obj_t *container = lv_obj_create(solar_container);
        lv_obj_remove_style_all(container);
        lv_obj_set_size(container, planets[i].radius * 10, planets[i].radius * 10);
        planets[i].obj = container;

        // Actual Planet Sphere
        lv_obj_t *sphere = lv_obj_create(container);
        lv_obj_remove_style_all(sphere);
        lv_obj_set_size(sphere, planets[i].radius * 2, planets[i].radius * 2);
        lv_obj_set_style_radius(sphere, LV_RADIUS_CIRCLE, 0);
        
        lv_color_t base_color = lv_color_hex(planets[i].color_hex);
        lv_obj_set_style_bg_color(sphere, base_color, 0);
        // Remove heavy CPU gradients for solid 60fps rendering performance
        // lv_obj_set_style_bg_grad_color(sphere, lv_color_darken(base_color, LV_OPA_60), 0);
        // lv_obj_set_style_bg_grad_dir(sphere, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(sphere, LV_OPA_COVER, 0);
        lv_obj_align(sphere, LV_ALIGN_CENTER, 0, 0);

        // Textures (added to sphere)
        if (strcmp(planets[i].name, "Jupiter") == 0 || strcmp(planets[i].name, "Saturn") == 0) {
            // Horizontal bands
            for (int b = -1; b <= 1; b+=2) {
                lv_obj_t *band = lv_obj_create(sphere);
                lv_obj_remove_style_all(band);
                int band_width = (planets[i].radius * 2) - 4;
                lv_obj_set_size(band, band_width, planets[i].radius / 3);
                lv_obj_set_style_bg_color(band, lv_color_darken(base_color, LV_OPA_30), 0);
                lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);
                lv_obj_align(band, LV_ALIGN_CENTER, 0, b * (planets[i].radius / 3));
            }
        } else if (strcmp(planets[i].name, "Earth") == 0) {
            lv_obj_set_style_clip_corner(sphere, true, 0);
            lv_obj_t *cont1 = lv_obj_create(sphere);
            lv_obj_remove_style_all(cont1);
            lv_obj_set_size(cont1, 6, 4);
            lv_obj_set_style_radius(cont1, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(cont1, lv_color_hex(0x2E8B57), 0);
            lv_obj_set_style_bg_opa(cont1, LV_OPA_COVER, 0);
            lv_obj_align(cont1, LV_ALIGN_CENTER, -2, -1);
            
            lv_obj_t *cont2 = lv_obj_create(sphere);
            lv_obj_remove_style_all(cont2);
            lv_obj_set_size(cont2, 4, 3);
            lv_obj_set_style_radius(cont2, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(cont2, lv_color_hex(0x228B22), 0);
            lv_obj_set_style_bg_opa(cont2, LV_OPA_COVER, 0);
            lv_obj_align(cont2, LV_ALIGN_CENTER, 2, 2);
            
        } else if (strcmp(planets[i].name, "Mars") == 0) {
            lv_obj_set_style_clip_corner(sphere, true, 0);
            lv_obj_t *crater = lv_obj_create(sphere);
            lv_obj_remove_style_all(crater);
            lv_obj_set_size(crater, 3, 2);
            lv_obj_set_style_radius(crater, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(crater, lv_color_darken(base_color, LV_OPA_50), 0);
            lv_obj_set_style_bg_opa(crater, LV_OPA_COVER, 0);
            lv_obj_align(crater, LV_ALIGN_CENTER, 1, -1);
        }

        // Saturn Rings (Asteroid Belt of Stones - Optimized)
        if (strcmp(planets[i].name, "Saturn") == 0) {
            int num_stones = 12; // Reduced from 18 for faster rendering
            float a = planets[i].radius * 2.2f; // Reduced horizontal radius to make it shorter
            float b = planets[i].radius * 0.8f;
            float tilt = 15.0f * PI / 180.0f;
            
            for (int j = 0; j < num_stones; j++) {
                float angle = (float)j / num_stones * 2.0f * PI;
                
                float x = a * cosf(angle);
                float y = b * sinf(angle);
                
                float rot_x = x * cosf(tilt) - y * sinf(tilt);
                float rot_y = x * sinf(tilt) + y * cosf(tilt);
                
                // Add stones to container, not the sphere, so they orbit it
                lv_obj_t *stone = lv_obj_create(container);
                lv_obj_remove_style_all(stone);
                
                int s_size = (j % 2 == 0) ? 5 : 3; // Increased size to make the ring thicker
                lv_obj_set_size(stone, s_size, s_size);
                lv_obj_set_style_radius(stone, LV_RADIUS_CIRCLE, 0);
                
                uint32_t color = (j % 2 == 0) ? 0xEAD6B8 : 0xC3B091;
                lv_obj_set_style_bg_color(stone, lv_color_hex(color), 0);
                lv_obj_set_style_bg_opa(stone, LV_OPA_COVER, 0);
                
                // Center in the container (which is 10x radius size)
                lv_coord_t px = (planets[i].radius * 5) + (lv_coord_t)rot_x - (s_size / 2);
                lv_coord_t py = (planets[i].radius * 5) + (lv_coord_t)rot_y - (s_size / 2);
                lv_obj_set_pos(stone, px, py);
            }
        }

        // Earth Pulse Wave
        if (strcmp(planets[i].name, "Earth") == 0) {
            lv_obj_t *wave = lv_obj_create(container);
            lv_obj_remove_style_all(wave);
            lv_obj_set_size(wave, planets[i].radius * 2, planets[i].radius * 2);
            lv_obj_set_style_radius(wave, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(wave, 1, 0);
            lv_obj_set_style_border_color(wave, lv_color_hex(planets[i].color_hex), 0);
            lv_obj_set_style_border_opa(wave, LV_OPA_COVER, 0);
            lv_obj_align(wave, LV_ALIGN_CENTER, 0, 0);

            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, wave);
            lv_anim_set_values(&a, planets[i].radius * 2, planets[i].radius * 8); // Scaled for the larger planet sizes
            lv_anim_set_time(&a, 3000);
            lv_anim_set_playback_time(&a, 0);
            lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
            
            lv_anim_set_exec_cb(&a, set_wave_width_cb);
            lv_anim_start(&a);
            lv_anim_set_exec_cb(&a, set_wave_height_cb);
            lv_anim_start(&a);

            lv_anim_t a_opa;
            lv_anim_init(&a_opa);
            lv_anim_set_var(&a_opa, wave);
            lv_anim_set_values(&a_opa, LV_OPA_80, LV_OPA_TRANSP);
            lv_anim_set_time(&a_opa, 3000);
            lv_anim_set_playback_time(&a_opa, 0);
            lv_anim_set_repeat_count(&a_opa, LV_ANIM_REPEAT_INFINITE);
            lv_anim_set_exec_cb(&a_opa, set_wave_opa_cb);
            lv_anim_start(&a_opa);
        }
        
        // Start Planet Orbit Animation (Time-based interpolation for perfect smoothness)
        lv_anim_t a_orbit;
        lv_anim_init(&a_orbit);
        lv_anim_set_var(&a_orbit, &planets[i]);
        lv_anim_set_values(&a_orbit, 0, 3600);
        
        // Convert old "degrees per 16ms" speed to total time in ms for 360 degrees
        uint32_t time_ms = (uint32_t)((360.0f / planets[i].speed) * 16.66f);
        lv_anim_set_time(&a_orbit, time_ms);
        lv_anim_set_repeat_count(&a_orbit, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_exec_cb(&a_orbit, planet_anim_cb);
        lv_anim_start(&a_orbit);
    }
}

void Spark_SolarSystem_Show(void) {
    if (solar_container) {
        lv_obj_clear_flag(solar_container, LV_OBJ_FLAG_HIDDEN);
    }
}

void Spark_SolarSystem_Hide(void) {
    if (solar_container) {
        lv_obj_add_flag(solar_container, LV_OBJ_FLAG_HIDDEN);
    }
}
