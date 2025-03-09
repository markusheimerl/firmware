#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spidriver/c/common/spidriver.h"

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <device> <file>\n", argv[0]);
        return 1;
    }

    const char *device = argv[1];
    const char *bin_file = argv[2];
    
    SPIDriver sd;
    FILE *fp;
    char *buffer;
    long file_size;
    
    // Initialize SPI driver
    spi_connect(&sd, device);
    if (!sd.connected) {
        fprintf(stderr, "Error: Could not connect to SPI device %s\n", device);
        return 1;
    }
    
    // Open binary file
    fp = fopen(bin_file, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Could not open file %s\n", bin_file);
        return 1;
    }
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    rewind(fp);
    
    // Allocate memory for file content
    buffer = (char*)malloc(file_size);
    if (!buffer) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        fclose(fp);
        return 1;
    }
    
    // Read file content
    if (fread(buffer, 1, file_size, fp) != file_size) {
        fprintf(stderr, "Error: Could not read the entire file\n");
        free(buffer);
        fclose(fp);
        return 1;
    }
    fclose(fp);
    
    // Select chip
    spi_sel(&sd);
    
    // Write the binary data to the SPI device
    spi_write(&sd, buffer, file_size);
    printf("Successfully wrote %ld bytes from %s to %s\n", file_size, bin_file, device);
    
    // Unselect chip
    spi_unsel(&sd);
    
    // Write 49 dummy bytes (0x00)
    char dummy_bytes[49] = {0};
    spi_write(&sd, dummy_bytes, 49);
    printf("Successfully wrote 49 dummy bytes to the SPI device\n");
    
    // Select chip again (as in Python script)
    spi_sel(&sd);
    
    // Clean up
    free(buffer);
    
    return 0;
}