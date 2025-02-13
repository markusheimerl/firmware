#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <string.h>
#include <linux/gpio.h>
#include <sys/stat.h>
#include <sys/types.h>

#define SPI_DEVICE "/dev/spidev0.0"  // adjust as needed
#define CS_GPIO 8  // adjust this to your CS GPIO pin number
#define BUF_SIZE 4096

// Function to export and set direction of GPIO
int gpio_init(int gpio) {
    FILE *fp;
    char buf[64];

    // Export GPIO
    fp = fopen("/sys/class/gpio/export", "w");
    if (fp == NULL) {
        perror("Error opening export file");
        return -1;
    }
    fprintf(fp, "%d", gpio);
    fclose(fp);

    // Give system time to create the direction file
    usleep(100000);

    // Set direction
    snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%d/direction", gpio);
    fp = fopen(buf, "w");
    if (fp == NULL) {
        perror("Error opening direction file");
        return -1;
    }
    fprintf(fp, "out");
    fclose(fp);

    return 0;
}

// Function to set GPIO value
int gpio_set_value(int gpio, int value) {
    FILE *fp;
    char buf[64];

    snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%d/value", gpio);
    fp = fopen(buf, "w");
    if (fp == NULL) {
        perror("Error opening value file");
        return -1;
    }
    fprintf(fp, "%d", value);
    fclose(fp);

    return 0;
}

// Function to cleanup GPIO
int gpio_cleanup(int gpio) {
    FILE *fp;

    fp = fopen("/sys/class/gpio/unexport", "w");
    if (fp == NULL) {
        perror("Error opening unexport file");
        return -1;
    }
    fprintf(fp, "%d", gpio);
    fclose(fp);

    return 0;
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
    if (gpio_init(CS_GPIO) < 0) {
        return 1;
    }

    // Pull CS low
    if (gpio_set_value(CS_GPIO, 0) < 0) {
        gpio_cleanup(CS_GPIO);
        return 1;
    }

    printf("CS is now pulled low. Press Enter to continue with programming...");
    getchar();

    // Open the binary file
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) {
        perror("Error opening binary file");
        gpio_cleanup(CS_GPIO);
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
        gpio_cleanup(CS_GPIO);
        return 1;
    }

    // Read file into buffer
    if (fread(data, 1, file_size, fp) != file_size) {
        perror("Error reading file");
        free(data);
        fclose(fp);
        gpio_cleanup(CS_GPIO);
        return 1;
    }
    fclose(fp);

    // Initialize SPI
    int spi_fd = spi_init(SPI_DEVICE);
    if (spi_fd < 0) {
        free(data);
        gpio_cleanup(CS_GPIO);
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
            gpio_cleanup(CS_GPIO);
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
        gpio_cleanup(CS_GPIO);
        return 1;
    }

    printf("Successfully wrote 49 dummy bytes\n");

    // Pull CS high
    if (gpio_set_value(CS_GPIO, 1) < 0) {
        close(spi_fd);
        free(data);
        gpio_cleanup(CS_GPIO);
        return 1;
    }

    // Cleanup
    close(spi_fd);
    free(data);
    gpio_cleanup(CS_GPIO);
    return 0;
}