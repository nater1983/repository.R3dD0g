/* snapd-activate: minimal systemd-style socket activator for snapd.
 * usage: snapd-activate [-m mode] sock1 [sock2 ...] -- cmd [args...]
 * Binds the unix sockets, waits for a connection, runs cmd with the sockets
 * as fds 3.. (LISTEN_PID/LISTEN_FDS/LISTEN_FDNAMES). When cmd exits with 42
 * (snapd "standby") it goes back to waiting; any other exit restarts it. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t term = 0;
static volatile pid_t child = 0;
static void on_term(int s) { term = 1; if (child > 0) kill(child, s); }

int main(int argc, char **argv) {
    int i = 1, n = 0, fds[16]; mode_t mode = 0666;
    if (i + 1 < argc && !strcmp(argv[i], "-m")) { mode = strtoul(argv[i+1], 0, 8); i += 2; }
    for (; i < argc && strcmp(argv[i], "--"); i++, n++) {
        struct sockaddr_un a = { .sun_family = AF_UNIX };
        if (n >= 16 || strlen(argv[i]) >= sizeof a.sun_path) return 2;
        strcpy(a.sun_path, argv[i]);
        unlink(argv[i]);
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        mode_t old = umask(0);
        if (fd < 0 || bind(fd, (void *)&a, sizeof a) || listen(fd, 128)) { perror(argv[i]); return 1; }
        umask(old);
        chmod(argv[i], mode);
        fds[n] = fd;
    }
    if (i >= argc - 1 || !n) { fprintf(stderr, "usage: %s [-m mode] sock... -- cmd...\n", argv[0]); return 2; }
    char **cmd = &argv[i + 1];

    struct sigaction sa = { .sa_handler = on_term };
    sigaction(SIGTERM, &sa, 0); sigaction(SIGINT, &sa, 0);
    signal(SIGPIPE, SIG_IGN);

    struct pollfd p[16];
    for (i = 0; i < n; i++) p[i] = (struct pollfd){ .fd = fds[i], .events = POLLIN };
    int idle = 0; /* wait for a connection before (re)starting, only after standby */
    while (!term) {
        if (idle) {
            int r = poll(p, n, -1);
            if (r < 0) { if (errno == EINTR) continue; return 1; }
        }
        pid_t pid = fork();
        if (pid < 0) { sleep(1); continue; }
        if (pid == 0) {
            int hi[16];
            for (i = 0; i < n; i++) hi[i] = fcntl(fds[i], F_DUPFD, 32); /* no CLOEXEC */
            for (i = 0; i < n; i++) { dup2(hi[i], 3 + i); close(hi[i]); }
            char b[32]; snprintf(b, sizeof b, "%d", n); setenv("LISTEN_FDS", b, 1);
            snprintf(b, sizeof b, "%d", getpid()); setenv("LISTEN_PID", b, 1);
            char names[256] = ""; /* one name per fd, must end in .socket */
            for (i = 0; i < n; i++) strcat(names, i ? ":snapd.socket" : "snapd.socket");
            setenv("LISTEN_FDNAMES", names, 1);
            execvp(cmd[0], cmd); perror(cmd[0]); _exit(127);
        }
        child = pid;
        int st; while (waitpid(pid, &st, 0) < 0 && errno == EINTR);
        child = 0;
        if (WIFEXITED(st) && WEXITSTATUS(st) == 42) idle = 1;
        else { idle = 0; if (!term) sleep(1); }
    }
    return 0;
}
