# DSGrab - Command Line DirectShow Image Capture tool

DSGrab is a command line DirectShow Image Capture tool. It will capture a single
still image from a compatible capture device (e.g. web cam, tuner card, etc.) and
save it as a file to the disk. The primary purpose of the application is to create
a "live cam" feed on a web site by using it in conjuction with the built in Windows
Task Scheduler, a manipulator tool such as ImageMagik, and an FTP tool such as 
WinSCP. It is also (probably) the least amount of code you could write to interact
with the DirectShow APIs.

## TODO

* Add a YUV -> RGB colour space converter for capture devices
  that do not expose an RGB output pin.