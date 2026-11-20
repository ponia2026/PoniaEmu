#!/usr/bin/env python3
# Virtual keyboard through /dev/uinput (root), to drive the frontend from a script when testing on a board:
#   python3 uinput_keys.py Return:3 Right Esc i:1 f a m i c o   (each key, optional ":seconds to wait after it")
import fcntl, os, struct, sys, time

KEYS = {"Esc": 1, "Return": 28, "Up": 103, "Left": 105, "Right": 106, "Down": 108, "F1": 59, "Backspace": 14, "Space": 57,
        "a": 30, "b": 48, "c": 46, "d": 32, "e": 18, "f": 33, "g": 34, "h": 35, "i": 23, "j": 36, "k": 37, "l": 38, "m": 50, "n": 49, "o": 24, "p": 25, "q": 16, "r": 19, "s": 31, "t": 20, "u": 22, "v": 47, "w": 17, "x": 45, "y": 21, "z": 44}
UI_SET_EVBIT, UI_SET_KEYBIT, UI_DEV_CREATE, UI_DEV_DESTROY = 0x40045564, 0x40045565, 0x5501, 0x5502

fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
fcntl.ioctl(fd, UI_SET_EVBIT, 1)
for code in KEYS.values():
    fcntl.ioctl(fd, UI_SET_KEYBIT, code)
user_dev = struct.pack("80sHHHHi", b"ponia-test-kbd", 3, 0x1234, 0x5678, 1, 0) + b"\0" * (4 * 64 * 4)
os.write(fd, user_dev)
fcntl.ioctl(fd, UI_DEV_CREATE)
time.sleep(1.5)

def ev(t, c, v):
    os.write(fd, struct.pack("llHHi", 0, 0, t, c, v))

def press(name):
    code = KEYS[name]
    ev(1, code, 1); ev(0, 0, 0); time.sleep(0.08)
    ev(1, code, 0); ev(0, 0, 0)

for arg in sys.argv[1:]:
    name, _, wait = arg.partition(":")
    press(name)
    time.sleep(float(wait) if wait else 0.6)

fcntl.ioctl(fd, UI_DEV_DESTROY)
os.close(fd)
