# WhisperClock for PebbleOS

**WhisperClock** is a highly customizable, natural-sounding speaking clock app for Pebble smartwatches that have speakers (currently: Pebble Time 2). 

## 🎯 Purpose
Traditional smartwatch speaking clocks rely on robotic, highly synthesized Text-to-Speech (TTS) engines. WhisperClock takes a radically different approach by using a custom **"Stitched Audio"** engine. 

By combining individually recorded human voice clips using intelligent grammatical logic, the app dynamically constructs the current time and announces it with natural human inflection and prosody. Whether you are visually impaired, driving, or just don't want to look at your wrist, WhisperClock delivers the time smoothly, clearly, and instantly.

## ✨ Features

* **Motion Activated:** Hear the time simply by flicking your wrist using Pebble's native accelerometer.
* **Custom Gesture Training:** Don't like the standard wrist flick? Record and train your own custom arm movement to trigger the clock using dynamic time-warping.
* **Intelligent Audio Stitching:** Dynamically handles 12-hour, 24-hour (military), and analog/conversational time formats seamlessly.
* **Pebble Quiet Time Integration:** Automatically mutes itself when your watch is in Do Not Disturb mode—preventing accidental interruptions during meetings or sleep.
* **Quick Kill Switch:** Tap any button or the screen to instantly cancel audio playback.
* **Deep Audio Customization:**
  * **Voice Interval:** Adjust the millisecond delay (50ms - 500ms) between stitched words for faster or slower reading speeds.
  * **Audio Trim:** Automatically trim dead space from the end of audio clips for punchier, faster playback.
  * **Volume Control:** Independent speaker volume levels (10% - 100%).
  * **Prefix/Suffix Toggles:** Choose whether the watch says "It's..." and "AM/PM".
* **Giant UI Display:** Shows a massive, high-contrast digital time readout on screen while speaking.

## 🛠️ Build Requirements

* Pebble SDK (v4.3 or newer)
* Exported 8-bit, 16kHz Mono WAV files for the audio engine (placed in the resources folder)
* Audacity (or similar) for batch-processing audio clips

## 🚀 How to Build and Install

1. Clone this repository to your local machine.
2. Place your processed `.wav` audio files into the `resources/audio/` directory.
3. Update `appinfo.json` to include your new audio resources.
4. Run `pebble build` in your terminal.
5. Install the generated `.pbw` file onto your Pebble via the Pebble app or `pebble install --emulator`.
