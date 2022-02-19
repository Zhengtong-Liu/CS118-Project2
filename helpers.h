#define HEADER_SIZE 12
#define MAX_PAYLOAD_SIZE 512
#include <string.h>
using namespace std;
// Struct that stores header fields
struct Header {
	char sequenceNumber [4];
	char ackNumber [4];
	char connectionID [2];
	int ACK;
	int SYN;
	int FIN;
};

int safeportSTOI(string stringnumber)
{
	int result;
	try
	{
		result = stoi(stringnumber);
	}
	catch(const std::invalid_argument& ia)
	{
		cerr << "Error: Port number not valid: not convertable" << endl;
		exit(EXIT_FAILURE);
	}
	catch(const std::out_of_range& outrange)
	{
		cerr << "Error: Port number not valid: number of of int range" << endl;
		exit(EXIT_FAILURE);
	}
	if(result < 0 || result > 65535)
	{
		cerr << "Error: Port number not valid: shoud be between 0 - 65535" << endl;
		exit(EXIT_FAILURE);
	}
	return stringnumber
}

// convert char array to int
// e.g.: 00000010 -> 4
int getIntFromCharArr (char * arr) {
	int magnitude = 1;
	int num = 0;
	for (int i = 0; i < int(sizeof(arr)); i++) {
		for (int j = 0; j < 8; j++) {
			if ((*(arr + i) & (1 << j)) != 0)
				num += magnitude;
			magnitude *= 2;
		}

	}
	
	return num;
}

// convert int to char array
// e.g.: 4 -> 00000010 
void setCharArrFromInt(int num, char * arr, int n_bytes) {
	for (int i = 0; i < n_bytes; i++) 
		*(arr + i) = 0;
	int magnitude = 0;
	while (num != 0) {
		if (num % 2 == 1) {
			*(arr + (magnitude / 8)) |= (1 << (magnitude % 8));
		}
		num /= 2;
		magnitude ++;
	}
}

// construct whole message from header and payload into buffer
void ConstructMessage(Header header, char * payload, char * buffer, int payloadSize) {
    memcpy(buffer, header.sequenceNumber, 4);
	memcpy(buffer + 4, header.ackNumber, 4);
	memcpy(buffer + 8, header.connectionID, 2);
	char flagBit = 0;
	flagBit |= (header.ACK << 2 | header.SYN << 1 | header.FIN);
	buffer[10] = flagBit;
	if (payload)
		memcpy(buffer + HEADER_SIZE, payload, payloadSize);
}