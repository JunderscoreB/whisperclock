# ⌚ WhisperClock for Pebble

WhisperClock is a highly customizable, stealthy time-telling app for Pebble smartwatches. It allows you to check the time privately by holding the watch to your ear and triggering a spoken audio announcement.

Designed for maximum reliability and efficiency, WhisperClock v1.0 completely bypasses background sensors by default, ensuring the app consumes **zero battery** when not actively speaking the time.

## ✨ Key Features

* ⚡ **Instant Quick Launch:** Integrates directly with PebbleOS Quick Launch. Hold a single button from your watchface to instantly hear the time without navigating menus.
* 🔋 **Zero Background Battery Drain:** By utilizing hardware button interrupts instead of background accelerometer polling, the app CPU sleeps 100% of the time until triggered.
* 🔊 **16-Bit Audio Engine:** Features real-time audio upsampling and a custom active-silence drip feed to protect the hardware DAC and ensure crystal-clear audio playback.

## 🛠️ How to Use

### 1. Map to Quick Launch (Required)
To get the true WhisperClock experience, map it to a physical hardware button:
1. On your Pebble, go to **Settings** -> **Quick Launch**.
2. Pick a button shortcut (e.g., `Up (Hold)` or `Down (Hold)`).
3. Scroll through your app list and select **WhisperClock**.

### 2. General Operation
* **Trigger the App:** From your watchface, press and hold your assigned Quick Launch button. 
* **Listen:** Hold the watch to your ear. The app will immediately speak the time and automatically close itself when finished.
* **Cancel:** Press **any** physical button on the watch or tap the screen to instantly kill the audio and close the app.

---

## 🎛️ Standard Features & Audio Tweaks

Launch WhisperClock normally from your Pebble's main app menu to configure your playback experience. Because Pebble audio can vary heavily depending on the recording, these DSP tools allow you to tune the voice to your exact preference:

* **Speaker Volume (1% - 100%):** A pure software-driven volume scaler that bypasses firmware gain limits, allowing for true "whisper" level playback.
* **Voice Interval:** Speeds up or slows down the physical millisecond pause between each spoken word.
* **Audio Trim:** Dynamically shaves off the silent tails at the end of each `.wav` file, creating a faster, punchier, and more robotic sentence structure.
* **Prefix & AM/PM:** Toggle conversational words like "It's" and "AM/PM" to shorten the playback time.
* **Clock Mode:** Force the app to speak in 12-Hour or 24-Hour (Military) time, or match your system default.

---

## 🧪 The Beta Physics Engine (Gestures & Tapping) -- Under active development, use at your own risk!

Want a truly hands-free experience? WhisperClock contains a hidden background physics engine that allows you to trigger the time using arm movements or physical taps.

To enable it, open WhisperClock and toggle **Beta Features** to **ON**. The menu will dynamically expand to reveal the physics settings:
* **Hardware Tap Detection:** Wake the app by rhythmically knocking on the watch glass. Set your required "Knock Count" (2 to 5 taps) to prevent accidental triggers.
* **Custom DTW Gestures:** Record a custom arm motion (like raising your arm to your ear). WhisperClock uses Dynamic Time Warping (DTW) to match your movement in the background.
* **Quiet Time Aware:** Automatically mutes the background worker and suspends the accelerometer if your Pebble is in Do Not Disturb mode or within your configured sleep hours.

*(Note: Enabling Beta features activates the Pebble background worker and 25Hz accelerometer polling, which will have a minor impact on daily battery life).*

---

## 🔬 Under the Hood (Tech Specs)

WhisperClock is built to be a modern, highly optimized Pebble app that takes full advantage of the latest hardware and algorithmic techniques:

* **Pebble Time 2 Touch Support:** Fully supports the Emery hardware architecture. WhisperClock implements custom `#ifdef PBL_TOUCH` physics, allowing users to scroll smoothly through the dynamic settings menu using the touch bezel.
* **Dynamic Time Warping (DTW):** The Beta gesture recognition engine doesn't rely on simple threshold triggers. It records a 3D spatial array of your wrist movement and utilizes a lightweight DTW algorithm to map and identify the specific topological shape of your gesture in real-time.
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

## 💻 Developer Notes: The Hardware Power-Gate
This app employs a highly specialized audio engine to circumvent a known hardware/firmware constraint on the Pebble Time 2. The PebbleOS kernel power-gates the Class-D smart amplifier immediately upon calling `speaker_stream_close()`, causing a mechanical pop as the capacitors discharge. Furthermore, the `speaker_stream_open()` API ignores user volume arguments.

To solve this, WhisperClock handles all volume attenuation mathematically in user-space software to prevent starvation, pushes a continuous drip-feed of pure digital silence to the DMA pipeline between words to keep the speaker cone centered, and utilizes an intentional 1500ms delay before stream shutdown to ensure the hardware power-gate only occurs after the user has lowered their wrist.

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
Parts of the codebase, specifically the 16-bit audio upsampling algorithms, accelerometer physics engine, and dynamic UI toggles, were generated and optimized with the assistance of generative AI (Google Gemini).

