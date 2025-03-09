#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <linux/gpio.h>

#define SPI_DEVICE "/dev/spidev0.0"  // Default SPI device on Raspberry Pi
#define SPI_SPEED 1000000            // 1 MHz
#define SPI_BITS_PER_WORD 8
#define SPI_MODE SPI_MODE_0          // Mode 0: CPOL=0, CPHA=0
#define MAX_TRANSFER_SIZE 4096       // Maximum size for a single transfer

#define GPIO_CHIP "/dev/gpiochip0"   // Default GPIO chip on Raspberry Pi
#define CS_PIN 25                    // GPIO pin number for manual chip select (changed to 25)

int spi_open(const char *device) {
    int fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("Failed to open SPI device");
        return -1;
    }

    uint8_t mode = SPI_MODE;
    uint8_t bits = SPI_BITS_PER_WORD;
    uint32_t speed = SPI_SPEED;

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

// Setup GPIO for chip select
int gpio_setup(int *cs_fd) {
    // Use standard file operations with /dev/gpiochip0
    *cs_fd = open(GPIO_CHIP, O_RDWR);
    if (*cs_fd < 0) {
        perror("Failed to open GPIO device");
        return -1;
    }
    
    // Request line
    struct gpiohandle_request req;
    memset(&req, 0, sizeof(req));
    
    req.flags = GPIOHANDLE_REQUEST_OUTPUT;
    req.lines = 1;
    req.lineoffsets[0] = CS_PIN;
    strcpy(req.consumer_label, "SPI_CS");
    req.default_values[0] = 1;  // Set high initially (inactive)
    
    if (ioctl(*cs_fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        perror("Failed to get GPIO line handle");
        close(*cs_fd);
        return -1;
    }
    
    // Close the GPIO device file descriptor and replace with the line handle
    close(*cs_fd);
    *cs_fd = req.fd;
    
    return 0;
}

// Set the CS GPIO high (inactive) or low (active)
int gpio_set_cs(int cs_fd, int active) {
    struct gpiohandle_data data;
    memset(&data, 0, sizeof(data));
    
    // active = 0 means set CS high (inactive)
    // active = 1 means set CS low (active)
    data.values[0] = active ? 0 : 1;
    
    if (ioctl(cs_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data) < 0) {
        perror("Failed to set GPIO line value");
        return -1;
    }
    
    return 0;
}

// Manually control SPI transfer
int spi_transfer_chunk(int fd, const uint8_t *tx_buffer, uint8_t *rx_buffer, size_t len) {
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx_buffer,
        .rx_buf = (unsigned long)rx_buffer,
        .len = len,
        .speed_hz = SPI_SPEED,
        .delay_usecs = 0,
        .bits_per_word = SPI_BITS_PER_WORD,
        .cs_change = 0,  // Don't toggle CS between transfers
    };

    int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret < 0) {
        perror("Failed to perform SPI transfer");
        return -1;
    }

    return ret;
}

int spi_transfer(int fd, const uint8_t *tx_buffer, uint8_t *rx_buffer, size_t len) {
    size_t remaining = len;
    size_t offset = 0;
    int ret;

    while (remaining > 0) {
        size_t chunk_size = (remaining > MAX_TRANSFER_SIZE) ? MAX_TRANSFER_SIZE : remaining;
        
        ret = spi_transfer_chunk(fd, tx_buffer + offset, 
                                rx_buffer ? rx_buffer + offset : NULL, 
                                chunk_size);
        
        if (ret < 0) {
            return -1;
        }
        
        remaining -= chunk_size;
        offset += chunk_size;
    }
    
    return len;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        return 1;
    }

    const char *bin_file = argv[1];
    
    int spi_fd, cs_fd;
    FILE *fp;
    uint8_t *buffer;
    long file_size;
    char input[10];
    
    // Setup GPIO for chip select
    printf("Setting up GPIO pin %d for manual chip select...\n", CS_PIN);
    if (gpio_setup(&cs_fd) < 0) {
        fprintf(stderr, "Error: Could not setup GPIO for chip select\n");
        fprintf(stderr, "Make sure GPIO %d is not in use by another process\n", CS_PIN);
        fprintf(stderr, "You may need to change the CS_PIN definition in the code\n");
        return 1;
    }
    
    // Ensure chip select is inactive initially
    gpio_set_cs(cs_fd, 0);
    printf("Chip select initialized (inactive/high)\n");
    
    // Open SPI device
    printf("Opening SPI device %s...\n", SPI_DEVICE);
    spi_fd = spi_open(SPI_DEVICE);
    if (spi_fd < 0) {
        fprintf(stderr, "Error: Could not open SPI device %s\n", SPI_DEVICE);
        close(cs_fd);
        return 1;
    }
    
    // Open binary file
    printf("Opening binary file %s...\n", bin_file);
    fp = fopen(bin_file, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Could not open file %s\n", bin_file);
        close(spi_fd);
        close(cs_fd);
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
        close(cs_fd);
        return 1;
    }
    
    // Read file content
    if (fread(buffer, 1, file_size, fp) != (size_t)file_size) {
        fprintf(stderr, "Error: Could not read the entire file\n");
        free(buffer);
        fclose(fp);
        close(spi_fd);
        close(cs_fd);
        return 1;
    }
    fclose(fp);
    
    // Activate chip select
    printf("\nActivating chip select (GPIO %d -> LOW)...\n", CS_PIN);
    gpio_set_cs(cs_fd, 1);
    
    // Wait for user to connect power and press Enter
    printf("\n========================================================\n");
    printf("MANUAL STEP REQUIRED: Connect power to the FPGA now.\n");
    printf("After connecting power, press Enter to start programming.");
    printf("\n========================================================\n");
    fgets(input, sizeof(input), stdin);
    
    // Write the binary data to the SPI device in chunks
    printf("Writing %ld bytes to SPI device...\n", file_size);
    if (spi_transfer(spi_fd, buffer, NULL, file_size) < 0) {
        fprintf(stderr, "Error: Failed to write data to SPI device\n");
        gpio_set_cs(cs_fd, 0);  // Ensure CS is deactivated
        free(buffer);
        close(spi_fd);
        close(cs_fd);
        return 1;
    }
    printf("Successfully wrote %ld bytes from %s to %s\n", file_size, bin_file, SPI_DEVICE);
    
    // Deactivate chip select
    gpio_set_cs(cs_fd, 0);
    printf("Deactivated chip select (GPIO %d -> HIGH)\n", CS_PIN);
    
    // Write 49 dummy bytes (0x00)
    printf("Writing 49 dummy bytes...\n");
    uint8_t dummy_bytes[49] = {0};
    if (spi_transfer(spi_fd, dummy_bytes, NULL, 49) < 0) {
        fprintf(stderr, "Error: Failed to write dummy bytes to SPI device\n");
        free(buffer);
        close(spi_fd);
        close(cs_fd);
        return 1;
    }
    printf("Successfully wrote 49 dummy bytes to the SPI device\n");
    
    // Clean up
    free(buffer);
    close(spi_fd);
    close(cs_fd);
    
    printf("\nProgramming complete! Your FPGA should now be running the loaded bitstream.\n");
    
    return 0;
}