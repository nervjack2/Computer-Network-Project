#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <string>
#include <iostream>
#include <thread>
#include <vector>
#include "opencv2/opencv.hpp"

#define buff_size 1024
#define buff_size2 1400
using namespace std;
using namespace cv;

char buf[40000000];
Mat imgClient[buff_size2];
int idx1, idx2;
int eof;

void crecv(int conn_fd, char *buf, int size){
	int cur_size = 0;
	while(cur_size < size){
		int res = recv(conn_fd, buf + cur_size, size - cur_size * sizeof(char), 0);
		cur_size += res;
	}
}
void csend(int conn_fd, char *buf, int size){
	int cur_size = 0;
	while(cur_size < size){
		int res = send(conn_fd, buf + cur_size, size - cur_size * sizeof(char), 0);
		cur_size += res;
	}
}

int getFileSize(FILE *fp){
	fseek(fp,0L,SEEK_END);
	int size = ftell(fp);
	fseek(fp,0L,SEEK_SET);
	return size;
}

void load_video(int master_fd){
	while(1){		
		int imgSize;
		char rbuf[buff_size] = "";
		bzero(rbuf, buff_size);
		crecv(master_fd, rbuf, buff_size);
		sscanf(rbuf, "%d", &imgSize);
		if(imgSize == 0){
			eof = idx1;			
			break;
		}
		crecv(master_fd, buf, imgSize);
		uchar *iptr = imgClient[idx1%buff_size2].data;
		memcpy(iptr,buf,imgSize);
		idx1 += 1;
	}	
	return;
}

void free_memory(vector<char *> *buffer){
	for(int i = 0; i < buffer->size(); i++){
		free(buffer->operator[](i));	
	}
}

int main(int argc, char *argv[]){
	mkdir("client_folder", 0777);
	chdir("client_folder");
	
	//initialize client
	char *ip, *cport;	
	ip = strtok(argv[1],":");
	cport = strtok(NULL, ":");	
	int port = atoi(cport);
	int master_fd = socket(AF_INET, SOCK_STREAM, 0);
	
	struct sockaddr_in sockaddr;
	memset(&sockaddr, 0, sizeof(sockaddr));
	sockaddr.sin_family = AF_INET;
	sockaddr.sin_addr.s_addr = inet_addr(ip);
	sockaddr.sin_port = htons(port);
	int opt = 1;
	if(setsockopt(master_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&opt, sizeof(opt)) < 0){
		perror("setsockopt");
		exit(1);
	}
	if(connect(master_fd, (struct sockaddr*)&sockaddr, sizeof(sockaddr)) < 0){
		perror("connect");
		exit(1);
	}	
	while(1){
		printf("Please enter instruction:");
		char command[buff_size] = "";
		fgets(command,buff_size,stdin);
		char ins[buff_size] = "", fpath[buff_size] = "", check[buff_size] = "";
		sscanf(command, "%s %s %s", ins, fpath, check);
		if(strcmp(ins, "ls") == 0){
			char rbuf[buff_size] = "";
			if(strlen(fpath) != 0){
				printf("Command format error.\n");
				continue;
			}
			csend(master_fd, command, buff_size);
			bzero(rbuf,buff_size);
			crecv(master_fd, rbuf, buff_size);
			printf("%s\n", rbuf);
		}
		else if(strcmp(ins, "get") == 0){
			char tmp[buff_size]= "";			
			csend(master_fd, command, buff_size);
			bzero(tmp,buff_size);
			crecv(master_fd, tmp, buff_size);
			if(strcmp(tmp,"OK.") != 0){
				printf("%s\n",tmp);
				continue;
			}
			FILE *fp = fopen(fpath, "w");			
			char rbuf[buff_size];
			char fsize[buff_size];
			bzero(fsize,buff_size);
			crecv(master_fd, fsize, buff_size);
			int size;
			sscanf(fsize,"%d", &size);
			while(size > 0){
				bzero(rbuf,buff_size);
				crecv(master_fd, rbuf, buff_size);
				fwrite(rbuf,sizeof(char),min(size,buff_size),fp);
				size -= min(size,buff_size);
			}
			fclose(fp);		
		}
		else if(strcmp(ins, "put") == 0){
			FILE *fp;
			if(strlen(check) != 0){
				printf("Command format error.\n");
				continue;
			}
			if((fp = fopen(fpath,"r")) == NULL){
				printf("The %s doesn't exist.\n", fpath);
				continue;
			}
			csend(master_fd, command, buff_size);
			int size = getFileSize(fp);
			char fsize[buff_size] = "";
			bzero(fsize, buff_size);
			sprintf(fsize, "%d", size);
			csend(master_fd, fsize, buff_size);
			char wbuf[buff_size];
			while(size > 0){
				bzero(wbuf, buff_size);
				fread(wbuf, sizeof(char), min(size, buff_size), fp);
				csend(master_fd, wbuf, buff_size);
				size -= min(size, buff_size);
			}
			fclose(fp);
		}
		else if(strcmp(ins, "play") == 0){
			if(strcmp(&fpath[strlen(fpath)-4],".mpg") != 0){
				printf("The %s is not a mpg file.\n", fpath);
				continue;
			}
			csend(master_fd, command, buff_size);
			char tmp[buff_size] = "";
			bzero(tmp, buff_size);			
			crecv(master_fd, tmp, buff_size);
			if(strcmp(tmp,"OK.") != 0){
				printf("%s\n",tmp);				
				continue;
			}
			int height, width;
			char rbuf[buff_size] = "";
			bzero(rbuf, buff_size);
			crecv(master_fd, rbuf, buff_size);
			sscanf(rbuf, "%d %d", &height, &width);
			for(int i = 0; i < buff_size2; i++){
				imgClient[i] = Mat::zeros(height,width,CV_8UC3);
				if(!imgClient[i].isContinuous())
					imgClient[i] = imgClient[i].clone();
			}
			idx1 = 0;
			idx2 = 0;
			eof = -1;
			thread th(load_video, master_fd);
			while(1){
				if(idx2 == eof){
					printf("Playing video frame %d/%d.\n", idx2, eof);					
					break;
				}
				if(idx1 > idx2){
					if(idx2 % 100 == 0 && idx2 != 0)						
						printf("Playing video frame %d/%d.\n", idx2, idx1);					
					imshow("Video",imgClient[idx2%buff_size2]);			
					idx2 += 1;					
					char c = (char)waitKey(33.33333);
					if(c == 27){
						destroyAllWindows();
						break;
					}
				}
			}
			destroyAllWindows();
			th.join();
		}
		else{
			printf("Command not found.\n");
			continue;
		}
	}
}



