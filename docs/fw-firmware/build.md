---
title: "How to build firmware"
---

For those who want to build Mongoose IoT firmware themselves, here is an
instruction on how to do that:

1. Make sure you have [Docker](https://www.docker.com/) installed and running
2. Execute the following:

```
$ git clone https://github.com/cesanta/mongoose-iot.git
$ cd mongoose-iot/fw/platforms/esp8266
$ make
```

The firmware gets built in the `firmware/` folder. Copy it to the
Fish'n'chips's `firmware/esp8266/` directory, and it'll be ready to flash!

NOTE: for Windows users:

- Operations above should be performed from `Docker QuickStart terminal` console, not from `cmd.exe`
- Only `C:\Users` is mapped into docker's VM by default, so put cloned repo to `Users` subfolder

NOTE: for Mac users:

- Only `/Users` is mapped into docker's VM by default, so put cloned repo to `Users` subfolder

NOTE: for Linux users:

- Some minimal distribution installs don't ship with `make`. If you don't have it you can install it with:
  - ubuntu: `apt-get install make`
  - centos: `yum install make`
  - arch: `pacman -S make`
