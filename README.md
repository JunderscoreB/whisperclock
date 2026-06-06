# ⌚ WhisperClock for Pebble

WhisperClock is a highly customizable, stealthy time-telling app for Pebble smartwatches. It allows you to check the time privately by holding the watch to your ear and triggering a spoken audio announcement.

Designed for maximum reliability and efficiency, WhisperClock completely bypasses background sensors by default, ensuring the app consumes **zero battery** when not actively speaking the time.

## ✨ Key Features

* ⚡ **Instant Quick Launch:** Integrates directly with PebbleOS Quick Launch. Hold a single button from your watchface to instantly hear the time without navigating menus.
* 🔋 **Zero Background Battery Drain:** By utilizing hardware button interrupts instead of background accelerometer polling, the app CPU sleeps 100% of the time until triggered.
* 🔊 **Native 16-Bit Audio Engine:** Features real-time decoding of Signed 16-bit 16kHz PCM audio and a custom active-silence drip feed to protect the hardware DAC and ensure crystal-clear, zero-hiss playback.
* 🗣️ **Dynamic Prosody Engine:** Implements human-like speech pacing with dynamic playback modifiers, contextual pausing, and aggressive syllable compression for fluid, natural-sounding time announcements.

## 🛠️ How to Use

### 1. Map to Quick Launch (Required)
To get the true WhisperClock experience, map it to a physical hardware button:
1. On your Pebble, go to **Settings** -> **Quick Launch**.
2. Pick a button shortcut (e.g., `Up (Hold)` or `Down (Hold)`).
3. Scroll through your app list and select **WhisperClock**.

### 2. General Operation
* **Trigger the App:** From your watchface, press and hold your assigned Quick Launch button.
* **Listen:** Hold the watch to your ear. The app will immediately speak the time and automatically close itself when finished.
* **Cancel & Settings:** Press the **Up** button while the watch is speaking to instantly halt the audio and launch the Settings menu. Press the **Select**, **Down**, or **Back** buttons to simply cancel playback and close the app.

---

## 🎛️ Standard Features & Audio Tweaks

Launch WhisperClock normally from your Pebble's main app menu to configure your playback experience. Because Pebble audio can vary heavily depending on the recording, these DSP tools allow you to tune the voice to your exact preference:

* **Speaker Volume (1% - 100%):** A pure software-driven volume scaler that bypasses firmware gain limits, allowing for true "whisper" level playback. (Note: at 100% it is no longer whispering!)
* **Voice Interval:** Speeds up or slows down the physical millisecond pause between each spoken word.
* **Audio Trim:** Dynamically shaves off the silent tails at the end of each `.wav` file, creating a faster, punchier, and more robotic sentence structure.
* **Prefix & AM/PM:** Toggle conversational words like "It's" and "AM/PM" to shorten the playback time.
* **Clock Mode:** Force the app to speak in 12-Hour or 24-Hour (Military) time, or match your system default.

---

## 🧪 The Beta Physics Engine (Gestures & Tapping)

Want a truly hands-free experience? WhisperClock contains a hidden background physics engine that allows you to trigger the time using arm movements or physical taps.

To enable it, open WhisperClock and toggle **Beta Features** to **ON**. The menu will dynamically expand to reveal the physics settings:
* **Hardware Tap Detection:** Wake the app by rhythmically knocking on the watch glass. Set your required "Knock Count" (2 to 5 taps) to prevent accidental triggers.
* **Custom DTW Gestures:** Train the watch to recognize a specific arm motion (like raising your wrist to your ear). Tap **Record Gesture**, wait for the 3-second green countdown, and perform your motion. WhisperClock will save this 3D spatial template and use Dynamic Time Warping (DTW) to match your movement in the background.
* **Quiet Time Aware:** Automatically mutes the background worker and suspends the accelerometer if your Pebble is in Do Not Disturb mode or within your configured sleep hours.

*(Note: Enabling Beta features activates the Pebble background worker and 25Hz accelerometer polling, which will have a minor impact on daily battery life).*

---

## 🔬 Under the Hood (Tech Specs)

WhisperClock is built to be a modern, highly optimized Pebble app that takes full advantage of the latest hardware and algorithmic techniques:

* **Pebble Time 2 Touch Support:** Fully supports the Emery hardware architecture. WhisperClock implements custom `#ifdef PBL_TOUCH` physics, allowing users to scroll smoothly through the dynamic settings menu using the touch bezel.
* **Kinetic Momentum Scrolling:** Because native PebbleOS lacks inertial scrolling for touch-enabled windows, WhisperClock implements a custom 40 FPS kinetic physics loop. It tracks physical finger velocity during drag events and applies mathematical friction upon liftoff to simulate weight and momentum, complete with touch-to-catch stopping.
* **Dynamic Time Warping (DTW):** The Beta gesture engine doesn't rely on simple threshold triggers. It records a 3D spatial array of your wrist movement and utilizes a DTW algorithm to map the specific topological shape of your gesture. Furthermore, the algorithm is heavily optimized for embedded limits, utilizing a rolling 1D array to achieve $O(N)$ space complexity so it fits safely inside the Pebble's strict 10KB background RAM limit.
* **Smart Tap Debouncing:** The acoustic glass-tapping feature uses complex 3-axis math (`z_shock_sq > xy_shock_sq / 2`) to differentiate a deliberate glass knock from general wrist wobble. It also includes strict millisecond debouncing to reject mechanical recoil and false positives.
* **Live DSP Previews:** A custom UI overlay allows you to instantly test your Interval, Trim, and Volume settings directly inside the app without having to trigger the Quick Launch.
* **OTA-Safe Memory Architecture:** Built with strict boundary-checked persistent storage (`persist_read_data`), allowing users to safely update the app from the Rebble Store to newer versions without ever wiping their saved preferences.

## 🌍 Future Plans: Localization & Community Voices

WhisperClock currently only ships with an **English** voice pack. However, the audio engine is designed to be highly modular.

**Call for Contributors!**
We want to bring WhisperClock to Spanish, French, German, Japanese, and more. If you have a decent microphone and want to contribute a voice pack for your native language, we need you!
You only need to record a few dozen short, individual `.wav` files (e.g., numbers 1-20, 30, 40, 50, and basic prefixes).

If you are interested in translating the app and recording a voice pack, please open an Issue or submit a Pull Request!

---

## 🐛 Known Issues & Limitations

* **The Hardware "Pop":** You will hear a faint mechanical click about 1.5 seconds after the time finishes speaking. This is a known hardware limitation of the Pebble Time 2's Class-D amplifier power-gating, which is aggressively managed by the PebbleOS Kernel to save battery. We intentionally delayed this shutdown by 1500ms so it occurs after you have lowered your wrist.
* **Firmware Volume Override:** The PebbleOS `speaker_stream_open` API currently ignores volume parameters and defaults to 100% hardware gain. WhisperClock handles your volume preference purely in software to bypass this, but the hardware amplifier is still fully pressurized during playback.
* **Beta Physics Battery Drain:** Using the standard "Quick Launch" method uses 0% background battery. However, enabling the Beta Gestures & Tapping requires the Pebble to keep its background worker and accelerometer running at 25Hz. This will have a minor but noticeable impact on your smartwatch's multi-day battery life.

## ⚖️ License & Credits

MIT License

Copyright (c) 2026 J_B

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## 🤖 AI Disclosure
Parts of the codebase, specifically the native 16-bit audio parsing, dynamic speech prosody, accelerometer physics engine, and dynamic UI toggles, were generated and optimized with the assistance of generative AI (Google Gemini).
