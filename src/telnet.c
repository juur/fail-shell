#define _XOPEN_SOURCE 700

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <netdb.h>
#include <err.h>
#include <sys/select.h>
#include <termios.h>
#include <arpa/telnet.h>
#include <sys/ioctl.h>

static bool  opt_8bit_data_path = false;
static char  opt_escape_char    = '\e';
static bool  opt_auto_login     = false;
static char *opt_user           = NULL;
static char *opt_host           = NULL;
static int   opt_port           = 0;
static bool  opt_debug          = false;

static const unsigned short default_port = 23;

static int remote_fd = -1;
static struct termios tios_save;
static bool tios_saved = false;

static const char *const telnet_commands[] = {
    [WILL] = "WILL",
    [WONT] = "WONT",
    [DO]   = "DO",
    [DONT] = "DONT",
    [DM]   = "DM",
    [SB]   = "SB",
    [255]  = NULL
};

static const char *const telnet_options[] = {
    [TELOPT_LOGOUT] = "TELOPT_LOGOUT",
    [TELOPT_DET] = "TELOPT_DET",
    [TELOPT_SNDLOC] = "TELOPT_SNDLOC",
    [TELOPT_OUTMRK] = "TELOPT_OUTMRK",
    [TELOPT_TTYPE] = "TELOPT_TTYPE",
    [TELOPT_TSPEED] = "TELOPT_TSPEED",
    [TELOPT_XDISPLOC] = "TELOPT_XDISPLOC",
    [TELOPT_NEW_ENVIRON] = "TELOPT_NEW_ENVIRON",
    [TELOPT_SGA] = "TELOPT_SGA",
    [TELOPT_ECHO] = "TELOPT_ECHO",
    [TELOPT_NAWS] = "TELOPT_NAWS",
    [TELOPT_STATUS] = "TELOPT_STATUS",
    [TELOPT_LFLOW] = "TELOPT_LFLOW",
    [255]  = NULL
};

static int baud_lookup[] = {
    [B0]       = 0,
    [B50]      = 50,
    [B75]      = 75,
    [B110]     = 110,
    [B134]     = 134,
    [B150]     = 150,
    [B200]     = 200,
    [B300]     = 300,
    [B600]     = 600,
    [B1200]    = 1200,
    [B1800]    = 1800,
    [B2400]    = 2400,
    [B4800]    = 4800,
    [B9600]    = 9600,
    [B19200]   = 19200,
    [B38400]   = 38400,
    [B57600]   = 57600,
    [B115200]  = 115200,
    [B230400]  = 230400,
    [B460800]  = 460800,
    [B500000]  = 500000,
    [B576000]  = 576000,
    [B921600]  = 921600,
    [B1000000] = 1000000,
    [B1152000] = 1152000,
    [B1500000] = 1500000,
    [B2000000] = 2000000,
    [B2500000] = 2500000,
    [B3000000] = 3000000,
    [B3500000] = 3500000,
    [B4000000] = 4000000
};

enum {
    STATE_DO   = (1 << 0),
    STATE_WILL = (1 << 1)
};

static bool telopt_state[] = {
    [TELOPT_STATUS] = STATE_WILL,
    [TELOPT_SGA]    = STATE_WILL,
    [TELOPT_TTYPE]  = STATE_WILL,
    [TELOPT_TSPEED] = STATE_WILL,
    [TELOPT_ECHO]   = STATE_WILL|STATE_DO,

    [255] = 0,
};

static void echo_on(int local_fd)
{
    struct termios tios;

    if (!isatty(local_fd))
        return;

    if (tcgetattr(local_fd, &tios) == -1)
        return;

    tios.c_lflag |= ECHO;

    tcsetattr(local_fd, TCSANOW, &tios);
}

static void echo_off(int local_fd)
{
    struct termios tios;

    if (!isatty(local_fd))
        return;

    if (tcgetattr(STDIN_FILENO, &tios) == -1)
        return;

    tios.c_lflag &= ~ECHO;

    tcsetattr(STDIN_FILENO, TCSANOW, &tios);
}

__attribute__((nonnull))
static void get_window_size(int local_fd, unsigned short *x, unsigned short *y)
{
    struct winsize winsize;

    if (isatty(local_fd) && ioctl(local_fd, TIOCGWINSZ, &winsize) != -1) {
        *x = winsize.ws_col;
        *y = winsize.ws_row;
    } else if (getenv("COLUMNS") && getenv("LINES")) {
        *y = atoi(getenv("COLUMNS"));
        *x = atoi(getenv("LINES"));
    } else {
        *x = 80;
        *y = 24;
    }
}

static void show_version(void)
{
    printf("fail-telnet 0.1.0\n");
}

static void show_usage(void)
{
    printf("Usage: telnet [-8EKLadhv] [-e ESCAPECHAR] [-l USER] HOST [PORT]\n"
            "Connect via TELNET protocol to HOST, optionally with non-standard PORT\n\n"
            "  -8       8bit data path (input and output)\n"
            "  -E       disable escape\n"
            "  -K       no auto-login\n"
            "  -L       8bit data path (output)\n"
            "  -a       auto-login\n"
            "  -d       debug\n"
            "  -e E     set escape to E\n"
            "  -l USER  enable auto-login as USER\n"

            "  -h       show help\n"
            "  -v       show version\n"
          );
}

    __attribute__((nonnull))
static inline void clean_socket(int *sock)
{
    shutdown(*sock, SHUT_RDWR);
    close(*sock);
    *sock = -1;
}

static void cleanup(void)
{
    if (tios_saved)
        tcsetattr(STDIN_FILENO, TCSANOW, &tios_save);

    if (opt_host)
        free(opt_host);

    if (remote_fd)
        clean_socket(&remote_fd);
}

__attribute__((nonnull))
static char parse_esc_char(const char *str __attribute__((unused)))
{
    return -1;
}

static int get_next_n(int fd, unsigned char *ptr, int len, int *inc)
{
    int rc;

    if ((rc = read(fd, ptr, len)) == -1)
        exit(EXIT_FAILURE);
    else if (rc == 0)
        exit(EXIT_SUCCESS);
    else {
        if (inc)
            inc += rc;
        return rc;
    }
}

__attribute__((nonnull))
static int process_command(int fd, const unsigned char *raw, ssize_t raw_len)
{
    int prefix;
    unsigned char *send;
    int i = 0;
    unsigned char buf[BUFSIZ];
    int len = raw_len;

    //if (opt_debug)
    //    printf("DEBUG: process_command: fd=%d raw=%p raw_len=%ld\n",
    //            fd, raw, raw_len);

    memcpy(buf, raw, len);

    if (len < 0)
        return -1;

    if (len == 0)
        get_next_n(fd, buf, 1, &len);

    //if (opt_debug)
    //    printf("DEBUG: IAC %02d [%s]\n", buf[i], telnet_commands[buf[i]]);

    switch(buf[i]) {
        /* Subnegotiation */
        case SB:
            {
                prefix = buf[i];

                if (len == 1)
                    i += get_next_n(fd, buf + 2, 1, &len);

                switch(buf[++i])
                {
                    case TELOPT_STATUS:
                        {
                            if (buf[++i] != 1 || /* SEND */
                                    buf[++i] != IAC ||
                                    buf[++i] != SE)
                                exit(EXIT_FAILURE);
                            if (opt_debug)
                                printf("DEBUG: received SB TELOPT_STATUS SEND IAC SE\n");
                            len = 0;
                            if ((send = malloc(5 + 256)) == NULL)
                                err(EXIT_FAILURE, "malloc");

                            send[len++] = IAC;
                            send[len++] = SB;
                            send[len++] = TELOPT_STATUS;
                            send[len++] = 0;

                            for (int i = 0; i < 256; i++)
                                if (telopt_state[i]) {
                                    printf("DEBUG: ");
                                    if (telopt_state[i] & STATE_WILL) {
                                        send[len++] = WILL;
                                        send[len++] = i;
                                        printf("WILL ");
                                    }
                                    if (telopt_state[i] & STATE_DO) {
                                        send[len++] = DO;
                                        send[len++] = i;
                                        printf("DO ");
                                    }
                                    printf("%s\n", telnet_options[i]);
                                }

                            send[len++] = IAC;
                            send[len]   = SE;

                            write(fd, send, len);
                            free(send);
                        }
                        break;
                    case TELOPT_TTYPE:
                        {
                        if (buf[++i] != 1 || /* SEND */
                                buf[++i] != IAC ||
                                buf[++i] != SE)
                            exit(EXIT_FAILURE);
                        if (opt_debug)
                            printf("DEBUG: received SB TELOPT_TTYPE SEND IAC SE\n");
                        int tmp = 6 + strlen(getenv("TERM"));
                        send = malloc(6 + tmp + 1);
                        snprintf((char *)send, tmp + 1, "%c%c%c%c%s%c%c", IAC, SB, TELOPT_TTYPE, 0, getenv("TERM"), IAC, SE);
                        if (opt_debug)
                            printf("DEBUG: sending IAC SB TELOPT_TTYPE IS %s IAC SE\n", getenv("TERM"));
                        write(fd, send, tmp);
                        free(send);
                        }
                        break;

                    case TELOPT_TSPEED:
                        {
                        if (buf[++i] != 1 || /* SEND */
                                buf[++i] != IAC ||
                                buf[++i] != SE)
                            exit(EXIT_FAILURE);
                        if (opt_debug)
                            printf("DEBUG: received SB TELOPT_TSPEED SEND IAC SE\n");
                        char tspeed_buf[64];
                        int tmp = snprintf(tspeed_buf, sizeof(tspeed_buf), 
                                "%c%c%c%c%u,%u%c%c", 
                                IAC, SB, TELOPT_TSPEED, 0,
                                baud_lookup[cfgetispeed(&tios_save)],
                                baud_lookup[cfgetospeed(&tios_save)],
                                IAC, SE);
                        if (opt_debug)
                            printf("DEBUG: sending IAC SB TELOPT_TSPEED IS %u,%u IAC SE [%d bytes]\n",
                                    baud_lookup[cfgetispeed(&tios_save)], 
                                    baud_lookup[cfgetospeed(&tios_save)],
                                    tmp);
                        write(fd, tspeed_buf, tmp);
                        }
                        break;

                    default:
                        if (opt_debug)
                            printf("DEBUG: unknown SB %02d [%s]\n", buf[i], telnet_options[buf[i]]);
                }
            }
            break;
        /* Data Mark */
        case DM:
            break;
        /* Option Processing */
        case WILL:
        case WONT:
        case DO:
        case DONT:
            {
                prefix = buf[i];
                if (opt_debug)
                    printf("DEBUG: received command %02d [%s %s]\n", buf[i], 
                            telnet_commands[buf[i]], telnet_options[buf[i+1]]);

                if (len == 1)
                    get_next_n(fd, buf + 2, 1, &len);

                switch(buf[++i]) {
                    case TELOPT_STATUS:
                    case TELOPT_SGA:
                    case TELOPT_TTYPE:
                    case TELOPT_NAWS:
                    case TELOPT_TSPEED:
                        if (prefix == DO) {
                            send = (unsigned char[]){IAC, WILL, buf[i]};
                            telopt_state[buf[i]] |= STATE_DO;
                            if (opt_debug)
                                printf("DEBUG: sending IAC WILL %s\n", telnet_options[buf[i]]);
                            write(fd, send, 3);
                            switch(buf[i]) {
                                case TELOPT_NAWS:
                                    {
                                        unsigned short x, y;
                                    get_window_size(STDIN_FILENO, &x, &y);
                                    send = (unsigned char[]){IAC, SB, TELOPT_NAWS, 
                                        ((x & 0xff00) >> 8), 
                                        ((x & 0x00ff)), 
                                        ((y & 0xff00) >> 8), 
                                        ((y & 0x00ff)),
                                        IAC, SE};
                                    if (opt_debug)
                                        printf("DEBUG: sending IAC SB TELOPT_NAWS %u %u %u %u IAC SE\n",
                                                send[3], send[4], send[5], send[6]);
                                    write(fd, send, 9);
                                    }
                                    break;
                            }
                        } else if (prefix == WILL) {
                            send = (unsigned char[]){IAC, DO, buf[i]};
                            if (opt_debug)
                                printf("DEBUG: sending IAC DO %s\n", telnet_options[buf[i]]);
                            write(fd, send, 3);

                            send = (unsigned char[]){IAC, WILL, buf[i]};
                            if (opt_debug)
                                printf("DEBUG: sending IAC WILL %s\n", telnet_options[buf[i]]);
                            write(fd, send, 3);
                        } else if (prefix == DONT) {
                            send = (unsigned char[]){IAC, WONT, buf[i]};
                            telopt_state[buf[i]] &= ~STATE_DO;
                            if (opt_debug)
                                printf("DEBUG: sending IAC WONT %s\n", telnet_options[buf[i]]);
                            write(fd, send, 3);
                        } else
                            goto noopt;
                        break;

                    case TELOPT_ECHO:
                        if (prefix == DO) {
                            echo_on(STDIN_FILENO);
                            telopt_state[buf[i]] |= STATE_DO;
                        } else if (prefix == DONT) {
                            echo_off(STDIN_FILENO);
                            telopt_state[buf[i]] &= ~STATE_DO;
                        }
                        send = (unsigned char []){IAC, prefix == DO ? WILL : WONT, TELOPT_ECHO};
                        write(fd, send, 3);
                        break;

                    default:
noopt:
                        if (opt_debug)
                            printf("DEBUG: unknown telnet option: %02d [%s %s]: sending IAC %s\n", 
                                    buf[i],
                                    telnet_commands[prefix],
                                    telnet_options[buf[i]],
                                    prefix == DO ? "WONT" : "DONT"
                                  );
                        send = (unsigned char []){IAC, prefix == DO ? WONT : DONT, buf[i]};
                        write(fd, send, 3);
                        break;
                }
            }
            break;
        /* No operation */
        case NOP:
            break;
        default:
            if (opt_debug)
                printf("DEBUG unknown IAC: %02d [%s]\n", buf[i], telnet_commands[buf[i]]);
            break;
    }
    //if (opt_debug)
    //    printf("DEBUG: process_command: processed=%d\n", i);

    return i;
}


static void do_telnet(const char *host, int port)
{
    struct addrinfo hints = {0};
    int rc;
    struct addrinfo *result = NULL;
    struct termios tios;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (isatty(STDIN_FILENO)) {
        if (tcgetattr(STDIN_FILENO, &tios_save) == -1)
            err(EXIT_FAILURE, "tcgetattr");
        tios_saved = true;

        memcpy(&tios, &tios_save, sizeof(struct termios));

        tios.c_lflag &= ~(ICANON);

        if (tcsetattr(STDIN_FILENO, TCSANOW, &tios) == -1)
            err(EXIT_FAILURE, "tcsetattr");
    }

    if ((rc = getaddrinfo(host, NULL, &hints, &result)) != 0) {
        if (rc == EAI_SYSTEM)
            err(EXIT_FAILURE, "getaddrinfo");
        else
            errx(EXIT_FAILURE, "getaddrinfo: %s", gai_strerror(rc));
    }

    if ((remote_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        err(EXIT_FAILURE, "socket");

    rc = 1;
    if (setsockopt(remote_fd, IPPROTO_TCP, TCP_NODELAY, &rc, sizeof(rc)) == -1)
        warn("setsockopt(TCP_NODELAY)");

    for (struct addrinfo *rp = result; rp; rp = rp->ai_next)
    {
        if (rp->ai_addrlen < sizeof(struct sockaddr_in))
            continue;

        ((struct sockaddr_in *)rp->ai_addr)->sin_port = htons(port);

        if (opt_debug)
            printf("DEBUG: connect(%08x:%04x)\n", 
                    ntohl(((struct sockaddr_in *)rp->ai_addr)->sin_addr.s_addr),
                    htons(((struct sockaddr_in *)rp->ai_addr)->sin_port));


        if (connect(remote_fd, rp->ai_addr, rp->ai_addrlen) == -1) {
            shutdown(remote_fd, SHUT_RDWR);
            warn("connect");
            continue;
        }
    }

    if (result)
        freeaddrinfo(result);

    if (remote_fd == -1)
        errx(EXIT_SUCCESS, "Unable to connect");

    if (opt_debug)
        printf("DEBUG: connected OK on fd %d\n", remote_fd);

    bool running = true;

    fd_set fds_in, fds_err;

    unsigned char buf[BUFSIZ];
    ssize_t bytes_read;

    while (running)
    {
        FD_ZERO(&fds_in);
        FD_ZERO(&fds_err);

        FD_SET(remote_fd, &fds_in);
        FD_SET(STDIN_FILENO, &fds_in);
        FD_SET(remote_fd, &fds_err);

        if (opt_debug)
            printf("DEBUG: sleeping via select()\n");

        rc = select(remote_fd + 1, &fds_in, NULL, &fds_err, NULL);

        if (rc == -1)
            err(EXIT_FAILURE, "select");

        if (FD_ISSET(remote_fd, &fds_err)) {
            if (opt_debug)
                printf("DEBUG: fd is in fds_err\n");
            exit(EXIT_FAILURE);
        }

        if (FD_ISSET(STDIN_FILENO, &fds_in)) {
            rc = read(STDIN_FILENO, buf, sizeof(buf));
            if (rc) {
                rc = write(remote_fd, buf, rc);
                if (rc == -1)
                    err(EXIT_FAILURE, "write");
            }
        }

        if (FD_ISSET(remote_fd, &fds_in)) {
            if (opt_debug)
                printf("DEBUG: fd is in fds_in\n");

            rc = bytes_read = read(remote_fd, buf, sizeof(buf));

            if (rc == -1)
                err(EXIT_FAILURE, "read");

            if (opt_debug)
                printf("DEBUG: read %lu bytes from fd\n", bytes_read);

            if (rc == 0)
                exit(EXIT_SUCCESS);

            for (int i = 0; i < bytes_read; i++) 
            {
                if (buf[i] == IAC) {
                    i++;
                    if (buf[i] == IAC)
                        goto force_print;
                    rc = process_command(remote_fd, &buf[i], bytes_read - i);
                    if (rc == -1)
                        exit(EXIT_FAILURE);
                    i += rc;
                } else {
force_print:
                    write(STDOUT_FILENO, &buf[i], 1);
                }
            }
        }
    }
}

int main(int argc, char *argv[])
{
    {
        int opt = 0;

        while ((opt = getopt(argc, argv, "8EKLadhve:l:")) != -1)
        {
            switch (opt)
            {
                case '8': opt_8bit_data_path = true;  break;
                case 'E': opt_escape_char    = -1;    break;
                case 'K': opt_auto_login     = false; break;
                case 'a': opt_auto_login     = true;  break;
                case 'd': opt_debug          = true;  break;

                case 'h':
                          show_usage();
                          exit(EXIT_SUCCESS);
                          break;

                case 'v':
                          show_version();
                          exit(EXIT_SUCCESS);
                          break;

                case 'e': opt_escape_char    = parse_esc_char(optarg); break;
                case 'l': opt_user           = strdup(optarg);
                          opt_auto_login     = true;
                          break;

                default:  goto fail;                  break;
            }
        }

        if (optind >= argc) {
            goto fail;
        }

        atexit(cleanup);

        opt_host = strdup(argv[optind++]);

        if (optind < argc) {
            /* TODO proper check */
            opt_port = atoi(argv[optind++]);
            if (opt_port == 0 || optind < argc)
                goto fail;
        }

        if (opt_port == 0) {
            struct servent *ent;

            if ((ent = getservbyname("telnet", "tcp")) != NULL)
                opt_port = ntohs(ent->s_port);
            else
                opt_port = default_port;
        }

    }

    if (opt_debug)
        printf("DEBUG: connecting to %s on port %d\n", opt_host, opt_port);

    do_telnet(opt_host, opt_port);

    exit(EXIT_SUCCESS);

fail:
    show_usage();
    exit(EXIT_FAILURE);
}
