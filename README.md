This is an emualtor for the WWVB signal running on an Adafruit Huzzah32 Featherboard.

The goal here is to create a small device that will get the current time via NTP and then use a GPIO pin to generate an emulated WWVB signal.

The signal currently looks like this:

And is showing up as a nice spike on my spectrum analyzer (direct connection via a 20dB attenuator) - The noise is 70dB down from the peak but I don't have antennas yet so I can yet test OTA values.

