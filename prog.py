#!/usr/bin/env python3
# coding=utf-8
import sys
from spidriver import SPIDriver

if __name__ == '__main__':
    # Ensure the script is called with the correct arguments
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <device> <file>")
        sys.exit(1)

    device = sys.argv[1]
    bin_file = sys.argv[2]

    try:
        # Open SPI driver
        s = SPIDriver(device)

        # Read the binary file
        with open(bin_file, "rb") as f:
            data = f.read()

        # Write the binary data to the SPI device
        s.write(list(data))
        print(f"Successfully wrote {len(data)} bytes from {bin_file} to {device}")

        # Pull chip select high (this step assumes that the SPI driver has a way to manipulate CS)
        s.unsel()  # This method needs to exist or be added in your SPIDriver class

        # Write 49 dummy bytes (0x00)
        dummy_bytes = [0] * 49
        s.write(dummy_bytes)
        print("Successfully wrote 49 dummy bytes to the SPI device")
        s.sel()

    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)