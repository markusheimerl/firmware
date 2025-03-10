#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#define SPI_DEVICE "/dev/spidev0.0"  // Default SPI device on Raspberry Pi
#define SPI_SPEED 1000000            // 1 MHz
#define SPI_BITS_PER_WORD 8
#define SPI_MODE SPI_MODE_0          // Mode 0: CPOL=0, CPHA=0
#define MAX_TRANSFER_SIZE 4096       // Maximum size for a single transfer

int spi_open(const char *device, int no_cs) {
    int fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("Failed to open SPI device");
        return -1;
    }

    uint8_t mode = SPI_MODE;
    uint8_t bits = SPI_BITS_PER_WORD;
    uint32_t speed = SPI_SPEED;
    
    // Set no CS mode if requested
    if (no_cs) {
        mode |= SPI_NO_CS;
    }

    // Set SPI mode
    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0) {
        perror("Failed to set SPI mode");
        close(fd);
        return -1;
    }

    // Set bits per word
    if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
        perror("Failed to set bits per word");
        close(fd);
        return -1;
    }

    // Set max speed
    if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        perror("Failed to set SPI speed");
        close(fd);
        return -1;
    }

    return fd;
}

// Manually control SPI transfer with CS control
int spi_transfer_chunk(int fd, const uint8_t *tx_buffer, uint8_t *rx_buffer, size_t len, int cs_active) {
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx_buffer,
        .rx_buf = (unsigned long)rx_buffer,
        .len = len,
        .speed_hz = SPI_SPEED,
        .delay_usecs = 0,
        .bits_per_word = SPI_BITS_PER_WORD,
        .cs_change = cs_active ? 0 : 1,  // Keep CS active or deactivate after transfer
    };

    int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret < 0) {
        perror("Failed to perform SPI transfer");
        return -1;
    }

    return ret;
}

int spi_transfer(int fd, const uint8_t *tx_buffer, uint8_t *rx_buffer, size_t len, int keep_cs_active) {
    size_t remaining = len;
    size_t offset = 0;
    int ret;

    while (remaining > 0) {
        size_t chunk_size = (remaining > MAX_TRANSFER_SIZE) ? MAX_TRANSFER_SIZE : remaining;
        int is_last_chunk = (remaining <= MAX_TRANSFER_SIZE);
        
        // Only deactivate CS on the last chunk if not keeping it active
        int cs_active = keep_cs_active || !is_last_chunk;
        
        ret = spi_transfer_chunk(fd, tx_buffer + offset, 
                                rx_buffer ? rx_buffer + offset : NULL, 
                                chunk_size, cs_active);
        
        if (ret < 0) {
            return -1;
        }
        
        remaining -= chunk_size;
        offset += chunk_size;
    }
    
    return len;
}

// Function to wait for user input before sending data
int wait_for_user_confirmation(const char *message) {
    char input[10];
    printf("\n========================================================\n");
    printf("%s\n", message);
    printf("Press Enter to continue.");
    printf("\n========================================================\n");
    return (fgets(input, sizeof(input), stdin) != NULL);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        return 1;
    }

    const char *bin_file = argv[1];
    
    int spi_fd;
    FILE *fp;
    uint8_t *buffer;
    long file_size;
    
    // Open SPI device
    printf("Opening SPI device %s...\n", SPI_DEVICE);
    spi_fd = spi_open(SPI_DEVICE, 0);  // Use hardware CS control
    if (spi_fd < 0) {
        fprintf(stderr, "Error: Could not open SPI device %s\n", SPI_DEVICE);
        return 1;
    }
    
    // Open binary file
    printf("Opening binary file %s...\n", bin_file);
    fp = fopen(bin_file, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Could not open file %s\n", bin_file);
        close(spi_fd);
        return 1;
    }
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    rewind(fp);
    printf("Binary file size: %ld bytes\n", file_size);
    
    // Allocate memory for file content
    buffer = (uint8_t*)malloc(file_size);
    if (!buffer) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        fclose(fp);
        close(spi_fd);
        return 1;
    }
    
    // Read file content
    if (fread(buffer, 1, file_size, fp) != (size_t)file_size) {
        fprintf(stderr, "Error: Could not read the entire file\n");
        free(buffer);
        fclose(fp);
        close(spi_fd);
        return 1;
    }
    fclose(fp);
    
    // Prepare a dummy byte to assert CS
    uint8_t dummy_byte = 0;
    
    // We'll send a dummy byte first with cs_change=0 to just assert the CS line
    printf("\nAssetting chip select (CS LOW)...\n");
    if (spi_transfer_chunk(spi_fd, &dummy_byte, NULL, 0, 1) < 0) {
        fprintf(stderr, "Error: Failed to assert chip select\n");
        free(buffer);
        close(spi_fd);
        return 1;
    }
    
    // Wait for user to connect power and press Enter
    wait_for_user_confirmation("MANUAL STEP REQUIRED: Connect power to the FPGA now.");
    
    // Write the binary data to the SPI device in chunks
    printf("Writing %ld bytes to SPI device...\n", file_size);
    if (spi_transfer(spi_fd, buffer, NULL, file_size, 0) < 0) {
        fprintf(stderr, "Error: Failed to write data to SPI device\n");
        free(buffer);
        close(spi_fd);
        return 1;
    }
    printf("Successfully wrote %ld bytes from %s to %s\n", file_size, bin_file, SPI_DEVICE);
    
    // Write 49 dummy bytes (0x00)
    printf("Writing 49 dummy bytes...\n");
    uint8_t dummy_bytes[49] = {0};
    if (spi_transfer(spi_fd, dummy_bytes, NULL, 49, 0) < 0) {
        fprintf(stderr, "Error: Failed to write dummy bytes to SPI device\n");
        free(buffer);
        close(spi_fd);
        return 1;
    }
    printf("Successfully wrote 49 dummy bytes to the SPI device\n");
    
    // Clean up
    free(buffer);
    close(spi_fd);
    
    printf("\nProgramming complete! Your FPGA should now be running the loaded bitstream.\n");
    
    return 0;
}