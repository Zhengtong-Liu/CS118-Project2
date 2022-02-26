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
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unordered_map>
#include <time.h>

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
	bool out_of_order = false;
	char buffer[BUFFER_SIZE];
	memset(buffer, 0, sizeof(buffer));


	// Header previous_header {{0}};

	unordered_map<int, Header> previous_header_map;
	unordered_map<int, ServerConnectionController*> client_controller_map;

	// non-blocking
	int yes = 1;
	ioctl(sock, FIONBIO, (char*)&yes);

	while (1) {

		// check whether any one of connections is expired
		for (auto it : client_controller_map) 
		{
			time_t retransmission_t = it.second -> retransmission_timer;
			if ((it.second -> sentSYN && (!(it.second -> recvSYNACK))) || (it.second -> sentFIN && (!(it.second -> recvFINACK)))) {
				if ((time(0) - retransmission_t) >= 0.5) {
					Header temp_header;
					if (it.second -> sentSYN && (!(it.second -> recvSYNACK)))
						temp_header = it.second -> SYN_header;
					else
						temp_header = it.second -> FIN_header;

					char retransmit_header [HEADER_SIZE];
					memset(retransmit_header, 0, sizeof(retransmit_header));
					ConstructMessage(temp_header, NULL, retransmit_header, 0);

					struct sockaddr_in temp_addr;
					memset(&temp_addr, 0, sizeof(temp_addr));
					temp_addr = it.second -> client_addr_info;
					socklen_t temp_sock_len = sizeof(temp_addr);

					if ( (sendto(sock, retransmit_header, sizeof(retransmit_header), 0, (sockaddr *)&temp_addr, temp_sock_len)) < 0)
					{
						perror("sendto");
						continue;
					}
					outputMessage(temp_header, "SEND");

				}
			}

			time_t prev_shut_down_t = it.second -> shut_down_timer;
			if ((time(0) - prev_shut_down_t) >= 10) {
				for (auto it_temp: it.second -> payload_map)
				{
					if (it_temp.second)
					{
						delete it_temp.second;
						it_temp.second = NULL;
					}
				}

				delete it.second;
				it.second = NULL;
				client_controller_map.erase(it.first);
				cout << "LOG: connection ID " << it.first << " expires" << endl;
			}
			
		}

		sock_len = sizeof(client_addr);
		memset(buffer, 0, sizeof(buffer));
		n = recvfrom(sock, (char *) buffer, BUFFER_SIZE, 0, (sockaddr *)&client_addr, &sock_len);
		// non-blocking
		if(n == -1 && errno == EWOULDBLOCK)
		{
			memset(buffer, 0, sizeof(buffer));
			continue;
		} else if (n < 0) {
			perror("recvfrom");
			memset(buffer, 0, sizeof(buffer));
			continue;
		}

		// Extract header and payload
		// header
		Header header = {0, 0, 0, 0, 0, 0};
		Header fin_header = {0, 0, 0, 0, 0, 0};

		DeconstructMessage(header, buffer);
		outputMessage(header, "RECV");
		int payloadLength = strlen(buffer + HEADER_SIZE); // [NOT SURE] whether payload is terminated with '/0'
		cout << "Payloadlength: " << to_string(payloadLength)<<endl;

		// flag bits
		header.ACK = (buffer[10] & 4) != 0;
		header.SYN = (buffer[10] & 2) != 0;
		header.FIN = (buffer[10] & 1) != 0;

		// construct and modify client controller of each connection
		if (header.SYN) {
			client_controller_map[connectionCount + 1] = new ServerConnectionController(connectionCount+1, header.sequenceNumber+1, INITIAL_SEQ+1);
		} 
		else {
			// if this is not a SYN packet but cannot find connection ID of this packet in the map, drop packet
			if (client_controller_map.find(header.connectionID) == client_controller_map.end()) {
				outputMessage(header, "DROP");
				continue;
			} 
			else {
				// check ACK
				if (header.ACK) {
					if (header.ackNumber != (INITIAL_SEQ + 1))
					{
						outputMessage(header, "DROP");
						continue;
					}
				}
				else if (!header.FIN) {
					int current_base = client_controller_map[header.connectionID] -> lastSentSeqNum;
					if ((header.sequenceNumber - current_base) > RWND)
					{
						outputMessage(header, "DROP");
						continue;
					}
					// out of order means the incoming packet has seq # > current expected seq #
					if (client_controller_map[header.connectionID] -> expectedSeqNum < header.sequenceNumber) {
						out_of_order = true;
						cout << "LOG: out of order packet" << endl;
						if(debug)
							cout<<"Expecting: " << to_string(client_controller_map[header.connectionID] -> expectedSeqNum) << " get: " << to_string(header.sequenceNumber);
					} 
					else {
						if(debug)
							cout<<"Setting: " << to_string(client_controller_map[header.connectionID] -> expectedSeqNum) << " to: " <<to_string(header.sequenceNumber) << " + " << to_string(payloadLength)<<endl;
						// client_controller_map[header.connectionID] -> expectedSeqNum = header.sequenceNumber + payloadLength;
					}
				}
				// update this client controller's information
				client_controller_map[header.connectionID] -> lastSentSeqNum = header.ackNumber;
				client_controller_map[header.connectionID] -> shut_down_timer = time(0);
			}
		}

		// ack number determined after determine the last in-order byte 
		bool set_ack_number = false;

		// Receive SYN, establish connection
		if (header.SYN) {
			connectionCount ++;
			header.connectionID = connectionCount;
			header.ackNumber = header.sequenceNumber + 1;
			header.sequenceNumber  = INITIAL_SEQ;
			header.ACK = 1;
			header.SYN = 1;

			sockaddr_in current_client_addr = client_addr;
			client_controller_map[header.connectionID] -> client_addr_info = current_client_addr;
			
			client_controller_map[header.connectionID] -> sentSYN = true;
			client_controller_map[header.connectionID] -> retransmission_timer = time(0);
			client_controller_map[header.connectionID] -> SYN_header = header;
			
		}
		// SYN ACK
		// else if (previous_header_map[header.connectionID].SYN && header.ACK) {
		// 	previous_header_map[header.connectionID].SYN = 0;
		// 	previous_header_map[header.connectionID].ACK = 0;
		// 	continue;
		// }
		// FIN
		else if (header.FIN) {
			header.ackNumber = header.sequenceNumber + 1;
			// [NOT SURE] For FIN ACK, set seq # to previous seq #
			header.sequenceNumber = previous_header_map[connectionCount].sequenceNumber;
			header.ACK = 1;
			header.FIN = 0;
			// in the case of FIN, needs to send additional FIN packet
			fin_header = header;

			fin_header.ackNumber = 0;
			fin_header.ACK = 0;
			fin_header.FIN = 1;

			client_controller_map[header.connectionID] -> sentFIN = true;
			client_controller_map[header.connectionID] -> retransmission_timer = time(0);
			client_controller_map[header.connectionID] -> FIN_header = fin_header;


		} 
		// receives ACK, do nothing
		else if (header.ACK)
		{
			if (previous_header_map[header.connectionID].SYN) {
				// did not check seq #, ack #
				client_controller_map[header.connectionID] -> recvSYNACK = true;
			} else if (previous_header_map[header.connectionID].FIN) {
				client_controller_map[header.connectionID] -> recvFINACK = true;
				continue;
			} else
				continue;
		}
		// Normal case, receives packet with payload
		else {

			if (previous_header_map[header.connectionID].SYN && header.ACK) {
				previous_header_map[header.connectionID].SYN = 0;
				previous_header_map[header.connectionID].ACK = 0;
			}

			char* payload = new char[payloadLength+1];
			memset(payload, 0, payloadLength+1);
			strcpy(payload, buffer + HEADER_SIZE);

			(client_controller_map[header.connectionID] -> payload_map)[header.sequenceNumber] = payload;


			int clientAckNumber = header.ackNumber;
			// if out of order, send dup ACK
			// if (out_of_order)
			// 	header.ackNumber = client_controller_map[header.connectionID] -> expectedSeqNum;
			set_ack_number = true;

			header.sequenceNumber = clientAckNumber;
			header.ACK = 1;

		}

		// append payload to specified file
		string file_path (dir);
		file_path += "/" + to_string(connectionCount) + ".file";

		ofstream f;
		f.open(file_path, ios_base::app); // append to file if exist
		int base = client_controller_map[header.connectionID] -> expectedSeqNum;
		// find if there is a packet with sequence number starting from the expected seq number;
		// if so, write to file and update: expected sequence number += payload size of this packet
		while ((client_controller_map[header.connectionID] -> payload_map).find(base) != (client_controller_map[header.connectionID] -> payload_map).end())
		{
			char* current_payload = (client_controller_map[header.connectionID] -> payload_map)[base];

			if (debug)
			{
				cout << "current expected seq num is " << base << endl;
				printf("current payload is %s\n", (client_controller_map[header.connectionID] -> payload_map)[base]);
			}

			if (f.is_open())
				f << current_payload;
			base += (int)(strlen(current_payload));
		}
		client_controller_map[header.connectionID] -> expectedSeqNum = base;

		f.close();

		// after we traverse the payload map and get the last in-order byte number, we can set the ack number
		if (set_ack_number)
			header.ackNumber = client_controller_map[header.connectionID] -> expectedSeqNum;


		// construct and send return message
		char out_msg [HEADER_SIZE];
		memset(out_msg, 0, sizeof(out_msg));
		ConstructMessage(header, NULL, out_msg, 0);
		if ( (sendto(sock, out_msg, sizeof(out_msg), 0, (sockaddr *)&client_addr, sock_len)) < 0)
		{
		  	perror("sendto");
		  	continue;
		}
		outputMessage(header, "SEND");

		// FIN case, send additional FIN message
		if (fin_header.FIN)
		{
			memset(out_msg, 0, sizeof(out_msg));
			ConstructMessage(fin_header, NULL, out_msg, 0);
			if ( (sendto(sock, out_msg, sizeof(out_msg), 0, (sockaddr *)&client_addr, sock_len)) < 0)
			{
				perror("sendto");
				continue;
			}
			outputMessage(fin_header, "SEND");
		}
		
		// assign current header to previous header
		previous_header_map[header.connectionID] = header;
	}

	return 0;
}
