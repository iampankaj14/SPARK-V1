# Spark V1 Architecture Specification

This document defines the new modular, manager-based architecture designed for Spark V1. This foundation decouples system state, visual assets, animations, and drivers, allowing Spark to scale to 100+ expressions, OTA theme packs, and neural personality engines in the future.

---

## 1. Core Layer (Spark Core)

Spark Core is structured into six independent manager modules, each communicating via clean APIs and event callbacks.

```
                  +-----------------------------------+
                  |            Spark Core             |
                  +-----------------------------------+
                                    |
     +-----------------+------------+------------+-----------------+
     |                 |            |            |                 |
+----+----+       +----+----+  +----+----+  +----+----+       +----+----+
|  State  |       |Hardware |  |  Face   |  |Animation|       | Emotion |
| Manager |       | Manager |  | Manager |  | Manager |       | Manager |
+---------+       +---------+  +---------+  +---------+       +---------+
```

### 1.1 State Manager (`spark_state`)
- **Responsibilities:**
  - Maintains the centralized system state.
  - Controls valid transitions and prevents illegal states.
  - Dispatches state changes to registered modules (e.g. muting the mic when SPEAKING).
- **Public API:**
  ```c
  typedef enum {
      SPARK_STATE_BOOT,
      SPARK_STATE_IDLE,
      SPARK_STATE_LISTENING,
      SPARK_STATE_THINKING,
      SPARK_STATE_SPEAKING,
      SPARK_STATE_SLEEPING,
      SPARK_STATE_CHARGING,
      SPARK_STATE_UPDATING,
      SPARK_STATE_ERROR
  } spark_state_t;

  typedef void (*spark_state_cb_t)(spark_state_t old_state, spark_state_t new_state);

  void Spark_State_Init(void);
  spark_state_t Spark_State_Get(void);
  bool Spark_State_TransitionTo(spark_state_t next_state);
  void Spark_State_RegisterCallback(spark_state_cb_t callback);
  const char* Spark_State_ToString(spark_state_t state);
  ```

### 1.2 Hardware Manager (`spark_hardware`)
- **Responsibilities:**
  - Interfaces with I2C, EXIO, Battery ADC, RTC, and Accelerometer/IMU.
  - Spawns a dedicated polling task for the IMU, preventing bus congestion.
  - Translates raw accelerometer data into discrete events (Shake, Tilt Up, Tilt Down, Tap, Long Press).
- **Public API:**
  ```c
  typedef enum {
      SPARK_HW_EVENT_SHAKE,
      SPARK_HW_EVENT_TILT_UP,
      SPARK_HW_EVENT_TILT_DOWN,
      SPARK_HW_EVENT_TAP,
      SPARK_HW_EVENT_DOUBLE_TAP,
      SPARK_HW_EVENT_TRIPLE_TAP,
      SPARK_HW_EVENT_LONG_PRESS_5S
  } spark_hw_event_t;

  typedef void (*spark_hw_callback_t)(spark_hw_event_t event);

  void Spark_Hardware_Init(void);
  float Spark_Hardware_GetBatteryVolts(void);
  int Spark_Hardware_GetBatteryPercent(void);
  void Spark_Hardware_SetBacklight(uint8_t percentage);
  void Spark_Hardware_RegisterCallback(spark_hw_callback_t callback);
  ```

### 1.3 Face Manager (`spark_face`)
- **Responsibilities:**
  - Manages eye, pupil, mouth, and accessory elements.
  - Defines faces using data-driven structures rather than hardcoded C logic.
  - Controls eye color, aura opacity, and styling dynamically.
- **Public API:**
  ```c
  typedef enum {
      SPARK_FACE_NORMAL,
      SPARK_FACE_BORED,
      SPARK_FACE_HAPPY,
      SPARK_FACE_ANGRY,
      SPARK_FACE_SLEEPY,
      SPARK_FACE_BLUSH,
      SPARK_FACE_BORING,
      SPARK_FACE_CHILL,
      SPARK_FACE_CRYING,
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

  void Spark_Face_Init(void);
  void Spark_Face_Set(spark_face_t face);
  spark_face_t Spark_Face_Get(void);
  void Spark_Face_SetColor(uint32_t color_hex);
  const char* Spark_Face_GetName(spark_face_t face);
  ```

### 1.4 Animation Manager (`spark_animation`)
- **Responsibilities:**
  - Provides a centralized registry of reusable visual behavior modules.
  - Executes procedural movements (Blink, Wink, Float, Bounce, Orbit, Supernova, Shake, Idle Drift) on any LVGL object.
  - Eliminates custom inline animation overrides.
- **Public API:**
  ```c
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
  ```

### 1.5 Emotion Manager (`spark_emotion`)
- **Responsibilities:**
  - Implements the high-level `Intent -> Emotion -> Face -> Animation` pipeline.
  - Translates text emotion tags ("happy", "wtf", "sleepy") into target face configurations.
  - Serves as the integration point for future neural personality engines.
- **Public API:**
  ```c
  void Spark_Emotion_Init(void);
  void Spark_Emotion_Set(const char *emotion_tag);
  void Spark_Emotion_ProcessIntent(const char *intent_name);
  ```

### 1.6 Intent Manager (`spark_intent`)
- **Responsibilities:**
  - Interfaces with the AFE (Audio Front End) and continuous MultiNet wake word spotter.
  - Captures microphone input, computes local voice activity detection (VAD), and manages the recording buffer.
  - Controls voice query posts to the Next.js server, parses incoming response headers, and routes them to the Emotion Manager.
- **Public API:**
  ```c
  void Spark_Intent_Init(void);
  void Spark_Intent_StartRecording(void);
  void Spark_Intent_StopRecording(void);
  bool Spark_Intent_IsRecording(void);
  ```

---

## 2. Centralized State Machine

All application states are tracked in a unified FSM. Transition logic is encapsulated inside `spark_state.c` to prevent race conditions.

```
       +--------------------------------------------+
       |                    BOOT                    |
       +--------------------------------------------+
                             | (Init Complete)
                             v
       +--------------------------------------------+ <--------+
  +--->|                    IDLE                    |          |
  |    +--------------------------------------------+          |
  |      | (Wake Word / Tap)  | (15s Inactive)                 |
  |      v                    v                                | (Action / Wake)
  |    +--------------------------------------------+          |
  |    |                 LISTENING                  |          |
  |    +--------------------------------------------+          |
  |      | (Silence VAD)                                       |
  |      v                                                     |
  |    +--------------------------------------------+          |
  |    |                  THINKING                  |          |
  |    +--------------------------------------------+          |
  |      | (Audio Response Ready)   | (Timeout/Err)            |
  |      v                          v                          |
  |    +------------------------+ +-----------------+          |
  |    |        SPEAKING        | |      ERROR      |          |
  |    +------------------------+ +-----------------+          |
  |      | (Playback Done)                                     |
  |      v                                                     |
  |    +--------------------------------------------+          |
  |    |                  SLEEPING                  |----------+
  |    +--------------------------------------------+
  |                                 | (USB Charge In)
  |                                 v
  |    +--------------------------------------------+
  +----|                  CHARGING                  |
       +--------------------------------------------+
```

---

## 3. Data-Driven Face & Animation Systems

### 3.1 Face Definition Structure
To eliminate hardcoded pixel manipulation and styling offsets, faces are specified via structured configurations.

```c
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
    spark_anim_t default_animation;
} spark_face_config_t;
```

With this architecture:
1. Adding a new face requires only appending a static struct entry to the `SPARK_FACES` array.
2. In the future, this layout array can be loaded from a JSON file (e.g. `/faces/happy.json`) on the SD card, making face expansions zero-firmware-change assets.

---

## 4. Pipeline Execution Model

```
[Voice Input] -> [Intent Matcher] -> [Emotion Tag] -> [Face ID] -> [Animation Class]
```

1. **Intent Matching:** The Next.js server processes a voice command and returns an intent header (e.g., `X-Intent: motivate`).
2. **Emotion Translation:** `Spark_Emotion_ProcessIntent("motivate")` evaluates that motivation corresponds to a "happy" emotion.
3. **Face Dispatch:** `Spark_Face_Set(SPARK_FACE_HAPPY)` receives the emotion, configures the LVGL container layouts automatically based on the face definition, and fades out inactive overlays.
4. **Animation Trigger:** `Spark_Anim_Play(SPARK_ANIM_BOUNCE, eye_container_l, 400)` runs a physical bounce pattern, bringing the expression to life.
