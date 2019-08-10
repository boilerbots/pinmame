# PinMAME

I cloned the original pinmame repo into github so I can hack on it.

My repo contains branches of various and drastic hacks I am playing with
to understand how PinMAME work and modify it to run on a raspberrypi.

## Branches and Hacks

* pi-build - Get the project running on a raspberry pi, with reasonable results.
* dmddump - Just output the DMD frames to the console, not useful.
* LED - Hack in LED DMD matric support. The DMD data is directly drawn on an attached DMD display. (https://github.com/hzeller/rpi-rgb-led-matrix)
* cleanup - Try to remove as much as possible without breaking the code. This makes it easier to understand what is going on.
* threading - Newer Pis have 4 cores, so lets try to multi-thread the code. Move sound into a thread, like the real hardware!


Youtube playlist of some examples (https://www.youtube.com/playlist?list=PLWYqDHKzcHkutDZyzECJIuOeYRO6jvw9q)


