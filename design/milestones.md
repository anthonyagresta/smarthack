## Firmware milestones
- [X] Blink.ino
- [ ] Configure wi-fi & UDP target host + port over serial port, resolve google.com or something to test
- [ ] Save wi-fi configuration to EEPROM, reload on boot
- [ ] Print arbitrary garbage data over UDP socket aimed at configured host
- [ ] Pressing GPIO button twice toggles "serial console" mode or "UDP fire hose" mode, default to UDP.
- [ ] Fetch current time via NTP, save time at millis = 0 to a global, add function to get current UNIX timestamp (Hardcode pool.ntp.org?)
- [ ] Read raw accelerometer data and print to serial port
- [ ] Maybe calibrate the accelerometer once? Set aside some calibration constants
- [ ] Send raw accelerometer data in an arbitrary format over configured UDP socket

=============== MVP LINE ========================
- [ ] Calculate AHRS data each loop & also send it
- [ ] Broadcast a "heartbeat" packet to the LAN once every few seconds so we can check the IP
- [ ] Tiny heartbeat client script, can send control instructions
- [ ] Device listens on a separate port for control instructions
- [ ] Other kool stuff?

