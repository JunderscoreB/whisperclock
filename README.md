# ⌚ WhisperClock for Pebble

WhisperClock is a highly customizable, stealthy time-telling app for Pebble smartwatches. It allows you to check the time privately by holding the watch to your ear and triggering a spoken audio announcement using either a wrist flick or by physically tapping the glass.

Designed with battery life and memory efficiency in mind, WhisperClock runs a deeply optimized background worker that listens for your specific triggers while letting the main CPU sleep.

## ✨ Key Features

* **✊ Hardware Tap Detection:** Wake the app by knocking on the watch glass. Set your required "Knock Count" (2 to 5 taps) to prevent accidental triggers.
* **🦾 Custom DTW Gestures:** Don't like the default wrist flick? Record a custom arm motion (like raising your arm to your ear). WhisperClock uses Dynamic Time Warping (DTW) to match your movement in the background.
* **🔋 Battery Optimized:** The background worker uses a "Motion Gate" to instantly drop accelerometer data when the watch is still, ensuring the complex gesture math only runs when you are actually moving.
* **🤫 Quiet Time Aware:** Automatically mutes the spoken audio if your Pebble is in Do Not Disturb mode, but can be overridden.
* **⚙️ Advanced Playback Controls:** Tweak the speaker volume, adjust the interval speed between spoken words, and trim the end of the audio clips for punchier playback.

## 🛠️ How to Use

### General Operation
1. **Trigger the App:** Depending on your settings, either perform your wrist sweep gesture or tap the glass (default is 3 taps).
2. **Listen:** Hold the watch to your ear to hear the time.
3. **Cancel:** Press **any** physical button on the watch or tap the screen to instantly kill the audio and close the app.

### Training a Custom Gesture
To replace the default wrist flick with your own custom movement (like smoothly lifting your arm to your ear):
1. Open WhisperClock from the Pebble system menu.
2. Scroll down and select **Record Gesture**.
3. **Get Ready (Amber Screen):** You have 3 seconds to place your arm into your natural starting position. 
4. **Record (Green Screen):** The watch will buzz. Perform your desired motion smoothly. A live countdown will show you how much recording time is left.
5. **Finish (Red Screen):** The watch will buzz twice and show "Saved!". The background listener will automatically reboot and start listening for your newly trained motion immediately!
*(Tip: You can change the total duration of the recording phase under the "Recording Time" setting).*

## 🎛️ Settings Menu

Launch WhisperClock from your Pebble's main menu to configure:
* **Clock Mode:** Auto (matches system), Force 12-Hour, or Force 24-Hour.
* **Quiet Time:** Choose whether to respect DND or always speak.
* **Trigger Method:** Choose between "Gesture Sweep", "Glass Tapping", or Both.
* **Knock Count:** How many physical glass taps are required to wake the app.
* **Record/Clear Gesture:** Train a custom accelerometer pattern or revert to the default flick.
* **Prefix & AM/PM:** Toggle conversational words like "It's" and "AM/PM".
* **Audio Tweaks:** Fine-tune Volume, Voice Interval, and Audio Trim.
