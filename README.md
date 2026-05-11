# WhisperClock for PebbleOS

**WhisperClock** is a highly customizable, natural-sounding speaking clock app for PebbleOS-based smartwatches that have speakers (currently supported: Pebble Time 2). 

## 🎯 Purpose
Traditionally, wristwatches require visual attention to tell the time, but a smartwatch with a speaker allows the time to be spoken. This is especially useful for people who cannot use their eyes to check the watch, including those who are visually impaired, not wearing their glasses, unable to take their eyes off a task (e.g., driving), or wanting to check the time in a dark place without using a harsh backlight (e.g., sleeping, stargazing). The intention is for the wearer to lift the watch up to their ear to hear the time spoken to them, allowing for a quiet, discreet, and private time check.

## ✨ Features

* **Motion Activated:** Hear the time simply by flicking your wrist using Pebble's native accelerometer.
* **Custom Gesture Training:** Don't like the standard wrist flick? Record and train your own custom arm movement to trigger the clock using dynamic time-warping.
* **Intelligent Audio Stitching:** Dynamically handles 12-hour and 24-hour (military) time formats seamlessly. Can be set independently from your system watch settings.
* **Pebble Quiet Time Integration:** Automatically mutes itself when your watch is in Do Not Disturb mode—preventing accidental interruptions during meetings or sleep. Can be overridden in settings. 
* **Quick Kill Switch:** Tap any button or the screen to instantly cancel audio playback.
* **Deep Audio Customization:**
  * **Voice Interval:** Adjust the millisecond delay (50ms - 500ms) between stitched words for faster or slower reading speeds.
  * **Audio Trim:** Automatically trim dead space from the end of audio clips for punchier, faster playback.
  * **Volume Control:** Independent speaker volume levels (10% - 100%).
  * **Prefix/Suffix Toggles:** Choose whether the watch says "It's..." and "AM/PM".
* **UI Displays the Time:** Shows a high-contrast digital time readout while speaking; the backlight remains off to preserve night vision and battery.  

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

## 🗺️ Future Plans

* Localization for regional English dialects
* Support for additional languages
* Compatibility with future PebbleOS watches featuring speakers
* General performance tweaks and quality-of-life improvements

We are happy to hear about other suggested tweaks or features from users. Language-pack contributions are welcome!
