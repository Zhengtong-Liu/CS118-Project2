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
	// cout << "Good Bye" << endl;
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
	// if (getcwd(dir, sizeof(dir)) == NULL) {
	// 	perror("getcwd");
	// 	exit(EXIT_FAILURE);
	// }
	strcat(dir, file_dir.c_str());
	if (mkdir(dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
		if (errno != EEXIST) {
			// cerr << "directory is " << dir << endl;
			perror("mkdir");
			exit(EXIT_FAILURE);
		}
	}

	socklen_t sock_len;
	int n;
	char buffer[BUFFER_SIZE];
	memset(buffer, 0, sizeof(buffer));


	// Header previous_header {{0}};

	// unordered_map<int, Header> previous_header_map;
	unordered_map<int, ServerConnectionController*> client_controller_map;

	// non-blocking
	int yes = 1;
	ioctl(sock, FIONBIO, (char*)&yes);

	while (1) {
	// ================================ CHECK TIMER =========================================
		// check whether any one of connections is expired
		for (auto it : client_controller_map) 
		{
			// retrieve retransmission timer
			time_t retransmission_t = it.second -> retransmission_timer;
			// if sent SYN but did not receive SYN ACK, check whether need to retransmit (similar for FIN)
			if ((it.second -> sentSYN && (!(it.second -> recvSYNACK))) || (it.second -> sentFIN && (!(it.second -> recvFINACK)))) {
				if ((time(0) - retransmission_t) > 0.5) {
					// get header to be retransmitted
					(it.second -> cwnd) -> timeout();
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

					// cerr << "This is a retransmitted packet due to ";
					// if (it.second -> sentSYN && (!(it.second -> recvSYNACK)))
					// 	cerr << "sent SYN but not received SYNACK" << endl;
					// else
					// 	cerr << "sent FIN but not received FINACK" << endl;
					
					// TO BE CHANGED
					int cwnd_size = (it.second -> cwnd) -> get_cwnd_size();
					if (cwnd_size <= 1)
					{
						cerr << "cannot resend SYN/FIN at this time " << endl;
						continue;
					}

					// retransmit the header
					if ( (sendto(sock, retransmit_header, sizeof(retransmit_header), 0, (sockaddr *)&temp_addr, temp_sock_len)) < 0)
					{
						perror("sendto");
						continue;
					}
					outputMessage(temp_header, "SEND");
					it.second -> retransmission_timer = time(0);
				}
			}
		}

		for (auto it = client_controller_map.begin(); it != client_controller_map.end();) 
		{
			// retrieve the shut down timer, remove from client controller map if time out
			time_t prev_shut_down_t = (it -> second) -> shut_down_timer;
			if ((time(0) - prev_shut_down_t) > 10) {

				// abort the connection and write a single ERROR string
				string file_path (dir);
				file_path += "/" + to_string((it->second) -> ConnectionID) + ".file";

				fstream f;
				f.open(file_path, ios_base::app | ios::binary); // append to file if exist
				if (f.is_open())
					f << "ERROR";

				delete it->second;
				it->second = NULL;
				// cerr << "LOG: connection ID " << (it -> first) << " expires" << endl;
				it = client_controller_map.erase(it);
			}
			else 
				it ++;
		}
	// ================================ RECV FILE =========================================
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

	// ================================ EXTRACT HEADER =========================================
		// Extract header and payload
		// header
		Header header = {0, 0, 0, 0, 0, 0};
		Header fin_header = {0, 0, 0, 0, 0, 0};

		DeconstructMessage(header, buffer);

	// ================================ CHECK VALIDITY OF PACKET =========================================
		// construct and modify client controller of each connection
		if (header.SYN) {
			client_controller_map[connectionCount+1] = new ServerConnectionController(connectionCount+1, header.sequenceNumber, INITIAL_SEQ+1);
		}
		// the connection to this client is closed
		// else if (client_controller_map.find(header.connectionID) != client_controller_map.end() && client_controller_map[header.connectionID] -> sentFIN && client_controller_map[header.connectionID] -> recvFINACK)
		// {
		// 	continue;
		// }
		// reset shutdown timer
		else {
			client_controller_map[header.connectionID] -> shut_down_timer = time(0);
		}
		
		// payload length is 0 for SYN and FIN packet
		int payloadLength = (header.SYN || header.FIN) ? 0 : (n - HEADER_SIZE);
		// if payload length is negative, drop the packet
		if (payloadLength < 0) {
			cerr << "Payload length is negative with payload length: " << payloadLength << endl;
			outputMessage(header, "DROP");
			continue;
		}
		// if not SYN and cannot find connection ID of this packet in the map, drop packet
		if ((!header.SYN) && (client_controller_map.find(header.connectionID) == client_controller_map.end())) {
			cerr << "Cannot find connection ID: " << header.connectionID << endl;
			outputMessage(header, "DROP");
			continue;
		}
		// if this is ACK, check the ack number
		if (header.ACK)
		{
			// int offset = (previous_header_map[header.connectionID].FIN || previous_header_map[header.connectionID].SYN) ? 1 : 0;
			if (header.ackNumber != client_controller_map[header.connectionID] -> lastSentSeqNum) {
				cerr << "ACK # error, expect: " << client_controller_map[header.connectionID] -> lastSentSeqNum << " get: " << header.ackNumber << endl;
				outputMessage(header, "DROP");
				continue;
			}

			(client_controller_map[header.connectionID] -> cwnd) -> recvACK();
			(client_controller_map[header.connectionID] -> cwnd) -> update_cumack(header.ackNumber);
		}
		// if the packet received exceed rwnd, drop the packet
		if ((!header.SYN) && (header.sequenceNumber - (client_controller_map[header.connectionID] -> expectedSeqNum)) > RWND)
		{
			cerr << "Sequence # " << header.sequenceNumber << " out of range" << endl;
			outputMessage(header, "DROP");
			continue;
		}

		outputMessage(header, "RECV");
		// out of order means the incoming packet has seq # > current expected seq #
		// if (client_controller_map[header.connectionID] -> expectedSeqNum < header.sequenceNumber) {
		// 	cerr << "LOG: out of order packet" << endl;
		// 	cerr << "Expecting: " << to_string(client_controller_map[header.connectionID] -> expectedSeqNum) << " get: " << to_string(header.sequenceNumber) << endl;
		// }

		/* Header changes:
			ABOVE this line, header refers to header receives from client (except connectionID); 
			BELOW this line, gradually convert to header to be sent to client 
		*/

		// ================================ SET OUTPUT MESSAGE and STORE INFORMATION =========================================

		// Receive SYN, establish connection
		if (header.SYN) {
			connectionCount ++;
			header.connectionID = connectionCount;
			header.ackNumber = header.sequenceNumber + 1;
			header.sequenceNumber  = INITIAL_SEQ;
			header.ACK = 1;
			header.SYN = 1;

			// store the client_addr info and syn packet related info
			client_controller_map[header.connectionID] -> expectedSeqNum = header.ackNumber; // this is now the ACK to be sent

			sockaddr_in current_client_addr = client_addr;
			client_controller_map[header.connectionID] -> client_addr_info = current_client_addr;
			
			client_controller_map[header.connectionID] -> sentSYN = true;
			client_controller_map[header.connectionID] -> retransmission_timer = time(0);
			client_controller_map[header.connectionID] -> SYN_header = header;
			
		}
		// FIN
		else if (header.FIN) {

			header.ackNumber = header.sequenceNumber + 1;
			header.sequenceNumber = client_controller_map[header.connectionID] -> lastSentSeqNum;
			header.ACK = 1;
			header.FIN = 0;

			// in the case of FIN, needs to send additional FIN packet
			fin_header = header;
			fin_header.ackNumber = 0;
			fin_header.ACK = 0;
			fin_header.FIN = 1;

			// TO BE CHANGED
			int cwnd_size = (client_controller_map[header.connectionID] -> cwnd) -> get_cwnd_size();
			if (cwnd_size <= 1)
			{
				cerr << "cannot send FIN at this time " << endl;
				continue;
			}
			
			// store fin related info
			client_controller_map[header.connectionID] -> expectedSeqNum = header.ackNumber; // this is now the ACK to be sent

			client_controller_map[header.connectionID] -> sentFIN = true;
			client_controller_map[header.connectionID] -> retransmission_timer = time(0);
			client_controller_map[header.connectionID] -> FIN_header = fin_header;

		} 
		// receives ACK (not SYN ACK), no need to send message back
		else if (header.ACK && (payloadLength == 0))
		{
			if (client_controller_map[header.connectionID] -> sentFIN) {

				client_controller_map[header.connectionID] -> recvFINACK = true;

				// previous_header_map[header.connectionID].FIN = 0;
				// previous_header_map[header.connectionID].ACK = 0;

				// cerr << "Connection " << header.connectionID << " turned down" << endl;
				if ((client_controller_map.find(header.connectionID) != client_controller_map.end()) && (client_controller_map[header.connectionID]))
				{
					delete client_controller_map[header.connectionID];
					client_controller_map[header.connectionID] = NULL;
					client_controller_map.erase(header.connectionID);
				}
			}
			continue;
		}
		// Normal case, receives packet with payload
		else {
			if (header.sequenceNumber >= (client_controller_map[header.connectionID] -> expectedSeqNum))
			{
				char* payload = new char[payloadLength+1];
				memset(payload, 0, payloadLength+1);
				memcpy(payload, buffer + HEADER_SIZE, payloadLength);

				// store file into payload map
				if (payloadLength > 0) {
					(client_controller_map[header.connectionID] -> payload_map)[header.sequenceNumber] = payload;
					(client_controller_map[header.connectionID] -> payload_length_map)[header.sequenceNumber] = payloadLength;
				}
			}

			// SYN ACK
			if ((client_controller_map[header.connectionID] -> sentSYN) && header.ACK) {
				client_controller_map[header.connectionID] -> recvSYNACK = true;
				// prevent looping
				// previous_header_map[header.connectionID].SYN = 0;
				// previous_header_map[header.connectionID].ACK = 0;
				header.sequenceNumber = header.ackNumber;
			} else {
				// for other cases, ack from client is 0; need to set server's sequence number from previous header
				header.sequenceNumber = client_controller_map[header.connectionID] -> lastSentSeqNum;
			}
			header.ACK = 1;

			// ================================ WRITE TO FILE and SET CUMMULATIVE ACK =========================================
			// append payload to specified file
			string file_path (dir);
			file_path += "/" + to_string(connectionCount) + ".file";

			fstream f;
			f.open(file_path, ios_base::app | ios::binary); // append to file if exist
			int base = client_controller_map[header.connectionID] -> expectedSeqNum;
			// find if there is a packet with sequence number starting from the expected seq number;
			// if so, write to file and update: expected sequence number += payload size of this packet
			while ((client_controller_map[header.connectionID] -> payload_map).find(base) != (client_controller_map[header.connectionID] -> payload_map).end())
			{
				char* current_payload = (client_controller_map[header.connectionID] -> payload_map)[base];

				int current_payload_length = (client_controller_map[header.connectionID] -> payload_length_map)[base];
				f.write(current_payload, current_payload_length);
				base += current_payload_length;

				//[NOT SURE] Navie way of handling 102400, not sure if correct
				if (base >= MAX_ACK)
					base = base % MAX_ACK;
			}
			// cerr << "current base is " << base << endl;
			client_controller_map[header.connectionID] -> expectedSeqNum = base;

			f.close();

			// after we traverse the payload map and get the last in-order byte number, we can set the ack number
			header.ackNumber = client_controller_map[header.connectionID] -> expectedSeqNum;

		}

		// ================================ RETURN MESSAGE =========================================
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
		// if (fin_header.FIN) previous_header_map[header.connectionID] = fin_header;
		// else previous_header_map[header.connectionID] = header;
		if (fin_header.FIN || header.SYN) client_controller_map[header.connectionID] -> lastSentSeqNum = header.sequenceNumber + 1;
		else client_controller_map[header.connectionID] -> lastSentSeqNum = header.sequenceNumber;
	}

	return 0;
}
