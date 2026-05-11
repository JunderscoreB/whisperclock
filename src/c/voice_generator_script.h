#pragma once

#if 0
// ==========================================
// TO RUN THIS SCRIPT:
// Copy this text, save it as "generate.sh" on your Gentoo machine,
// run 'chmod +x generate.sh', and execute it!
// ==========================================

#!/bin/bash

# Create a directory for the audio and enter it
mkdir -p whisper_audio && cd whisper_audio

echo "Generating numbers 1 through 59..."
# 1. Generate numbers 1 through 59
for i in {1..59}; do
  echo " -> $i.wav"
  espeak-ng -w temp.wav "$i"
  ffmpeg -y -i temp.wav -ac 1 -ar 16000 -acodec pcm_u8 "$i.wav" -loglevel error
done

echo "Generating special vocabulary words..."
# 2. Generate the special time words
# We spell out A M and P M so the TTS engine doesn't try to pronounce them as words
declare -A words=( 
  ["oh"]="Oh" 
  ["oclock"]="O'clock" 
  ["am"]="A M" 
  ["pm"]="P M" 
  ["its"]="It's"
  ["noon"]="Noon"
  ["zero"]="Zero"
  ["hundred"]="Hundred"
  ["hours"]="Hours"
)

for key in "${!words[@]}"; do
  echo " -> $key.wav (${words[$key]})"
  espeak-ng -w temp.wav "${words[$key]}"
  ffmpeg -y -i temp.wav -ac 1 -ar 16000 -acodec pcm_u8 "$key.wav" -loglevel error
done

# Clean up the temporary file
rm temp.wav
echo "======================================================"
echo "Success! All audio generated and formatted for Pebble!"
echo "======================================================"

  /*
  
  The Master Audio Checklist

1. The Flavor & Connectors

    its.wav ("It's...")

    oh.wav (Used for minutes 01-09, e.g., "Oh five")

2. The 12-Hour Standard Modifiers

    am.wav

    pm.wav

    oclock.wav

    noon.wav (For exactly 12:00 PM)

3. The 24-Hour Military Modifiers

    zero.wav (For the midnight hour: "Zero hundred")

    hundred.wav (For the top of the hour: "Fourteen hundred")

    hours.wav (To append to military top-of-hour: "Twelve hundred hours")

4. The Numbers (1 through 59)

    1.wav through 59.wav
  
  */
  
  
  
#endif