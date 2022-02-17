#include <string>
#include <thread>
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "helpers.h"

#define BUFFER_SIZE 1024
int sock = -1;
int connectionCount = 0;

using namespace std;

// signal handler
void sig_handler(int sig) {
	if (sock > 0) {
		if ( close(sock) < 0 ) {
			perror("close");
			exit(EXIT_FAILURE);
		}
  	}
	cout << "Good Bye" << endl;
	exit(EXIT_SUCCESS);
}


int main(int argc, char* argv[])
{
  	if (argc != 3) {
    	cerr << "Usage: " << argv[0] << " <PORT> <FILE-DIR> "  << endl;
    	return 1;
  	}

  	signal(SIGINT, sig_handler);
  	signal(SIGQUIT, sig_handler);
  	signal(SIGTERM, sig_handler);

  	int port = stoi(argv[1]);
  	string file_dir = argv[2];
  
  	// reference: https://www.geeksforgeeks.org/udp-server-client-implementation-c/
  	// create socket fd
  	if ( (sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0 ) {
    	perror("socket");
    	exit(EXIT_FAILURE);
  	}

	struct sockaddr_in server_addr, client_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	memset(&client_addr, 0, sizeof(client_addr));

	//server information
	server_addr.sin_family = AF_INET; // IPv4
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // IP address
	server_addr.sin_port = htons(port); // port number

	// bind socket
	if ( ::bind(sock, (sockaddr *)&server_addr, sizeof(server_addr)) < 0 ) {
		perror("bind");
		exit(EXIT_FAILURE);
	}

	// open directory to save files
	char dir[BUFFER_SIZE] = {0};
	if (getcwd(dir, sizeof(dir)) == NULL) {
		perror("getcwd");
		exit(EXIT_FAILURE);
	}
	strcat(dir, file_dir.c_str());
	if (mkdir(dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
		if (errno != EEXIST) {
			perror("mkdir");
			exit(EXIT_FAILURE);
		}
	}

	// keep track the file id to save
	int connection_id = 1;

	socklen_t sock_len;
	int n;
	char buffer[BUFFER_SIZE];
	memset(&buffer, 0, sizeof(buffer));
	while (1) {
		sock_len = sizeof(client_addr);
		if ( (n = recvfrom(sock, (char *) buffer, BUFFER_SIZE, 0, (sockaddr *)&client_addr, &sock_len)) < 0 )
		{
			perror("recvfrom");
			memset(&buffer, 0, sizeof(buffer));
			continue;
		}

		// Extract header and payload
		// header
		Header header {{0}};
		memcpy(header.sequenceNumber, buffer, 4);
		memcpy(header.ackNumber, buffer + 4, 4);
		memcpy(header.connectionID, buffer + 8, 2);
		int clientSequenceNumber = getIntFromCharArr(header.sequenceNumber);
		int clientAckNumber = getIntFromCharArr(header.ackNumber);
		int clientConnectionID = getIntFromCharArr(header.connectionID);
		// flag bits
		header.ACK = (buffer[10] & 4) != 0;
		header.SYN = (buffer[10] & 2) != 0;
		header.FIN = (buffer[10] & 1) != 0;

		// Receive SYN, establish connection
		if (header.SYN) {
			connectionCount ++;
			setCharArrFromInt(connectionCount, header.connectionID, 2);
			setCharArrFromInt(clientSequenceNumber + 1, header.ackNumber, 4);
			setCharArrFromInt(4321, header.sequenceNumber, 4);
			header.ACK = 1;
		}

		cout << "sequenceNumber: " << sequenceNumber << endl;
		cout << "ackNumber: " << ackNumber << endl;
		cout << "connectionID: " << connectionID << endl;
		cout << "ACK: " << header.ACK << endl;
		cout << "SYN: " << header.SYN << endl;
		cout << "FIN: " << header.FIN << endl;
		cout << "Payload: " << buffer + HEADER_SIZE << endl;

		string file_path (dir);
		file_path += "/" + to_string(connection_id) + ".file";

		ofstream f (file_path);
		if (f.is_open()) {
			// cout << file_path << endl;
			f << buffer + HEADER_SIZE;
			f.close();
		} else {
			cerr << "Unable to open the file: " << file_path << endl;
		}

		// construct return message
		char out_msg [sizeof(payload) + HEADER_SIZE];
		memset(out_msg, 0, sizeof(out_msg));
		ConstructMessage(header, payload, out_msg, sizeof(payload));
		if ( (sendto(sock, out_msg, sizeof(out_msg), 0, (sockaddr *)&client_addr, sock_len)) < 0)
		{
		  	perror("sendto");
		  	continue;
		}
	}

	return 0;
}
