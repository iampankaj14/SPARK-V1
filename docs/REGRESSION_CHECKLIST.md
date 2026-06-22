# Spark V1 Hardware Testing & Regression Protocol

Since the Spark V1 firmware executes heavily on physical sensors and LVGL rendering pipelines, you must physically validate the following systems to ensure the development mode removal and architectural refactor introduced zero side-effects.

Please load the newly compiled firmware and execute this protocol:

## 1. Face System Validation
Wait for the device to complete the 1-second boot sequence (Normal eyes). 
- [ ] Observe the device for 7+ seconds without interacting. Confirm it transitions from `NORMAL` -> `BORING` -> `BORED` -> `SLEEP`.
- [ ] Ensure that during transitions, there is no "clipping" (edges of the eyes suddenly snapping instead of smoothly interpolating).
- [ ] Verify that the eye colors exactly match your `branding.json` configuration.

## 2. Gesture System Validation
With the device awake and in the `NORMAL` state:
- [ ] **Swipe Left/Right:** Verify the face briefly blushes (`BLUSH`).
- [ ] **Swipe Up:** Verify the face transitions to shocked (`WTF`).
- [ ] **Swipe Down:** Verify the face transitions to wonder (`OOH`).
- [ ] **Single Tap:** Tap the screen once. Verify it transitions to `HAPPY` (or `CHILL` if it was asleep).
- [ ] **Double Tap:** Tap the screen twice rapidly. Verify it transitions to `INTEREST`.
- [ ] **Triple Tap:** Tap the screen three times rapidly. Verify it transitions to `ANGRY`.

## 3. IMU (Accelerometer) System Validation
Pick up the physical device:
- [ ] **Tilt Up/Forward:** Tilt the device so the screen faces slightly downwards. Verify it transitions to `CRYING` or `CRYING_MOUTH`.
- [ ] **Shake (X-Axis):** Shake the device aggressively side-to-side. Verify it transitions to `IGNORE`.
- [ ] **Shake (Y/Z-Axis):** Shake the device up and down. Verify it transitions to `ANGRY`.

## 4. Voice / Intent System Validation
- [ ] Speak the wake word to trigger the audio recording arc.
- [ ] Say something happy (e.g., "Tell me a joke"). Verify it fetches the intent, speaks the audio, and the face transitions to `LAUGH` or `HAPPY`.
- [ ] Ensure that the speaking lip-sync animation (the expanding/contracting mouth) does not stutter or drop frames while the audio is playing.

## 5. Stress Testing & Architecture Claims
- [ ] **Rapid Input:** Tap the screen and swipe repeatedly as fast as possible for 30 seconds. The `Animation Manager` should gracefully replace animations without memory leaks or crash loops.
- [ ] **Long Runtime:** Leave the device plugged in overnight. If there are no memory leaks in the `SparkCore` managers, the device should still be running smoothly in the morning.

> [!IMPORTANT]
> **Reporting Back:** Please let me know if any of these tests fail, stutter, or exhibit unexpected behaviors. If they pass, we can mark Phases 2, 3, and 4 as verified!
