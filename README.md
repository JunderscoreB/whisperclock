# ⌚ WhisperClock for Pebble

WhisperClock is a highly customizable, stealthy time-telling app for Pebble smartwatches. It allows you to check the time privately by holding the watch to your ear and triggering a spoken audio announcement.

Designed for maximum reliability and efficiency, WhisperClock completely bypasses background sensors by default, ensuring the app consumes **zero battery** when not actively speaking the time.

## ✨ Key Features

* ⚡ **Instant Quick Launch:** Integrates directly with PebbleOS Quick Launch. Hold a single button from your watchface to instantly hear the time without navigating menus.

* 🔋 **Zero Background Battery Drain:** By utilizing hardware button interrupts instead of background accelerometer polling, the app CPU sleeps 100% of the time until triggered.

* 🔊 **Native 16-Bit Audio Engine:** Features real-time decoding of Signed 16-bit 16kHz PCM audio and a custom active-silence drip feed to protect the hardware DAC and ensure crystal-clear, zero-hiss playback.

* 🗣️ **Dynamic Prosody Engine:** Implements human-like speech pacing with dynamic playback modifiers, contextual pausing, and aggressive syllable compression for fluid, natural-sounding time announcements.

* 🌊 **Live Animated Visualizer:** A low-cost, LUT-based waveform visualizer bounces in perfect synchronization with the audio clip durations while displaying the current time and date.

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

## 🎛️ Standard Features & Audio Tweaks

Launch WhisperClock normally from your Pebble's main app menu to configure your playback experience. Because Pebble audio can vary heavily depending on the recording, these DSP tools allow you to tune the voice to your exact preference:

* **Clock Modes:** Choose exactly how you want the time read to you:

  * **System Default:** Automatically mirrors your Pebble's native 12-hour or 24-hour global system settings.

  * **12-Hour Digital:** Standard AM/PM readout (e.g., "Five Oh Five PM").

  * **24-Hour Military:** Zero-padded military format (e.g., "Zero Five Hundred Hours").

  * **24-Hour Civilian:** 24-hour style without the jargon (e.g., "Oh Five Oh Five").

  * **Colloquial:** Natural human phrasing (e.g., "Quarter Past Five").

  * **Telecom Radio:** Vintage time station readout that calculates future bounds to play an exact top-of-the-minute tone (e.g., "At the tone, it will be 10 hours, 12 minutes, precisely... *BEEP*").

  * **Fuzzy:** Casual rounding (e.g., "Almost quarter to five").

* **Phrase Dialect:** Toggle between US ("Till/After") and UK ("To/Past") phrasing for Colloquial and Fuzzy modes.

* **Smart Grammar & Edge Cases:** The audio engine dynamically applies singular/plural grammar rules (e.g., "One minute" vs. "Two minutes") and intelligently suppresses AM/PM tags for conversational edge cases like "Noon" and "Midnight."

* **Granular Speaker Volume:** A pure software-driven volume scaler that bypasses firmware gain limits. Features ultra-precise 1% increments up to 20%, and 5% steps up to 100%. (Note: at 100% it is no longer whispering!)

* **True Bedside Mode (Quiet Time):** Set custom Night Start and Night End hours to automatically lower the volume, mute the app, or put the background physics worker to sleep while you are in bed. Quiet Time dynamically routes your volume independently of the gesture engine, allowing your watch to stay perfectly silent during the day but whisper softly at night.

* **Independent Speech Pacing Tuner:** A dedicated calibration board allowing you to tune the exact millisecond gaps, trims, and glides between words independently for each clock mode. Finding the perfect conversational glide in Colloquial Mode will no longer break the snappy pacing of your Military Mode!

## 🧪 The Experimental Physics Engine (Gestures & Tapping)

Want a truly hands-free experience? WhisperClock contains a hidden background physics engine that allows you to trigger the time using arm movements or physical taps.

To enable it, open WhisperClock and toggle **Experimental Features** to **ON**. The menu will dynamically expand to reveal the physics settings:

* **Hardware Tap Detection:** Wake the app by rhythmically knocking on the watch glass. Uses strict geometric wobble-filtering (`z_sq > (x_sq + y_sq) * 2`) to reject arm swings and prevent false positives.

* **Anti-Clap Wrist Flick (Bi-Directional Snap):** The default wrist-flick mode searches for a very specific 100-millisecond rotational forearm signature (a massive positive force spike immediately followed by a negative deceleration snap) making it completely immune to violent false positives like clapping. Includes a simple 0-12 "Sensitivity" UI scale.

* **Custom DTW Gestures:** Train the watch to recognize a specific arm motion (like raising your wrist to your ear). Tap **Record Gesture**, wait for the 3-second green countdown, and perform your motion. WhisperClock will save this 3D spatial template and use Dynamic Time Warping (DTW) to match your movement in the background.

*(Note: Enabling Experimental features activates the Pebble background worker and 25Hz accelerometer polling, which will have a minor impact on daily battery life).*

## 🔬 Under the Hood (Tech Specs)

WhisperClock is built to be a modern, highly optimized Pebble app that takes full advantage of the latest hardware and algorithmic techniques:

* **OS-Level Playback Limits & Weak Polyfills:** Fully adopts the new 2026 PebbleOS SDK hardware limits. Dynamically polls `speaker_get_max_duration_ms()` to securely truncate audio before RAM allocation, and `speaker_get_max_volume()` to safely clamp volume spoofing, preventing hardware brownouts. Implemented with `__attribute__((weak))` polyfills to guarantee compiler/linker safety across older SDK versions.

* **Single-Pass Audio Pipeline:** The ADPCM decompression algorithm, software volume scaler, and active-silence fade ratios have been merged into a highly optimized single CPU-register pass, halving RAM I/O iterations and drastically speeding up buffer parsing.

* **Telecom "Time Travel" Scheduling:** In Telecom mode, the audio engine computes the physical duration of the required words in milliseconds, looks into the future to find the next 5-second boundary, and uses the `time_ms()` API to trigger the final beep with microsecond accuracy.

* **Flash Memory Wear Prevention:** UI navigation is completely decoupled from Pebble's `persist_write_data()` API. The app only commits physical flash memory writes when you exit a menu row, protecting the smartwatch's delicate flash storage sectors from permanent degradation during rapid kinetic scrolling.

* **Worker & Memory Safety Hardening:** Replaced heavy background physics integer division with pre-calculated thresholds to maximize STM32 sleep cycles. Patched structural buffer overflows (`MAX_BUFFER_SIZE`), enforced strict null-terminated string boundaries (`queue_audio_ext`), and safely manages the gesture worker lifecycle to permanently eliminate the "Dead Worker" bug.

* **Pebble Time 2 Touch Support:** Fully supports the Emery hardware architecture. WhisperClock implements custom `#ifdef PBL_TOUCH` physics, allowing users to scroll smoothly through the dynamic settings menu using the touch bezel, complete with dynamic resolution-aware hitboxes that account for the 16-pixel status bar.

* **Kinetic Momentum Scrolling:** Because native PebbleOS lacks inertial scrolling for touch-enabled windows, WhisperClock implements a custom kinetic physics loop. It tracks physical finger velocity during drag events and applies mathematical friction upon liftoff to simulate weight and momentum, complete with touch-to-catch stopping.

* **Dynamic Time Warping (DTW):** The experimental gesture engine doesn't rely on simple threshold triggers. It records a 3D spatial array of your wrist movement and utilizes a DTW algorithm to map the specific topological shape of your gesture. Furthermore, the algorithm is heavily optimized for embedded limits, utilizing a rolling 1D array to achieve $O(N)$ space complexity so it fits safely inside the Pebble's strict 10KB background RAM limit.

* **Live DSP Previews:** A custom UI overlay allows you to instantly test your Interval, Trim, and Volume settings directly inside the app without having to trigger the Quick Launch.

* **OTA-Safe Memory Architecture:** Built with strict boundary-checked persistent storage (`persist_read_data`), allowing users to safely update the app from the Rebble Store to newer versions without ever wiping their saved preferences.

## 🌍 Future Plans: Localization & Community Voices

WhisperClock currently ships with an **English** voice pack featuring US and UK dialects. However, the audio engine is designed to be highly modular.

**Call for Contributors!**
We want to bring WhisperClock to Spanish, French, German, Japanese, and more. If you have a decent microphone and want to contribute a voice pack for your native language, we need you!
You only need to record a few dozen short, individual `.wav` files (e.g., numbers 0-20, 30, 40, 50, and basic prefixes/connectors).

If you are interested in translating the app and recording a voice pack, please open an Issue or submit a Pull Request!

## 🐛 Known Issues & Limitations

* **The Hardware "Pop":** You will hear a faint mechanical click about 1 second after the time finishes speaking. This is a known hardware limitation of the Pebble Time 2's Class-D amplifier power-gating, which is aggressively managed by the PebbleOS Kernel to save battery. We intentionally delayed this shutdown so it occurs after you have lowered your wrist.

* **Firmware Volume Override:** The PebbleOS `speaker_stream_open` API currently ignores volume parameters and defaults to 100% hardware gain. WhisperClock handles your volume preference purely in software to bypass this, but the hardware amplifier is still fully pressurized during playback.

* **Experimental Physics Battery Drain:** Using the standard "Quick Launch" method uses 0% background battery. However, enabling the Experimental Gestures & Tapping requires the Pebble to keep its background worker and accelerometer running at 25Hz. This will have a minor but noticeable impact on your smartwatch's multi-day battery life.

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

Parts of the codebase, specifically the native 16-bit audio parsing, dynamic speech prosody, accelerometer physics engine, and dynamic UI toggles, were generated and optimized with the assistance of generative AI.
