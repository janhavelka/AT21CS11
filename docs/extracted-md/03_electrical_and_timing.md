# Electrical And Timing

Use these AT21CS01/AT21CS11 limits to size SI/O bit timing and validation checks.

| Parameter | AT21CS01 | AT21CS11 | Source |
|---|---:|---:|---|
| Pull-up voltage | 1.7 V to 3.6 V in high-speed mode; 2.7 V to 3.6 V in standard-speed mode | 2.7 V to 4.5 V in high-speed mode | datasheet, pp. 8-9 |
| Temperature range | -40 degC to +85 degC | -40 degC to +85 degC | datasheet, p. 1 |
| Max bit rate | 15.4 kbps standard speed, 125 kbps high speed | 125 kbps high speed only | datasheet, pp. 1, 10, 20 |
| Reset low time | 480 us standard-speed timing; 96 us high-speed timing | 96 us high-speed timing | datasheet, p. 10 |
| Reset recovery time | 8 us in high-speed timing | 8 us | datasheet, p. 10 |
| Discovery acknowledge time | 8 us to 24 us | 8 us to 24 us | datasheet, p. 10 |
| Start/stop SI/O high time | 150 us minimum | 150 us minimum | datasheet, p. 10 |
| Write cycle time | 5 ms max | 5 ms max | datasheet, p. 10 |

The datasheet notes that the device defaults to high-speed mode after reset and that standard speed is not available on AT21CS11. Source: datasheet, pp. 10, 20.

High-speed data communication timing:

| Parameter | Symbol | High-speed limit | Source |
|---|---|---:|---|
| Bit frame duration | `tBIT` | `tLOW0 + tPUP + tRCV` min, 25 us max | datasheet, pp. 10-11 |
| SI/O low time, logic 0 | `tLOW0` | 6 us to 16 us | datasheet, p. 10 |
| SI/O low time, logic 1 | `tLOW1` | 1 us to 2 us | datasheet, p. 10 |
| Master SI/O low during read | `tRD` | `1 us - tPUP` to `2 us - tPUP` | datasheet, p. 10 |
| Master read strobe time | `tMRS` | `tRD + tPUP` to 2 us | datasheet, pp. 10-11 |
| Data output hold, logic 0 | `tHLD0` | 2 us to 6 us | datasheet, pp. 10-11 |
| Slave recovery time | `tRCV` | 2 us min | datasheet, pp. 10-11 |

During a nonvolatile write, security lock, ROM-zone write, or ROM-zone freeze,
the device NACKs commands until `tWR` has elapsed. SI/O must remain pulled high
through the external pull-up for the full internal write cycle. Source:
datasheet, pp. 21-24, 29-31.
