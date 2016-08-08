/*
	Author: Rocky Tatiefo
	This file contains source code for a program which creates a temporary server and takes in a string of text from a client,
	and then display the text and scrolls it across an led matrix. 
*/


#include "led-matrix.h"
#include "graphics.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <iostream>
#include <thread>
#include <mutex>

#define PORT "3490"	//Port for client to
#define BACKLOG 5	//Number of pendong connections to be held
#define MAXDATASIZE 100	//Max number of bytes to be received at once

using namespace rgb_matrix;

bool scroll = false;	//Boolean used to determine wether to scroll text
std::mutex mtx;		//Mutex for critical section

static int usage(const char *progname) {
  fprintf(stderr, "usage: %s [options]\n", progname);
  fprintf(stderr, "Options:\n"
        "\t-f <font-file>: Use given font.\n"
        "\t-P <parallel> : For Plus-models or RPi2: parallel chains. 1..3. "
        "Default: 1\n"
        "\t-c <chained>  : Daisy-chained boards. Default: 1.\n"
        "\t-y <y-origin> : Y-Origin of displaying text (Default: 0)\n"
        "\t-C <r,g,b>    : Color. (Default 255,255,0)\n"
	"\t-s <speed>    : Scrolling speed (1-10. Default: 5) \n");
  return 1;
}

static bool parseColor(Color *c, const char *str) {
  return sscanf(str, "%hhu,%hhu,%hhu", &c->r, &c->g, &c->b) == 3;
}

// Used to by serverCall to get address (sockaddr) in IPv4 or IPv6
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET)
		return &(((struct sockaddr_in*)sa)->sin_addr);
	else
		return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// Use to make a server that will recieve text
void serverCall(char **msg){
	char buffe[MAXDATASIZE]; // Buffer that will take in incoming data
	int sockfd, new_fd, numbytes; // Listen on sock_fd, new connection on new_fd
	struct addrinfo hints;
	struct addrinfo *servinfo, *p;
	struct sockaddr_storage client_addr;	// Will store the address of the client
	socklen_t sin_size;
	bool lfcon = true;	// Boolean flag for when looking for connection

	int yes = 1;	// Will be used to allow for port reuse if necessary
	
	char s[INET6_ADDRSTRLEN];
	int rv;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;	// IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM;	//tcp protocol	
	hints.ai_flags = AI_PASSIVE;	// Use my address

	if((rv = getaddrinfo(NULL, PORT, &hints, &servinfo))){
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		exit(1);
	}

	// Loop through results from getaddrinfo and bind to the first socket we can
	for(p = servinfo; p != NULL; p->ai_next){
		if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
			perror("server: socket");
			continue;
		}
		
		if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1){
			perror("setsockopt");
			exit(1);
		}

		if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1){
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	freeaddrinfo(servinfo); //Don't need server address info anymore

	if(p == NULL){
		fprintf(stderr, "server: failed to bind \n");
		exit(1);
	}

	if(listen(sockfd, BACKLOG) == -1){
		perror("listen");
		exit(1);
	}


	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
            s, sizeof s);
	printf("server: %s\n", s);

	std::unique_lock<std::mutex> lck(mtx, std::defer_lock);

	// Connect with clients until one sends a newline
	while(lfcon) {
		printf("server: waiting for connections... \n");
	
		sin_size = sizeof client_addr;
		new_fd = accept(sockfd, (struct sockaddr *) &client_addr, &sin_size);
		if (new_fd == -1){
			perror("accept");
			continue;
		}
	

	inet_ntop(client_addr.ss_family, get_in_addr((struct sockaddr *)&client_addr), s, sizeof s);
	printf("server: got connection from %s\n", s);


		//if (send(new_fd, "This is test", 12, 0) == -1)
		//	perror("send");
		//	close(new_fd);
	

	// Keep server up until client disconnects or send only a newline
	while(true){
	if((numbytes = recv(new_fd, buffe, MAXDATASIZE-1, 0)) == -1) {
		perror("recv");
		close(new_fd);
		exit(1);
	}
	
	scroll = true;
	usleep(1000);

	// Client disconnected
	if(numbytes == 0){
		printf("server: client '%s' \n", "disconnected");
		close(new_fd);
		break;
	}

	if(buffe[0] == '\n'){
		printf("server: recieved '%s' \n", "newline");
		lfcon = false;
		break;
	}

	buffe[numbytes] = '\0';
	
	lck.lock();
	strcpy(*msg, buffe);
	*msg = strtok(*msg, "\n");
	
	if(*msg == NULL){
		lfcon = false;
		break;
	}

	printf("server: recieved '%s' \n", *msg);
	lck.unlock();

	}
	}

	// Done. Close connection with client and stop the scrollling
	close(sockfd);
	close(new_fd);
	usleep(10000);
	scroll = false;
	printf("server:'%s' \n", "exiting");
}

int main(int argc, char *argv[]) {
  
	// Set up GPIO pins. This fails when not running as root.
	GPIO io;
	if (!io.Init())
		return 1;

	Color color(255, 255, 0); // Text color
	Color bcolor(0,0,0);
	Color *bgcolor = &bcolor; // Background color
	const char *bdf_font_file = "fonts/5x7.bdf"; // Font file
	int rows = 32;    // A 32x32 display. Use 16 when this is a 16x32 display.
	int chain = 1;    // Number of boards chained together.
	int parallel = 1; // Number of chains in parallel (1..3). > 1 for plus or Pi2
	int y_orig = 16;
	int speed = 5;	// Scrolling speed

	int opt;
  	while ((opt = getopt(argc, argv, "P:c:y:f:C:s:")) != -1) {
	switch (opt) {
		case 'P': parallel = atoi(optarg); break;	
		case 'c': chain = atoi(optarg); break;
		case 'y': y_orig = atoi(optarg); break;
		case 'f': bdf_font_file = strdup(optarg); break;
		case 'C':
      			if (!parseColor(&color, optarg)) {
        			fprintf(stderr, "Invalid color spec.\n");
        			return usage(argv[0]);
      			}
			break;
		case 's': speed = atoi(optarg); break;
    		default:
      			return usage(argv[0]);
		}
	}
	
	

	// Load font. This needs to be a filename with a bdf bitmap font.
  	rgb_matrix::Font font;
  	if (!font.LoadFont(bdf_font_file)) {
  		fprintf(stderr, "Couldn't load font '%s'\n", bdf_font_file);
    		return 1;
  	}
	if (chain < 1) {
    		fprintf(stderr, "Chain outside usable range\n");
    		return 1;
 	 }
  	if (chain > 8) {
    		fprintf(stderr, "That is a long chain. Expect some flicker.\n");
  	}
  	if (parallel < 1 || parallel > 3) {
    		fprintf(stderr, "Parallel outside usable range.\n");
   		 return 1;
  	}
	if (speed < 1 || speed > 10){
		fprintf(stderr, "Speed outside usable range.\n");
   		return 1;
  	}
	
	// Set up the RGBMatrix. It implements a 'Canvas' interface.
	Canvas *canvas = new RGBMatrix(&io, rows, chain, parallel);
	
	char *letters;	// Will contain message to display
	letters = (char*) malloc(MAXDATASIZE);
	char *cmsg; 	// Will store message received from client
	cmsg = (char*) malloc(MAXDATASIZE);

	std::unique_lock<std::mutex> lck(mtx, std::defer_lock);

	// Start server and get text to display from a client
	std::thread serverthread (serverCall, &cmsg);
	
	// Wait until server gets first message from client
	while(!scroll)
		usleep(2000);

	std::cout << "Working... \n";
	usleep(1000);

	int sleept = 150000.0 / speed; // Sleep time determined by speed.
	
	// Keep scrolling the text while the server is connected
	while(scroll){
	lck.lock();
	strcpy(letters, cmsg);
	printf("Displaying '%s' \n", letters);
	lck.unlock();

	usleep(10000);
	
	int move = rgb_matrix::DrawText(canvas, font, ((chain * 32) - 2), y_orig, color, bgcolor, letters); // LED lenght of text

	// Shift text to the left
	for(int i = 1; i < move + (chain * 32); i++){
		canvas->Clear();
		rgb_matrix::DrawText(canvas, font, ((chain * 32) - 2) - i, y_orig, color, bgcolor, letters);
		usleep(sleept);
		if(!scroll)
			break;
	}
	}
	
	usleep(3000);
	std::cout << "Done \n";
	canvas->Clear();
	delete canvas;

	serverthread.join();
	return 0;
}