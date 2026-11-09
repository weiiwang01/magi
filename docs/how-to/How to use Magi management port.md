The Magi management port is the USB-C port located at the bottom left of the
Magi board, near the three unit LEDs and the button.

The Magi management port internally connects to a 4-port USB hub. The four
downstream ports of the USB hub are connected to the Magi controller and the
three Raspberry Pi units' USB 2.0 ports.

You can connect the Magi management port to your computer using a USB-C cable to
access the Magi management console and flash the Raspberry Pis using rpiboot.
The Magi management port doesn't supply power to the Magi cluster, so you will
need an additional USB-C power supply to power up the Magi cluster.

Once connected, a `/dev/ttyACM*` device will appear on your system (Linux). You
can use a terminal emulator like `minicom` to access the Magi management
console, for example: `sudo minicom -D /dev/ttyACM0 -b 115200`.
