# Pinout And Signals

The device interface is intentionally minimal: SI/O and GND are the only electrical pins needed for protocol access. Package drawings include NC pins or package-specific pads/balls, but they do not add protocol signals. Source: datasheet, pp. 2, 5.

| Signal | Direction | Electrical/protocol notes | Source |
|---|---|---|---|
| SI/O | Bidirectional single-wire bus | Open-drain style signaling through a pull-up. The pull-up also powers the device, so timing and bus-idle high periods matter. | datasheet, pp. 1, 5, 12 |
| GND | Power reference | Required return for SI/O-powered operation. | datasheet, pp. 2, 5 |
| NC | None | Leave package NC pins unconnected unless the package drawing or board standard requires otherwise. | datasheet, p. 2 |

Package options listed by the datasheet include 2-pad XSFN, 3-lead SOT23, 8-lead SOIC, 4-ball WLCSP, and die sale options. Source: datasheet, pp. 1-2.

SI/O transaction facts:

- There is no SCL pin; bit timing is encoded on SI/O.
- A host creates logic 0, logic 1, ACK/NACK, Start, and Stop frames by driving SI/O low for the documented pulse widths and then releasing it.
- Reset/discovery also uses SI/O: the host drives SI/O low for `tRESET`, releases it, waits `tRRT`, requests discovery by driving SI/O low for `tDRR`, and samples during `tMSDR`.
- Multiple single-wire devices can share the bus when their preprogrammed slave address bits differ. Source: datasheet, p. 17.

Sources: datasheet, pp. 10-14, 17.
