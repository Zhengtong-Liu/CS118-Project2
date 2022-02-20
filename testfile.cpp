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
#include <algorithm>
// #include "helpers.h"

#define BUFFER_SIZE 1024
int sock = -1;
int connectionCount = 0;

using namespace std;

class Cwnd
{
	int cwnd_size;
	int ssthresh;
	int max_cwnd;
	bool is_congestion_avoidance;
	Public:
	Cwnd():cwnd_size(512),ssthresh(10000),max_cwnd(51200),is_congestion_avoidance(false){}
	int get_cwnd_size()
	{
		return cwnd_size;
	}
	int get_ssthresh()
	{
		return ssthresh;
	}
	void recvACK()
	{
		if(!is_congestion_avoidance && (cwnd_size < ssthresh))
		{
			cwnd_size = std::min(cwnd_size+512, max_cwnd);
		}
		else
		{
			is_congestion_avoidance = true;
			cwnd_size = std::min(cwnd_size+((512*512)/cwnd_size), max_cwnd);
		}
	}
	void timeout()
	{
		ssthresh = cwnd_size / 2;
		cwnd_size = 512;
		is_congestion_avoidance = false;
	}
}
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
