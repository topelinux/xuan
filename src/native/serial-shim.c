#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/serial.h>
#endif

#define SERIAL_FLAG_RTSCTS 0x01

static int serial__baud_to_speed(int baud, speed_t *speed) {
    switch (baud) {
        case 50:
            *speed = B50;
            return 0;
        case 75:
            *speed = B75;
            return 0;
        case 110:
            *speed = B110;
            return 0;
        case 134:
            *speed = B134;
            return 0;
        case 150:
            *speed = B150;
            return 0;
        case 200:
            *speed = B200;
            return 0;
        case 300:
            *speed = B300;
            return 0;
        case 600:
            *speed = B600;
            return 0;
        case 1200:
            *speed = B1200;
            return 0;
        case 1800:
            *speed = B1800;
            return 0;
        case 2400:
            *speed = B2400;
            return 0;
        case 4800:
            *speed = B4800;
            return 0;
        case 9600:
            *speed = B9600;
            return 0;
        case 19200:
            *speed = B19200;
            return 0;
        case 38400:
            *speed = B38400;
            return 0;
#ifdef B57600
        case 57600:
            *speed = B57600;
            return 0;
#endif
#ifdef B115200
        case 115200:
            *speed = B115200;
            return 0;
#endif
#ifdef B230400
        case 230400:
            *speed = B230400;
            return 0;
#endif
#ifdef B460800
        case 460800:
            *speed = B460800;
            return 0;
#endif
#ifdef B500000
        case 500000:
            *speed = B500000;
            return 0;
#endif
#ifdef B576000
        case 576000:
            *speed = B576000;
            return 0;
#endif
#ifdef B921600
        case 921600:
            *speed = B921600;
            return 0;
#endif
#ifdef B1000000
        case 1000000:
            *speed = B1000000;
            return 0;
#endif
#ifdef B1152000
        case 1152000:
            *speed = B1152000;
            return 0;
#endif
#ifdef B1500000
        case 1500000:
            *speed = B1500000;
            return 0;
#endif
#ifdef B2000000
        case 2000000:
            *speed = B2000000;
            return 0;
#endif
#ifdef B2500000
        case 2500000:
            *speed = B2500000;
            return 0;
#endif
#ifdef B3000000
        case 3000000:
            *speed = B3000000;
            return 0;
#endif
#ifdef B3500000
        case 3500000:
            *speed = B3500000;
            return 0;
#endif
#ifdef B4000000
        case 4000000:
            *speed = B4000000;
            return 0;
#endif
        default:
            return -EINVAL;
    }
}

static int serial__configure(int fd, int baud, int data_bits, int stop_bits, int parity, int flags) {
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        return -errno;
    }

    cfmakeraw(&tio);

    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~CSIZE;
    switch (data_bits) {
        case 5:
            tio.c_cflag |= CS5;
            break;
        case 6:
            tio.c_cflag |= CS6;
            break;
        case 7:
            tio.c_cflag |= CS7;
            break;
        case 8:
            tio.c_cflag |= CS8;
            break;
        default:
            return -EINVAL;
    }

    if (stop_bits == 2) {
        tio.c_cflag |= CSTOPB;
    } else if (stop_bits == 1) {
        tio.c_cflag &= ~CSTOPB;
    } else {
        return -EINVAL;
    }

    tio.c_cflag &= ~(PARENB | PARODD);
#ifdef CMSPAR
    tio.c_cflag &= ~CMSPAR;
#endif
    switch (parity) {
        case 'n':
        case 'N':
            break;
        case 'e':
        case 'E':
            tio.c_cflag |= PARENB;
            break;
        case 'o':
        case 'O':
            tio.c_cflag |= (PARENB | PARODD);
            break;
        default:
            return -EINVAL;
    }

#ifdef CRTSCTS
    if (flags & SERIAL_FLAG_RTSCTS) {
        tio.c_cflag |= CRTSCTS;
    } else {
        tio.c_cflag &= ~CRTSCTS;
    }
#endif

    // Keep read() semantics simple: once poll says readable, read at least one byte.
    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 0;

    speed_t speed;
    int speed_ret = serial__baud_to_speed(baud, &speed);
    if (speed_ret != 0) {
        return speed_ret;
    }
    if (cfsetispeed(&tio, speed) != 0 || cfsetospeed(&tio, speed) != 0) {
        return -errno;
    }

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        return -errno;
    }
    return 0;
}

int serial_open(const char *path, int baud, int data_bits, int stop_bits, int parity, int flags) {
    int open_flags = O_RDWR | O_NOCTTY;
#ifdef O_CLOEXEC
    open_flags |= O_CLOEXEC;
#endif

    int fd = open(path, open_flags);
    if (fd < 0) {
        return -errno;
    }

    int ret = serial__configure(fd, baud, data_bits, stop_bits, parity, flags);
    if (ret < 0) {
        close(fd);
        return ret;
    }
    return fd;
}

int serial_close(int fd) {
    if (close(fd) != 0) {
        return -errno;
    }
    return 0;
}

int serial_config(int fd, int baud, int data_bits, int stop_bits, int parity, int flags) {
    return serial__configure(fd, baud, data_bits, stop_bits, parity, flags);
}

int serial_drain(int fd) {
    if (tcdrain(fd) != 0) {
        return -errno;
    }
    return 0;
}

int serial_flush(int fd, int queue_selector) {
    int selector = TCIOFLUSH;
    if (queue_selector == 1) {
        selector = TCIFLUSH;
    } else if (queue_selector == 2) {
        selector = TCOFLUSH;
    }
    if (tcflush(fd, selector) != 0) {
        return -errno;
    }
    return 0;
}

int serial_set_rs485(int fd,
                     int enabled,
                     int rts_on_send,
                     int rts_after_send,
                     int delay_before_send_ms,
                     int delay_after_send_ms) {
#if defined(__linux__) && defined(TIOCSRS485)
    struct serial_rs485 rs485;
    memset(&rs485, 0, sizeof(rs485));

    if (enabled) {
        rs485.flags |= SER_RS485_ENABLED;
    }
    if (rts_on_send) {
        rs485.flags |= SER_RS485_RTS_ON_SEND;
    }
    if (rts_after_send) {
        rs485.flags |= SER_RS485_RTS_AFTER_SEND;
    }
    if (delay_before_send_ms > 0) {
        rs485.delay_rts_before_send = (uint32_t) delay_before_send_ms;
    }
    if (delay_after_send_ms > 0) {
        rs485.delay_rts_after_send = (uint32_t) delay_after_send_ms;
    }

    if (ioctl(fd, TIOCSRS485, &rs485) != 0) {
        return -errno;
    }
    return 0;
#else
    (void) fd;
    (void) enabled;
    (void) rts_on_send;
    (void) rts_after_send;
    (void) delay_before_send_ms;
    (void) delay_after_send_ms;
    return -ENOTSUP;
#endif
}

const char *serial_strerror(int error_no) {
    return strerror(error_no);
}
