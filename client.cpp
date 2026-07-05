#define _GNU_SOURCE 1
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <vector>
#include <cstring>
#include <assert.h>

#define BUFFER_SIZE 64
#define MAX_EPOLL_EVENTS 1024

int setnonblock(int fd) {
    int oldoption = fcntl(fd, F_GETFL);
    int newoption = oldoption |= O_NONBLOCK;
    fcntl(fd, F_SETFL, newoption);
    return oldoption;
}

void addfd(int epollfd, int fd, bool oneshot = false) {
    epoll_event event;
    event.data.fd = fd;

    if (fd == STDIN_FILENO) {
        event.events = EPOLLIN;
    }
    else {
        event.events = EPOLLIN | EPOLLRDHUP;
    }

    if (oneshot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblock(fd);
}


void chatworkflow(epoll_event* events, int number, int epoollfd, int sockfd) {
    std::vector<char> buf(BUFFER_SIZE, '\0');
    int pipefd[2];
    int ret = pipe(pipefd);
    assert(ret != -1);

    for (int i = 0; i < number; i++) {
        if (events[i].events & EPOLLRDHUP) {
            printf("server close the connection\n");
            break;
        }
        else if(events[i].data.fd == sockfd && events[i].events & EPOLLIN){
            std::fill(buf.begin(), buf.end(), '\0');
            recv(sockfd, buf.data(), BUFFER_SIZE - 1, 0);
            printf("%s\n", buf.data());
        }
        else if (events[i].data.fd == STDIN_FILENO && events[i].events & EPOLLIN) {
            //使用splice将用户输入的数据直接写到sockfd上
            ret = splice(STDIN_FILENO, NULL, pipefd[1], NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
            ret = splice(pipefd[0], NULL, sockfd, NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
        }
        else {
            printf("something went wrong\n");
            break;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc <= 2) {
        printf("usage: %s ip_address port_number\n", argv[0]);
        return 1;
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);
    
    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &server_address.sin_addr);
    server_address.sin_port = htons(port);

    int sockfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(sockfd >= 0);

    if (connect(sockfd, (const struct sockaddr*)&server_address, sizeof(server_address)) < 0) {
        printf("Connection failed\n");
        close(sockfd);
        return 1;
    }

    epoll_event events[MAX_EPOLL_EVENTS];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);
    addfd(epollfd, sockfd, false);
    addfd(epollfd, STDIN_FILENO, false);

    while (1) {
        int ret = epoll_wait(epollfd, events, MAX_EPOLL_EVENTS, -1);
        if (ret < 0) {
            printf("epoll failure\n");
            break;
        }
        chatworkflow(events, ret, epollfd, sockfd);
    }
    close(sockfd);
    return 0;
}