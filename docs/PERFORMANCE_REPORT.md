# Spark V1 Performance & Footprint Review

## 1. Memory Management (RAM & PSRAM)

### Pre-Refactor Issues
- **Uncontrolled Allocations:** State machines, LVGL polling, and sensor polling were heavily coupled with unbounded global variables.
- **PSRAM Spikes:** Audio processing and web endpoints shared the primary heap dynamically, leading to fragmentation and sudden Out-Of-Memory (OOM) resets on intense voice queries.
- **Static Strings:** Emotion mapping strings, face definition names, and intent tags were consuming substantial instruction RAM instead of being properly flash-allocated.

### Post-Refactor Improvements
- **Const Allocation:** The `SPARK_FACES` array defining the visual coordinates and timing properties for 18 expressions is now stored strictly in Flash (`.rodata`) saving ~2KB of internal SRAM.
- **Scoped Callbacks:** The `Spark_Anim_Prop` architecture replaces redundant custom LVGL animation execution callbacks with centralized, strongly-typed static wrappers (e.g., `Spark_Anim_SetWidthCb`), drastically reducing code footprint and preventing stack bloat.
- **Controlled Hardware Context:** Hardware states (IMU tilt, tap, accelerometer bounds, battery voltage) are polled periodically in a dedicated Hardware Manager, buffering values and preventing interrupt-driven heap exhaustion.

## 2. Rendering Pipeline (LVGL Object Management)

### Pre-Refactor Issues
- **Masking Overhead:** `obj_set_style_translate_y` was aggressively casted via `(lv_anim_exec_xcb_t)lv_obj_set_style_translate_y`, creating incompatible function signatures.
- **Orphaned Objects:** Mask components and auxiliary expressions (like tears, blushing, ooh-mouth) were repeatedly toggled via opa (opacity), resulting in lingering layout calculations even when hidden.
- **State Churning:** Setting a face or animation required manual parsing of visual widgets across multiple isolated scopes.

### Post-Refactor Improvements
- **Data-Driven States:** `Spark_Face_Set` acts as a single-entry pipeline. When transitioning between states (e.g., from `SLEEP` to `WTF`), it intelligently fades all accessory groups simultaneously via `hide_all_accessories` and `hide_all_masks`.
- **Callback Safety:** The type-safe wrapper functions (`set_width_cb`, `set_height_cb`, etc.) ensure the animation engine doesn't invoke corrupted pointers or invalid memory addresses during LVGL screen redraws.
- **FPS Stability:** Base eye containers are only hidden when switching to fully disconnected visual elements (like `EYES_CLOSED` or `IGNORE`). For most standard transitions (Normal -> Happy, Happy -> Angry), the core objects remain mounted, keeping rendering FPS stable and avoiding expensive reallocation.

## 3. Real-Time Responsiveness

### Pre-Refactor Issues
- `MIC_Speech.c` was forcefully overriding UI widgets globally, interrupting `deskimon.c` loops, leading to display tearing and stuttering voice animations.

### Post-Refactor Improvements
- **Intent Parsing Decoupled:** `MIC_Speech.c` now simply calls `Spark_Emotion_ProcessIntent()`. The `Emotion Manager` dictates the translation from intent string (e.g., `COMPANION_SAD`) to emotion state (`crying`), and issues non-blocking commands to the `Face Manager` and `Animation Manager`.
- **Lock Contention Removed:** By centralizing LVGL animation queues, there is no longer a race condition between the Mic daemon trying to render an active speaking icon while the Cloud daemon simultaneously attempts to update the brand color.

## Conclusion
The new **SparkCore** layer guarantees an O(1) complexity switch for face transitions and significantly stabilizes the core ESP-IDF task manager, preventing stack overflows during simultaneous cloud connectivity, voice processing, and 60FPS UI updates.
