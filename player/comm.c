#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
static int sfd;
int comm_init(const char *sockPath) {
    struct sockaddr_un server_sockaddr;
    sfd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sfd < 0) {
        perror("BIND UNIX SOCKET\n");
        return -1;
    }
    server_sockaddr.sun_family = AF_UNIX;   
    strcpy(server_sockaddr.sun_path, sockPath);
    unlink(sockPath);
    int rc = bind(sfd, (struct sockaddr *) &server_sockaddr, sizeof(server_sockaddr));
    if (rc == -1){
        perror("BIND UNIX SOCKET");
        close(sfd);
        return -1;
    }
}
char comm_buf[1024*512];
char *comm_read() {
    int res = recv(sfd, comm_buf, sizeof(comm_buf) - 1, MSG_DONTWAIT);
    if (res < 0 && errno != EAGAIN) {
        perror("COMM READ");
    }
    if (res >= 0) {
        comm_buf[res] = 0;
    }
    return res < 0 ? 0 : comm_buf;
}
void comm_close() {
    close(sfd);
}
