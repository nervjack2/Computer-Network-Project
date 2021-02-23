#include <string>
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include "opencv2/opencv.hpp"

using namespace std;
using namespace cv;

#define max_fd 1024
#define buff_size 1024
void srecv(int fd, char *buf, int size){
	int cur = 0;
	while(cur < size){
		int res = recv(fd, buf + cur, size - cur * sizeof(char), 0);
		cur += res;
	}
}
void ssend(int fd, char *buf, int size){
	int cur = 0;
	while(cur < size){
		int res = send(fd, buf + cur, size - cur * sizeof(char), 0);
		cur += res;
	}
}

enum State{HANG=0, FINS=1, PUT=2, GET=3, PLAY=4};

typedef struct{
	enum State state;
	int size;
	FILE *fp;
	Mat imgServer;
	VideoCapture cap;
}Client;

int getFileSize(FILE *fp){
	fseek(fp,0L,SEEK_END);
	int size = ftell(fp);
	fseek(fp,0L,SEEK_SET);
	return size;
}

char buf[40000000] = "";
Client client[buff_size];

int main(int argc, char *argv[]){
	mkdir("server_folder", 0777);
	chdir("server_folder");
	// initialize server
	int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in sockaddr;
	int addrlen = sizeof(sockaddr);
	memset(&sockaddr, 0, sizeof(sockaddr));
	sockaddr.sin_family = AF_INET;
	sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	sockaddr.sin_port = htons(atoi(argv[1]));
	int opt = 1;
	if(setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&opt, sizeof(opt)) < 0){
		perror("setsockopt");
		exit(1);
	}
	if(bind(listen_fd, (struct sockaddr*)&sockaddr, sizeof(sockaddr)) < 0){
		perror("bind");
		exit(1);	
	}
	if(listen(listen_fd, max_fd) < 0){
		perror("listen");
		exit(1);
	}

	printf("Start to linsten at port %d\n", atoi(argv[1]));	

	struct sockaddr_in caddr;
	fd_set rset, wset, rcur, wcur;
	FD_ZERO(&rset);
	FD_ZERO(&wset);
	FD_ZERO(&rcur);
	FD_ZERO(&wcur);
	FD_SET(listen_fd, &rcur);
	
	while(1){
		memcpy(&rset, &rcur, sizeof(rcur));
		memcpy(&wset, &wcur, sizeof(wcur));
		puts("Selecting a client...");
		select(max_fd, &rset, &wset, NULL, NULL);
		for(int i = 3; i < max_fd; i++){
			int sd = i;
			if(FD_ISSET(sd, &rset)){
				if(sd == listen_fd){
					int addr_sz = sizeof(caddr);
					int new_socket = accept(listen_fd, (struct sockaddr*)&caddr, (socklen_t*)&addr_sz);
					if(new_socket < 0){
						perror("accpet");
						exit(1);
					}
					printf("New connection, socket fd is %d, ip is %s, port is %d.\n", new_socket, inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port));
					client[new_socket].size = 0;
					client[new_socket].state = FINS;
					client[new_socket].fp = NULL;
					FD_SET(new_socket, &rcur);
				}
				else{
					// fetch instruction
					if(client[sd].state == FINS){
						char rbuf[buff_size] = "";
						char ins[buff_size] = "", fpath[buff_size] = "", check[buff_size]="";
						bzero(rbuf, buff_size);
						// check whether the client left or not and fetch instruction
						if(recv(sd, rbuf, buff_size, 0) == 0){
							getpeername(sd, (struct sockaddr*)&sockaddr, (socklen_t*)&addrlen);
							printf("Host disconnected, socket fd is %d, ip is %s, port is %d.\n", sd, inet_ntoa(sockaddr.sin_addr), ntohs(sockaddr.sin_port));
							close(sd);
							client[sd].state = HANG;
							client[sd].fp = NULL;
							client[sd].size = 0;
						}
						sscanf(rbuf,"%s %s %s", ins, fpath, check);
						if(strcmp(ins,"ls") == 0){
							char wbuf[buff_size] = "";
							bzero(wbuf,buff_size);
							DIR *dp;
							struct dirent *ep;	
							dp = opendir("./");
							while(ep = readdir(dp)){
								strcat(wbuf,ep->d_name);
								strcat(wbuf," ");
							}							
							closedir(dp);
							ssend(sd, wbuf, buff_size);
							client[sd].state = FINS;
						}
						else if(strcmp(ins,"get") == 0){
							FILE *fp;
							char tmp[buff_size] = "";
							bzero(tmp,buff_size);
							if(strlen(check) != 0){
								sprintf(tmp,"Command format error.");
								ssend(sd, tmp, buff_size);
								client[sd].state = FINS;
								continue;
							}								
							if((fp=fopen(fpath,"r"))==NULL){
								sprintf(tmp,"The %s doesn't exist.",fpath);
								ssend(sd, tmp, buff_size);
								client[sd].state = FINS;
								continue;
							}
							sprintf(tmp,"OK.");
							ssend(sd, tmp, buff_size);
							client[sd].state = GET;
							client[sd].fp = fp;
							client[sd].size = getFileSize(fp);
							FD_SET(sd, &wcur);
							FD_CLR(sd, &rcur);
							char size[buff_size] = "";
							sprintf(size,"%d",client[sd].size);
							ssend(sd, size, buff_size);
						}
						else if(strcmp(ins,"put") == 0){
							FILE *fp = fopen(fpath, "w");
							int size;
							char fsize[buff_size] = "";
							bzero(fsize, buff_size);
							srecv(sd, fsize, buff_size);
							sscanf(fsize, "%d", &size);
							client[sd].state = PUT;
							client[sd].fp = fp;
							client[sd].size = size;
						}
						else if(strcmp(ins,"play") == 0){
							FILE *fp;
							char tmp[buff_size] = "";
							bzero(tmp,buff_size);
							if(strlen(check) != 0){
								sprintf(tmp,"Command format error.");
								ssend(sd, tmp, buff_size);
								client[sd].state = FINS;
								continue;
							}
							if((fp = fopen(fpath,"r")) == NULL){
								sprintf(tmp,"The %s doesn't exist.",fpath);
								ssend(sd, tmp, buff_size);
								client[sd].state = FINS;
								continue;
							}
							fclose(fp);
							sprintf(tmp,"OK.");
							ssend(sd, tmp, buff_size);
							client[sd].cap = VideoCapture(fpath);
							int width = client[sd].cap.get(CV_CAP_PROP_FRAME_WIDTH);
							int height = client[sd].cap.get(CV_CAP_PROP_FRAME_HEIGHT); 
							client[sd].imgServer = Mat::zeros(height, width, CV_8UC3);
							if(!client[sd].imgServer.isContinuous())
								client[sd].imgServer = client[sd].imgServer.clone();
							char wbuf[buff_size] = "";
							bzero(wbuf,buff_size);
							sprintf(wbuf,"%d %d",height,width);
							ssend(sd, wbuf, buff_size);
							client[sd].state = PLAY;
							FD_CLR(sd, &rcur);
							FD_SET(sd, &wcur);
						}
					}
					else if(client[sd].state == PUT){
						char rbuf[buff_size] = "";
						bzero(rbuf, buff_size);
						srecv(sd, rbuf, buff_size);
						fwrite(rbuf,sizeof(char),min(client[sd].size,buff_size),client[sd].fp);
						client[sd].size -= min(client[sd].size, buff_size);
						if(client[sd].size == 0){
							client[sd].state = FINS;
							client[sd].size = 0;
							fclose(client[sd].fp);
							printf("Finishing PUT command | command owner: socket fd %d\n", sd);
						}
					}
				}
			}
			if(FD_ISSET(sd, &wset)){
				if(client[sd].state == GET){
					char wbuf[buff_size] = "";
					bzero(wbuf, buff_size);
					fread(wbuf, sizeof(char), min(buff_size,client[sd].size), client[sd].fp);
					ssend(sd,wbuf,buff_size);
					client[sd].size -= min(buff_size,client[sd].size);
					if(client[sd].size == 0){
						client[sd].state = FINS;
						client[sd].size = 0;
						FD_CLR(sd, &wcur);
						FD_SET(sd, &rcur);
						fclose(client[sd].fp);
						printf("Finishing GET command | command owner: socket fd %d\n", sd);
					}
				}
				else if(client[sd].state == PLAY){				
					client[sd].cap >> client[sd].imgServer;
					int imgSize = client[sd].imgServer.total() * client[sd].imgServer.elemSize(); 	
					char wbuf[buff_size] = "";
					bzero(wbuf, buff_size);
					sprintf(wbuf,"%d",imgSize);
					ssend(sd,wbuf,buff_size);
					if(imgSize == 0){
						FD_SET(sd, &rcur);
						FD_CLR(sd, &wcur);
						client[sd].state = FINS;
						client[sd].cap.release();
						continue;
					}
					memcpy(buf, client[sd].imgServer.data, imgSize);
					ssend(sd,buf,imgSize);		
				}
			}
		}
	}
}	









