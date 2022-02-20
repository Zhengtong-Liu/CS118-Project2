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
#include <unordered_map>

#include "helpers.h"

#define BUFFER_SIZE 1024
#define INITIAL_SEQ 4321
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
	//==========================================InputProcess=============================
  	if (argc != 3) {
    	cerr << "Usage: " << argv[0] << " <PORT> <FILE-DIR> "  << endl;
    	return 1;
  	}

  	signal(SIGINT, sig_handler);
  	signal(SIGQUIT, sig_handler);
  	signal(SIGTERM, sig_handler);
    
  	int port = safeportSTOI((argv[1]));
  	string file_dir = argv[2];
	//==========================================InputProcess-END=============================
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

	socklen_t sock_len;
	int n;
	char buffer[BUFFER_SIZE];
	memset(&buffer, 0, sizeof(buffer));
	// Header previous_header {{0}};

	unordered_map<int, Header> client_status;

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
		Header header = {0, 0, 0, 0, 0, 0};
		Header fin_header {0, 0, 0, 0, 0, 0};

		DeconstructMessage(header, buffer);
		outputMessage(header, false);
		int payloadLength = strlen(buffer + HEADER_SIZE); // [NOT SURE] whether payload is terminated with '/0'

		// flag bits
		header.ACK = (buffer[10] & 4) != 0;
		header.SYN = (buffer[10] & 2) != 0;
		header.FIN = (buffer[10] & 1) != 0;

		// Receive SYN, establish connection
		if (header.SYN) {
			connectionCount ++;
			header.connectionID = connectionCount;
			header.ackNumber = header.sequenceNumber + 1;
			header.sequenceNumber  = INITIAL_SEQ;
			header.ACK = 1;
			header.SYN = 1;
		}
		else if (client_status[header.connectionID].SYN) {
			client_status[header.connectionID].SYN = 0;
			client_status[header.connectionID].ACK = 0;
			continue;
		}
		else if (header.FIN) {
			header.ackNumber = header.sequenceNumber + 1;
			// [NOT SURE] For FIN ACK, set seq # to previous seq #
			header.sequenceNumber = client_status[connectionCount].sequenceNumber;
			header.ACK = 1;

			// in the case of FIN, needs to send additional FIN packet
			fin_header = header;

			fin_header.ackNumber = 0;
			fin_header.ACK = 0;
			fin_header.FIN = 1;

		} else {
			int clientAckNumber = header.ackNumber;
			header.ackNumber = header.sequenceNumber + payloadLength;
			header.sequenceNumber = clientAckNumber;
			header.ACK = 1;
		}

		string file_path (dir);
		file_path += "/" + to_string(connectionCount) + ".file";

		ofstream f;
		f.open(file_path, ios_base::app); // append to file if exist
		if (f.is_open()) {
			// cout << file_path << endl;
			f << buffer + HEADER_SIZE;
			f.close();
		} else {
			cerr << "Unable to open the file: " << file_path << endl;
		}

		// construct return message
		char out_msg [HEADER_SIZE];
		memset(out_msg, 0, sizeof(out_msg));

		ConstructMessage(header, NULL, out_msg, 0);
		if ( (sendto(sock, out_msg, sizeof(out_msg), 0, (sockaddr *)&client_addr, sock_len)) < 0)
		{
		  	perror("sendto");
		  	continue;
		}
		outputMessage(header, false, "SEND");

		if (header.FIN)
		{
			memset(out_msg, 0, sizeof(out_msg));
			ConstructMessage(fin_header, NULL, out_msg, 0);
			if ( (sendto(sock, out_msg, sizeof(out_msg), 0, (sockaddr *)&client_addr, sock_len)) < 0)
			{
				perror("sendto");
				continue;
			}
			outputMessage(fin_header, false, "SEND");
		}

		client_status[connectionCount] = header;
	}

	return 0;
}
