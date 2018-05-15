/*
	rdt3.0 UDP server

	Author: Ryan Ku
	Date: 22 February 2018
	Lab: Thursday 5:15pm - 8:00pm
	Description:	This code acts as a server machine that binds to a socket.
  					Once a client machine sends data to the socket using user
  					datagram protocol (UDP), the server machine picks it up,
  					creates a destination file under a given name, and saves
  					the contents that it reads into that file. A random function
  					has been implemented to randomly flip and/or skip sending
  					ACKs back to the client.

*/

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// GLOBAL VARIABLES
int socket_fd, count, oldSEQ = 0;
socklen_t addr_size;
struct sockaddr_in serv_addr, *serv_addr_pointer;
struct sockaddr_storage serv_stor, *serv_stor_pointer;
struct PACKET msg_packet, ack_packet, *msg_packet_pointer, *ack_packet_pointer;

int csum();
bool goodCSUM();
bool sendACK(bool positive);
int receiveMessage();

struct HEADER
{
	int seq_ack;	// SEQ for data and ACK for acknowledgement
	int len;		// length of data in bytes (zero for ACK)
	int checksum;	// checksum calculated (by byte)
};

struct PACKET
{
	struct HEADER header;
	char data[10];
};

int csum()
{
	char checksum = 0;
	int i;
	char *packet = (char *)msg_packet_pointer;
	for(i = 0; i < sizeof(packet); i++)	checksum ^= packet[i];
	return (int) checksum;
}

bool goodCSUM()
{
	int old = msg_packet.header.checksum;
	msg_packet.header.checksum = 0;
	msg_packet.header.checksum = csum(msg_packet);
	if(msg_packet.header.checksum != old)
	{
		printf("ERROR: BAD CSUM on message #%i - checksum: %i\n", count, msg_packet.header.checksum);
		return false;
	}
	return true;
}

bool goodSEQ()
{
	if(msg_packet.header.seq_ack == oldSEQ)
	{
		printf("ERROR: BAD SEQ on message #%i - sequence: %i, expected: %i\n", count, msg_packet.header.seq_ack, abs(oldSEQ - 1));
		return false;
	}
	return true;
}

bool sendACK(bool positive)
{
	// create ACK
	ack_packet.header.seq_ack = msg_packet.header.seq_ack;
	ack_packet.header.len = 0;
	ack_packet.header.checksum = msg_packet.header.checksum;
	bool flip = rand() < RAND_MAX / 3; // 33% chance of sending random NAK
	bool skip = rand() < RAND_MAX / 3; // 33% chance of skipping random ACK

	if(!positive || flip)
	{
		ack_packet.header.seq_ack = abs(msg_packet.header.seq_ack - 1);
		if(positive && flip)	printf("INVERTING to rand() NAK\n");
		else if (!positive)		printf("SENDING NAK: %i\n", ack_packet.header.seq_ack);
	}
	if(!skip)
	{
		int bytes_sent = sendto(socket_fd, ack_packet_pointer, sizeof(ack_packet), 0, (struct sockaddr *)&serv_stor, addr_size);
		if(bytes_sent == -1)
		{
			printf("sendto() error");
			exit(EXIT_FAILURE);
		}
		if(positive)			 printf("SENDING ACK: %i\n", ack_packet.header.seq_ack);
	} else 	printf("SKIPPING rand() ACK\n");
	return positive && !flip;
}

int receiveMessage()
{
	count++;
	int bytes_received = recvfrom(socket_fd, msg_packet_pointer, sizeof(msg_packet), 0, (struct sockaddr *)&serv_stor, &addr_size);
	if(bytes_received < 0)
	{
		perror("recvfrom() error");
		exit(EXIT_FAILURE);
	}
	printf("\nMessage #%i: %s\n", count, msg_packet.data);
	printf("Message #%i received - length: %d - SEQ: %d - checksum: %d\n", count, msg_packet.header.len, msg_packet.header.seq_ack, msg_packet.header.checksum);
	return bytes_received;
}

int main(int argc, char* argv[])
{
	int bind_var, bytes_received, port_number;
	time_t t;
	serv_stor_pointer = &serv_stor;
	msg_packet_pointer = &msg_packet;
	ack_packet_pointer = &ack_packet;

	// set seed for rand() ACK call
	srand((unsigned) time(&t));

	// checks for port number error
	if(argv[1] == NULL)
	{
		printf("Error: No Port Number.\n");
		exit(EXIT_FAILURE);
	}
	port_number = atoi(argv[1]);

	// set up
	memset(msg_packet.data, '0', sizeof(msg_packet.data));
	memset(ack_packet.data, '0', sizeof(ack_packet.data));
	memset(&serv_addr, '0', sizeof(serv_addr));

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port_number);
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	memset((char *)serv_addr.sin_zero, 0, sizeof(serv_addr.sin_zero));
	addr_size = sizeof(serv_stor);

	// create, bind, and listen to socket
	socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if(socket_fd < 0)
	{
		printf("Error: Could not create socket.");
		exit(EXIT_FAILURE);
	}
	printf("Socket file descriptor creation: SUCCESS\n");

	bind_var = bind(socket_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
	if(bind_var != 0)
	{
		printf("Error: Could not bind.\n");
		exit(EXIT_FAILURE);
	}
	printf("Socket file descriptor binding: SUCCESS\n\n");

	// create destination file
	FILE* destfile;
	char filename[100];
	memset(filename, '0', sizeof(filename));
	bytes_received = receiveMessage();
	while(bytes_received > 0)
	{
		if(!goodSEQ())
		{	// if bad sequence number, skip and ask for next packet
			sendACK(true);
			bytes_received = receiveMessage();
			continue;
		}
		if(!goodCSUM())
		{	// check checksum
			sendACK(false);
			bytes_received = receiveMessage();
			continue;
		}
		if(!sendACK(true))
		{	// if NAK was sent through rand()
			bytes_received = receiveMessage();
			continue;
		}
		strcpy(filename, msg_packet.data);
		destfile = fopen(filename, "wb");
		break;
	}
	printf("Destination file created - Name: %s\n", filename);
	
	oldSEQ = msg_packet.header.seq_ack;
	bytes_received = receiveMessage();
	while(msg_packet.header.len != 0)
	{
		if(!goodCSUM())
		{	// check checksum
			sendACK(false);
			bytes_received = receiveMessage();
			continue;
		}
		if(!goodSEQ())
		{	// if bad sequence number, skip and ask for next packet
			sendACK(true);
			bytes_received = receiveMessage();
			continue;
		}
		if(!sendACK(true))
		{	// if NAK was sent through rand()
			bytes_received - receiveMessage();
			continue;
		}

		// write contents to the file
		fwrite(msg_packet.data, 1, msg_packet.header.len - 1, destfile);
		printf("Writing message #%i to file\n", count);

		// save last correct packet's sequence number
		oldSEQ = msg_packet.header.seq_ack;
		bytes_received = receiveMessage();
	}

	printf("File received.\n");
	close(socket_fd);
	fclose(destfile);
	exit(EXIT_SUCCESS);
}
