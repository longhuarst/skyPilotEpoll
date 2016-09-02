#include <iostream>
#include "unistd.h"
#include "sys/types.h"
#include "stdint.h"
#include "stdlib.h"
#include "stdio.h"
#include "sys/socket.h"
#include "netinet/in.h"
#include "arpa/inet.h"
#include "errno.h"
#include "sys/epoll.h"
#include "vector"
#include "string.h"
#include "fcntl.h"

int client_cnt = 0;

using namespace std;

#define ERR_EXIT(msg) \
	do \
	{ \
		perror(msg); \
		exit(EXIT_FAILURE); \
	} while (0);

ssize_t readn(int fd, void *buf, size_t count)
{
	size_t nleft = count;
	ssize_t nread;
	char *pbuf = (char *)buf;

	while (nleft > 0) {
		if ((nread = read(fd, pbuf, nleft)) < 0) {
			if (errno == EINTR)
				continue;
			else
				return -1;
		}
		else if (nread == 0) {
			//对等方关闭
			return count - nleft;
		}
		else {
			pbuf += nread;
			nleft -= nread;
		}
	}
	return count;
}


ssize_t writen(int fd, const void *buf, size_t count)
{
	size_t nleft = count;
	ssize_t nwrite;
	char *pbuf = (char *)buf;

	while (nleft > 0) {
		if ((nwrite = write(fd, pbuf, nleft)) < 0) {
			if (errno == EINTR)
				continue;
			else
				return -1;
		}
		else if (nwrite == 0) {
			continue;
		}
		else {
			pbuf += nwrite;
			nleft -= nwrite;
		}
	}
	return count;
}


ssize_t read_peek(int fd, void *buf, size_t count)
{
	int ret;
	while (1) {
		ret = recv(fd, buf, sizeof(buf), MSG_PEEK);
		if (ret == -1 && errno == EINTR)
			continue;
		return ret;
	}
}

ssize_t readline(int fd, void *buf, size_t maxline)
{
	int ret,i;
	char *pbuf = (char *)buf;
	size_t nleft = maxline;
	size_t nread;
	size_t count;
	while (1) {
		ret = read_peek(fd, pbuf, nleft);

		if (ret < 0)
			return ret;
		else if (ret == 0)
			return ret;

		nread = ret;

		for (i = 0; i < nread; ++i) {
			if (pbuf[i] == '\n') {
				ret = readn(fd, pbuf, i + 1);
				if (ret != i + 1)
					ERR_EXIT("read");

				return ret + count;
			}
		}

		if (nread > nleft)
			ERR_EXIT("read");

		nleft -= nread;
		
		ret = readn(fd, pbuf, nread);
		if (ret != nread)
			ERR_EXIT("read");

		pbuf += nread;
		count += nread;
	}

	return -1;
}

void client_service(int fd,int epfd,struct epoll_event *events) 
{
	char buffer[1024];
	ssize_t size;
	
	bzero(buffer,sizeof(buffer));

	size = readline(fd, buffer, 1024);

	if (size < 0) {
		cout << "read error!" << endl;
	}
	else if (size == 0) {
		cout << "client disconnected !" << endl;
		epoll_ctl(epfd, EPOLL_CTL_DEL, fd, events);
		close(fd);
		client_cnt--;
		cout << "clent_cnt = " << client_cnt << endl;
	}
	else {
		if (size < sizeof(buffer))
			buffer[size] = 0;
		cout << "read data from client = " << buffer << endl;
	}


}

void setnonblocking(int fd,bool flag)
{
	int status;
	if (flag == true) {
		fcntl(fd, F_GETFL, &status);
		fcntl(fd, F_SETFL, status | O_NONBLOCK);
	}
	else {
		fcntl(fd, F_GETFL, &status);
		status &= ~O_NONBLOCK;
		fcntl(fd, F_SETFL, status);
	}
}

int main(int argc, char *argv[])
{
	
	int listenfd;

	if ((listenfd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
		ERR_EXIT("socket");

	int on = 1;
	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		ERR_EXIT("setsockopt");

	struct sockaddr_in servaddr;
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(18888);
	if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
		ERR_EXIT("bind");

	if (listen(listenfd, SOMAXCONN) < 0)
		ERR_EXIT("listen");

	struct epoll_event listen_event;
	int epfd;
	if ((epfd = epoll_create1(EPOLL_CLOEXEC)) < 0)
		ERR_EXIT("epoll_create1");

	listen_event.data.fd = listenfd;
	listen_event.events = EPOLLIN;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &listen_event) < 0)
		ERR_EXIT("epoll_ctl");
	
	vector<struct epoll_event> epoll_wait_events(100);

	while (1) {
	
		int ret = epoll_wait(epfd, &*epoll_wait_events.begin(), epoll_wait_events.size(), -1);

		if (ret < 0) {
			if (errno == EINTR)
				continue;
			else
				ERR_EXIT("epoll_wait");
		}
		else if (ret == 0) {
			//此时不可能发生
			cout << "epoll_wait tiemout" << endl;
			continue;
		}else {
			if (ret == epoll_wait_events.size()) {
				epoll_wait_events.resize(epoll_wait_events.size() * 2);
			}
			//处理事件
			int i;
			for (i = 0; i < ret; ++i) {
				if (epoll_wait_events[i].data.fd == listenfd) {
					//连接
					struct sockaddr_in cliaddr;
					bzero(&cliaddr, sizeof(cliaddr));
					socklen_t cliaddr_len = sizeof(cliaddr);
					int clifd;
					if ((clifd = accept(listenfd, (struct sockaddr *)&cliaddr, &cliaddr_len)) < 0)
						ERR_EXIT("accpet");

					setnonblocking(clifd, true);
					client_cnt++;
					cout << "clent_cnt = " << client_cnt << endl;
					struct epoll_event clievent;
					clievent.data.fd = clifd;
					clievent.events = EPOLLIN|EPOLLET;
					if (epoll_ctl(epfd, EPOLL_CTL_ADD, clifd, &clievent) < 0)
						ERR_EXIT("epoll_ctl");



				}
				else {
					//用户交互
					client_service(epoll_wait_events[i].data.fd, epfd,&epoll_wait_events[i]);
				}
			}
			
		}




	}

	






	return 0;
}