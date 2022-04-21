# wizfs
A 9Front file server for interacting with Philips Wiz light bulbs
by adventuresin9

usage: wizfs -m "mount point" -s "srv name" -d (set debug true)

The defaults are to mount "wiz" in /n, and post "wizfs" to /srv.

The commands that can be sent to the bulbs;

on : sends "state":true to turn the bulb on
off : send "state":false to turn the bulb off
pulse: uses the pulse method to make the bulb dim for half a second

All other commands have to be sent as a key=value pair.
Known working options are:

state=(0 or 1)(false or true) turns the bulb off or on
r=(0-255) for the red LED
g=(0-255) for green
b=(0-255) for blue
c=(0-255) for cool white
w=(0-255) for warm white
temp=(2200-6500) for warm to cool white light
dimming=(10-100) dims the bulbs
sceneId=(0-32) sets one of the prepackaged "scenes", note the capital I in Id.
speed=(0-200) used for sceneId that cycles through colors, makes is go faster or slower

Sometimes the bulb will send back an error if the value is outside a given range.
Other times it will report success, but the value might be looped back around to 0.
Like, b=265 looks like b= 10.  So bits over a certain value may be truncated by the bulb.
