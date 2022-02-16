#include <string>
#include <thread>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "helpers.h"

#define BUFFER_SIZE 1024

using namespace std;

int main(int argc, char* argv[])
{
	if (argc != 4) {
		std::cerr << "Usage: " << argv[0] << " <HOSTNAME-OR-IP> <PORT> <FILENAME> " << std::endl;
		return 1;
	}

	std::string server_ip = argv[1];
	int port = std::stoi(argv[2]);
	std::string file_path = argv[3];

	int sock;

	// create socket fd
	if ( (sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0 ) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	
	// Convert IP address from string to binary form
	if( !inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr)){
		std::cerr << "Wrong IP address" << std::endl;
		exit(EXIT_FAILURE);
	}

	int ret;
	if ( (ret = connect(sock, (sockaddr*)&server_addr, sizeof(server_addr))) < 0 ) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	// construct header
	Header header { 0 };
	setCharArrFromInt(4, header.sequenceNumber);
	setCharArrFromInt(233, header.ackNumber);
	setCharArrFromInt(1024, header.connectionID);
	header.ACK = 1;
	header.SYN = 0;
	header.FIN = 1;
	char hello_msg [6] = "hello";

	// construct return message
	char out_msg [sizeof(hello_msg) + HEADER_SIZE] = {0};
	ConstructMessage(header, hello_msg, out_msg, sizeof(hello_msg));
	if ( (ret = send(sock, out_msg, 18, 0)) < 0 ) {
		perror("send");
		exit(EXIT_FAILURE);
	}
    
	// char* buffer[BUFFER_SIZE];
	// memset(buffer, 0, sizeof(buffer));

	// if ( (ret = recv(sock, buffer, BUFFER_SIZE, 0)) < 0 ) {
	//   perror("recv");
	//   exit(EXIT_FAILURE);
	// }

	// std::cout << "Server: " << buffer << std::endl;

	if ( close(sock) < 0 ) {
		perror("close");
		exit(EXIT_FAILURE);
	}

	return 0;
}
