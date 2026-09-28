# Wiring

How to connect the CYD to a controller. A Bluetooth controller link needs no
wires beyond power; this page covers the wired VESC UART link and powering the
display from the controller.

Wiring mistakes on an electric vehicle can destroy a controller or start a
fire. If anything here does not match what is printed on your boards, stop and
check before connecting power.

## VESC over UART

![CYD to VESC UART wiring](images/wiring-vesc-uart.svg)

Four wires. Three of them go to the CYD's **CN1** connector, one to the power
connector.

| VESC COMM | CYD    | CYD connector   | Purpose                  |
| --------- | ------ | --------------- | ------------------------ |
| `TX/SCL`  | `IO27` | CN1             | VESC transmits, CYD receives |
| `RX/SDA`  | `IO22` | CN1             | CYD transmits, VESC receives |
| `GND`     | `GND`  | CN1             | Signal and power return  |
| `5V`      | `VIN`  | VIN/TX/RX/GND   | Display power            |

TX and RX cross over: the controller's transmit line goes to the display's
receive pin, and the other way around. The pins are set in
[`include/config.h`](../include/config.h) as `VESC_RX_PIN = 27` and
`VESC_TX_PIN = 22`.

### The CYD's two 4-pin connectors

They look alike but are not interchangeable, and only one carries the link:

- **CN1 — `GND, IO22, IO27, 3.3V`.** Free GPIOs, which the firmware maps UART2
  onto. **This is the link.**
- **`VIN, TX, RX, GND`.** `VIN` is the 5 V input. Its `TX`/`RX` are UART0 on
  most board revisions, i.e. GPIO1/GPIO3: shared with the USB-serial bridge,
  printed on by the boot ROM at every reset, and carrying the debug log. **Use
  it for power only.**

Ground runs in the CN1 bundle rather than beside `VIN`, so the UART pair has its
return alongside it and there is one ground path instead of a loop between two.
The display's ~250 mA returns down that same wire.

### Pins to leave unconnected

- `3.3V` at either end — it would fight the CYD's own regulator.
- `ADC1` / `ADC2` — throttle inputs.
- `PowerSW` — the controller's soft power-button input. Grounding it by
  accident shuts the controller down.

### Electrical notes

- No level shifting: both ends are 3.3 V logic.
- The display draws about 150–250 mA. A VESC-6-class 5 V rail supplies about
  1 A, shared with anything else on the CAN/PPM/SENSE 5 V pins.
- Once the display is fed from the controller, **disconnect that 5 V before
  plugging in USB** to flash, or two supplies share one rail.
- Keep the COMM cable short and away from the phase and battery leads. See
  [Cable and noise](#cable-and-noise).

### Cable and noise

Under heavy phase current a plain UART link can drop replies, especially on
high-voltage controllers: the display updates less often under throttle, or
briefly shows `WAITING FOR VESC`. The firmware rejects any reply whose CRC,
length or framing is wrong, so noise costs updates rather than showing wrong
numbers. Rule out motor detection first: poor motor parameters cause stutter
under load that looks like a link problem.

A shielded four-core cable helps. A USB 2.0 cable with its plugs cut off has
exactly the conductors needed, with the data pair twisted and the ground
running beside it:

| USB wire | Usual colour | Use |
| --- | --- | --- |
| VBUS | red | VESC `5V` to CYD `VIN` |
| GND | black | `GND` to `GND` on CN1 |
| D+ | green | VESC `TX/SCL` to `IO27` |
| D- | white | VESC `RX/SDA` to `IO22` |

- **Check it with a meter first.** Charge-only cables lack the data pair, cheap
  ones may have no shield or a floating one, and colours are not always
  standard.
- **Connect the shield to `GND` at the VESC end.** It may also go to `GND` at
  the CYD end, as long as the CYD has no other ground (no USB, nothing to the
  frame); otherwise leave that end open.
- **Cut the USB plugs off and label the cable,** so it is never plugged into a
  computer with the controller's 5 V and UART on it.
- 28 AWG power conductors drop about 0.1 V per metre at the display's current.
  Thicker ones (24 AWG) are better if available.

The shield mainly blocks electric-field coupling from the phase wires, whose
voltage swings by the full pack voltage on every PWM edge. It does little
against the magnetic field of the phase current; that is handled by keeping the
cable short, its conductors together, and its route away from the phase and
battery leads. If replies still drop, in order:

1. Clip a ferrite onto the cable near the CYD.
2. Lower the baud rate, for example to 57600 or 38400, in both VESC Tool and
   the display's controller Connection screen.
3. Add an RC low-pass at each receive pin, for example 220 Ω in series and
   1 nF to `GND` (about 0.2 µs against an 8.7 µs bit at 115200 baud).
4. Use the Bluetooth link instead, or an isolated UART (digital isolator plus
   isolated DC-DC).

### Controller configuration

In VESC Tool, set **App Settings > General > App to Use** to `UART` (or a
combined mode alongside PPM/ADC) and the UART baud rate to **115200**, matching
`VESC_BAUD`. The display's own baud rate can be changed later on its
controller Connection screen.

If the controller's built-in Bluetooth module shares the COMM UART rather than
using a separate one, it and the display will contend for the same lines.
Suspect that first if correct wiring produces no link.

### Bring-up

1. Wire `GND`, `TX` and `RX` only, and keep the display on USB power.
2. Power the controller. Within about a second of a working link the dashboard
   clears its `WAITING FOR VESC` overlay, and the serial log at 115200 baud
   reports `VESC telemetry connected`.
3. Once the link is solid, disconnect USB and add the 5 V lead.

## Tested controllers

| Controller | COMM connector | Notes |
| --- | --- | --- |
| Makerbase MKSESC 84200HP | 8-pin: `PowerSW, ADC2, TX/SCL, RX/SDA, ADC1, GND, 3.3V, 5V` | One connector supplies both link and power. |

Other VESC-based controllers expose the same `TX`, `RX`, `GND` and `5V`
signals, often on a differently ordered connector. Match by signal name, not by
position.

## CYD pin reference

All configured from `platformio.ini` build flags and `include/config.h`;
`User_Setup.h` does not need editing.

| Function | Pins |
| --- | --- |
| TFT (HSPI) | MOSI 13, MISO 12, SCLK 14, CS 15, DC 2, RST -1, BL 21 |
| Touch (VSPI) | MOSI 32, MISO 39, SCLK 25, CS 33, IRQ 36 |
| VESC UART (UART2) | RX 27, TX 22, 115200 baud |
| RGB LED (active-low) | red 4, green 16, blue 17 |
| Light sensor (LDR) | GPIO34 |

GPIO16/17 are kept free from UART use because they drive the RGB LED.
