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

// #include "helpers.h"

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
	try
	{
		string a = "hahahah";
  		// int port = stoi(a);
		// int port2 = stoi("21474836419");
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
  	
	return 0;
}
