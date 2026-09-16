# s9sd

A Stream Deck controller for s9. It connects to s9 on this machine at the default port; `--url` points it elsewhere.

## Running

`python/make_venv.sh` installs it as a console script. Start s9, then:

```
python/venv/bin/s9sd
```

## Linux setup

The `streamdeck` package needs the libusb backend of hidapi, which Debian and Raspberry Pi OS ship separately from the hidraw one:

```
sudo apt install libhidapi-libusb0
```

The deck's USB device node is root-only by default. Rather than running under sudo, give Elgato devices to the `plugdev` group:

```
sudo tee /etc/udev/rules.d/70-streamdeck.rules <<'EOF'
SUBSYSTEMS=="usb", ATTRS{idVendor}=="0fd9", GROUP="plugdev", MODE="0660", TAG+="uaccess"
EOF
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=usb
```

A shipped unit needs both in its image.
