#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <string.h>
#include <errno.h>
#include <gpiod.h>

#define SPI_DEVICE "/dev/spidev0.0"  // adjust as needed
#define CS_GPIO 8  // adjust this to your CS GPIO pin number
#define GPIO_CHIP "gpiochip0"
#define BUF_SIZE 4096

struct gpiod_chip *chip;
struct gpiod_line *cs_line;

int gpio_init(void) {
    // Open GPIO chip
    chip = gpiod_chip_open_by_name(GPIO_CHIP);
    if (!chip) {
        perror("Failed to open GPIO chip");
        return -1;
    }

    // Get GPIO line
    cs_line = gpiod_chip_get_line(chip, CS_GPIO);
    if (!cs_line) {
        perror("Failed to get GPIO line");
        gpiod_chip_close(chip);
        return -1;
    }

    // Request GPIO line as output
    if (gpiod_line_request_output(cs_line, "FPGA_CS", 1) < 0) {
        perror("Failed to request GPIO line as output");
        gpiod_chip_close(chip);
        return -1;
    }

    return 0;
}

int gpio_set_value(int value) {
    return gpiod_line_set_value(cs_line, value);
}

void gpio_cleanup(void) {
    if (cs_line) {
        gpiod_line_release(cs_line);
    }
    if (chip) {
        gpiod_chip_close(chip);
    }
}

int spi_init(const char *device) {
    int fd;
    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = 1000000;  // 1MHz

    fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("Error opening SPI device");
        return -1;
    }

    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0) {
        perror("Error setting SPI mode");
        close(fd);
        return -1;
    }

    if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
        perror("Error setting bits per word");
        close(fd);
        return -1;
    }

    if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        perror("Error setting speed");
        close(fd);
        return -1;
    }

    return fd;
}

int spi_transfer(int fd, uint8_t *tx_buf, uint8_t *rx_buf, size_t len) {
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx_buf,
        .rx_buf = (unsigned long)rx_buf,
        .len = len,
        .delay_usecs = 0,
        .speed_hz = 1000000,
        .bits_per_word = 8,
    };

    return ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <binary_file>\n", argv[0]);
        return 1;
    }

    // Initialize GPIO for chip select
    if (gpio_init() < 0) {
        fprintf(stderr, "Failed to initialize GPIO\n");
        return 1;
    }

    // Pull CS low
    if (gpio_set_value(0) < 0) {
        fprintf(stderr, "Failed to set GPIO low\n");
        gpio_cleanup();
        return 1;
    }

    printf("CS is now pulled low. Press Enter to continue with programming...");
    getchar();

    // Open the binary file
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) {
        perror("Error opening binary file");
        gpio_cleanup();
        return 1;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Allocate buffer for file data
    uint8_t *data = malloc(file_size);
    if (!data) {
        perror("Error allocating memory");
        fclose(fp);
        gpio_cleanup();
        return 1;
    }

    // Read file into buffer
    if (fread(data, 1, file_size, fp) != file_size) {
        perror("Error reading file");
        free(data);
        fclose(fp);
        gpio_cleanup();
        return 1;
    }
    fclose(fp);

    // Initialize SPI
    int spi_fd = spi_init(SPI_DEVICE);
    if (spi_fd < 0) {
        free(data);
        gpio_cleanup();
        return 1;
    }

    // Write binary data
    size_t bytes_written = 0;
    while (bytes_written < file_size) {
        size_t chunk_size = (file_size - bytes_written) > BUF_SIZE ? 
                           BUF_SIZE : (file_size - bytes_written);
        
        if (spi_transfer(spi_fd, &data[bytes_written], NULL, chunk_size) < 0) {
            perror("Error writing data");
            close(spi_fd);
            free(data);
            gpio_cleanup();
            return 1;
        }
        bytes_written += chunk_size;
    }

    printf("Successfully wrote %ld bytes from %s\n", file_size, argv[1]);

    // Write 49 dummy bytes
    uint8_t dummy[49] = {0};
    if (spi_transfer(spi_fd, dummy, NULL, 49) < 0) {
        perror("Error writing dummy bytes");
        close(spi_fd);
        free(data);
        gpio_cleanup();
        return 1;
    }

    printf("Successfully wrote 49 dummy bytes\n");

    // Pull CS high
    if (gpio_set_value(1) < 0) {
        close(spi_fd);
        free(data);
        gpio_cleanup();
        return 1;
    }

    // Cleanup
    close(spi_fd);
    free(data);
    gpio_cleanup();
    return 0;
}