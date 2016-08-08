/*
	Author: Rocky Tatiefo
	This file contains source code for a program which creates a temporary server and takes in a string of text from a client,
	and then display the text and scrolls it across the bottom row an led matrix. Dispays an animation on the top row.
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
#define BACKLOG 5	//Number of pending connections to be held
#define MAXDATASIZE 100	//Max number of bytes to be received at once
#define RGB_COMPONENT_COLOR 255	

using namespace rgb_matrix;

// Struct to store the rgb components of a pixel.
typedef struct {
     unsigned char red,green,blue;
} PPMPixel;

// Struct to store PPM image. 
typedef struct {
     int x, y;
     PPMPixel *data;
} PPMImage;

bool scroll = false;	//Boolean used to determine wether to scroll text
std::mutex mtx;		//Mutex for critical section

rgb_matrix::Font font1; // Font for animation text
const int width = 191;
const int height = 71;
Color color(255, 255, 0); // Text color
Color color2(0,255,255);
Color color3(250,185,176);
Color color4(255,0,255);
Color color5(255,0,0);

// Prints the usage of options
static int usage(const char *progname) {
  fprintf(stderr, "usage: %s [options]\n", progname);
  fprintf(stderr, "Options:\n"
        "\t-f <font-file>: Use given font.\n"
        "\t-C <r,g,b>    : Color. (Default 255,255,0)\n"
	"\t-s <speed>    : Scrolling speed (1-10. Default: 5) \n");
  return 1;
}

// Checks if the user entered 3 components for the color
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

// Sets up a server that recieves text from a client. 
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

// Functiom made by rfp from stackoverflow to read a PPM P6 image
static PPMImage *readPPM(const char *filename)
{
         char buff[16];
         PPMImage *img;
         FILE *fp;
         int c, rgb_comp_color;
         //open PPM file for reading
         fp = fopen(filename, "rb");
         if (!fp) {
              fprintf(stderr, "Unable to open file '%s'\n", filename);
              exit(1);
         }

         //read image format
         if (!fgets(buff, sizeof(buff), fp)) {
              perror(filename);
              exit(1);
         }

    //check the image format
    if (buff[0] != 'P' || buff[1] != '6') {
         fprintf(stderr, "Invalid image format (must be 'P6')\n");
         exit(1);
    }

    //alloc memory form image
    img = (PPMImage *)malloc(sizeof(PPMImage));
    if (!img) {
         fprintf(stderr, "Unable to allocate memory\n");
         exit(1);
    }

    //check for comments
    c = getc(fp);
    while (c == '#') {
    while (getc(fp) != '\n') ;
         c = getc(fp);
    }

    ungetc(c, fp);
    //read image size information
    if (fscanf(fp, "%d %d", &img->x, &img->y) != 2) {
         fprintf(stderr, "Invalid image size (error loading '%s')\n", filename);
         exit(1);
    }

    //read rgb component
    if (fscanf(fp, "%d", &rgb_comp_color) != 1) {
         fprintf(stderr, "Invalid rgb component (error loading '%s')\n", filename);
         exit(1);
    }

    //check rgb component depth
    if (rgb_comp_color!= RGB_COMPONENT_COLOR) {
         fprintf(stderr, "'%s' does not have 8-bits components\n", filename);
         exit(1);
    }

    while (fgetc(fp) != '\n') ;
    //memory allocation for pixel data
    img->data = (PPMPixel*)malloc(img->x * img->y * sizeof(PPMPixel));

    if (!img) {
         fprintf(stderr, "Unable to allocate memory\n");
         exit(1);
    }

    //read pixel data from file
    if (fread(img->data, 3 * img->x, img->y, fp) != img->y) {
         fprintf(stderr, "Error loading image '%s'\n", filename);
         exit(1);
    }

    fclose(fp);
    return img;
}

// Fills in the area in the led matrix from xstart to xend and from ystart to yend
// with the color r, g, b
static void fillCanvas(Canvas *canvas, int xstart, int xend, int ystart, int yend, int r, int g, int b){
	for(int i = xstart; i <= xend; i++){
		for (int j = ystart; j <= yend; j++){
			canvas->SetPixel(i,j,r,g,b);
		}
	}
}

/*
// Generic function to print a sprite to the led matrix from an image array.
// Takes in canvas for the led matrix, the image array, the x and y positions defining
// the top left edge of where the sprite will print, the sprite number, and a boolean
// which determines if the sprite will be printed normally or flipped along the y-axis.
// (x-offset,y-offset) is the location of the top left corner of the sprite in the 
// sprite sheet. width is the x-lenght of the sprite, and height is the y-lenght of the
// sprite
static void printSprite(Canvas *canvas, unsigned char img[width][height][3], int xoffset, int yoffset, int width, int height, int xpos, int ypos, bool dir){
	int sprx;
	int spry
	
	if(dir){
	for (int i = 0; i < 16; i++){
		sprx = i + xoffset;
		for(int j = 0; j < 16; j++){
			spry = j + yoffset;
			canvas->SetPixel(xpos + i, ypos+j, img[sprx][spry][0], img[sprx][spry][1], img[sprx][spry][2]);
		}
	}
	}
	else {
	for (int i = 0; i < 16; i++){
		sprx = (15 - i) + xoffset;
		for(int j = 0; j < 16; j++){
			spry = j + yoffset;
			canvas->SetPixel(xpos + i, ypos+j, img[sprx][spry][0], img[sprx][spry][1], img[sprx][spry][2]);
		}
	}
	}
}
	

*/

// Prints the basic kirby sprites to the led matrix.
// Takes in canvas for the led matrix, the image array, the x and y positions defining
// the top left edge of where the sprite will print, the sprite number, and a boolean
// which determines if the sprite will be printed normally or flipped along the y-axis.
static void printKirby(Canvas *canvas, unsigned char img[width][height][3], int xpos, int ypos, int sprnum, bool dir){
	int offset = 0;
	if(sprnum < 7 && sprnum > -1)
		offset = 16*sprnum;

	int sprx;	

	if(dir){
	for (int i = 0; i < 16; i++){
		sprx = i + offset;
		for(int j = 0; j < 16; j++){
			canvas->SetPixel(xpos + i, ypos+j, img[sprx][j][0], img[sprx][j][1], img[sprx][j][2]);
		}
	}
	}
	else {
	for (int i = 0; i < 16; i++){
		sprx = (15 - i) + offset;
		for(int j = 0; j < 16; j++){
			canvas->SetPixel(xpos + i, ypos+j, img[sprx][j][0], img[sprx][j][1], img[sprx][j][2]);
		}
	}
	}
}

// Prints the larger kirby sprites to the led matrix.
static void printKirbyBig(Canvas *canvas, unsigned char img[width][height][3], int xpos, int ypos, int sprnum, bool dir){
	int offset = 112;
	if(sprnum < 3 && sprnum > -1);
		offset += (24 * sprnum);

	int sprx;
	
	if(dir){
	for (int i = 0; i < 24; i++){
		sprx = i + offset;
		for(int j = 0; j < 24; j++){
			canvas->SetPixel(xpos + i, ypos+j, img[sprx][j][0], img[sprx][j][1], img[sprx][j][2]);
		}
	}
	}
	else {
	for (int i = 0; i < 24; i++){
		sprx = (15 - i) + offset;
		for(int j = 0; j < 24; j++){
			canvas->SetPixel(xpos + i, ypos+j, img[sprx][j][0], img[sprx][j][1], img[sprx][j][2]);
		}
	}
	}
}

// Prints the basic mario sprites to the led matrix.
static void printMario(Canvas *canvas, unsigned char img[width][height][3], int xpos, int ypos, int sprnum, bool dir){
	int yoffset = 16;
	int offset = 0;
	if(sprnum < 5 && sprnum > -1)
		offset = 16*sprnum;

	int sprx;
	int spry;	

	if(dir){
	for (int i = 0; i < 16; i++){
		sprx = i + offset;
		for(int j = 0; j < 16; j++){
			spry = j + yoffset;
			canvas->SetPixel(xpos + i, ypos+j, img[sprx][spry][0], img[sprx][spry][1], img[sprx][spry][2]);
		}
	}
	}
	else {
	for (int i = 0; i < 16; i++){
		sprx = (15 - i) + offset;
		for(int j = 0; j < 16; j++){
			spry = j + yoffset;
			canvas->SetPixel(xpos + i, ypos+j, img[sprx][spry][0], img[sprx][spry][1], img[sprx][spry][2]);
		}
	}
	}
}

// Prints the basic pacman sprites to the led matrix.
static void printPacman(Canvas *canvas, unsigned char img[width][height][3], int xpos, int ypos, int sprnum, bool dir){
	int yoffset = 32;
	int offset = 0;
	if(sprnum < 5 && sprnum > -1)
		offset = 16*sprnum;

	int sprx;
	int spry;	

	if(dir){
	for (int i = 0; i < 16; i++){
		sprx = i + offset;
		for(int j = 0; j < 16; j++){
			spry = j + yoffset;
			canvas->SetPixel(xpos + i, ypos+j, img[sprx][spry][0], img[sprx][spry][1], img[sprx][spry][2]);
		}
	}
	}
	else {
	for (int i = 0; i < 16; i++){
		sprx = (15 - i) + offset;
		for(int j = 0; j < 16; j++){
			spry = j + yoffset;
			canvas->SetPixel(xpos + i, ypos+j, img[sprx][spry][0], img[sprx][spry][1], img[sprx][spry][2]);
		}
	}
	}
}

// Prints the basic sonic sprites to the led matrix.
static void printSonic(Canvas *canvas, unsigned char img[width][height][3], int xpos, int ypos, int sprnum, bool dir){
	int yoffset = 24;
	int offset = 95;
	if(sprnum < 6 && sprnum > -1)
		offset += (16*sprnum);

	int sprx;
	int spry;	

	if(dir){
	for (int i = 0; i < 16; i++){
		sprx = i + offset;
		for(int j = 0; j < 24; j++){
			spry = j + yoffset;
			canvas->SetPixel(xpos + i, ypos+j, img[sprx][spry][0], img[sprx][spry][1], img[sprx][spry][2]);
		}
	}
	}
	else {
	for (int i = 0; i < 16; i++){
		sprx = (15 - i) + offset;
		for(int j = 0; j < 24; j++){
			spry = j + yoffset;
			canvas->SetPixel(xpos + i, ypos+j, img[sprx][spry][0], img[sprx][spry][1], img[sprx][spry][2]);
		}
	}
	}
}

// Prints the pikachu sprite to the led matrix.
static void printPikachu(Canvas *canvas, unsigned char img[width][height][3], int xpos, int ypos, bool sil){
	int yoffset = 48;
	int offset;

	if(sil){
		offset = 148;
	}
	else{
		offset = 53;
	}


	int sprx;
	int spry;
	
	for (int i = 0; i < 35; i++){
		sprx = i + offset;
		for(int j = 0; j < 22; j++){
			spry = j + yoffset;
			canvas->SetPixel(xpos + i, ypos+j, img[sprx][spry][0], img[sprx][spry][1], img[sprx][spry][2]);
		}
	}

}

// Prints the basic pokeball sprites to the led matrix.
static void printPokeball(Canvas *canvas, unsigned char img[width][height][3], int xpos, int ypos, int sprnum){
	int yoffset = 48;
	int offset = 88;
	if(sprnum < 5 && sprnum > -1)
		offset += (12*sprnum);

	int sprx;
	int spry;	
	int r;
	int g;
	int b;	

	for (int i = 0; i < 12; i++){
		sprx = i + offset;
		for(int j = 0; j < 12; j++){
			spry = j + yoffset;
			r = img[sprx][spry][0];
			g = img[sprx][spry][1];
			b = img[sprx][spry][2];
			if(r > 5 || g > 5 || b > 5)
				canvas->SetPixel(xpos + i, ypos+j, r, g, b);
		}
	}
}

// Prints the basic smoke sprites to the led matrix.
static void printSmoke(Canvas *canvas, unsigned char img[width][height][3], int xpos, int ypos, int sprnum){
	int yoffset = 48;
	int offset = 0;
	if(sprnum < 2 && sprnum > -1)
		offset += (12*sprnum);

	int sprx;
	int spry;	
	int r;
	int g;
	int b;	

	for (int i = 0; i < 16; i++){
		sprx = i + offset;
		for(int j = 0; j < 16; j++){
			spry = j + yoffset;
			r = img[sprx][spry][0];
			g = img[sprx][spry][1];
			b = img[sprx][spry][2];
			if(r > 5 || g > 5 || b > 5)
				canvas->SetPixel(xpos + i, ypos+j, r, g, b);
		}
	}
}

// Prints the larger smoke sprite to the led matrix.
static void printOtherSmoke(Canvas *canvas, unsigned char img[width][height][3], int xpos, int ypos){
	int yoffset = 48;
	int offset = 32;

	int sprx;
	int spry;	
	int r;
	int g;
	int b;	

	for (int i = 0; i < 20; i++){
		sprx = i + offset;
		for(int j = 0; j < 20; j++){
			spry = j + yoffset;
			r = img[sprx][spry][0];
			g = img[sprx][spry][1];
			b = img[sprx][spry][2];
			if(r > 5 || g > 5 || b > 5)
				canvas->SetPixel(xpos + i, ypos+j, r, g, b);
		}
	}
}

// "Plays" the animation on the led board. Takes in the canvas for the led matrix, the
// image array, and displays the frame defined by frame.
static void animate(Canvas *canvas, unsigned char img[width][height][3], int frame) {
	int move;

	// Get led board text length useful for some frames
	if(frame < 255)
		move = rgb_matrix::DrawText(canvas, font1, 200, 27, color, NULL, "TEST");
	else
		move = rgb_matrix::DrawText(canvas, font1, 34, 100, color, NULL, "IS");

	// One switch statement case for each frame
	switch(frame){
	
	//Kirby
	case 1:
	case 2:
	case 3:
	printKirby(canvas, img, 95, 15, 3, false);
	break;
	case 4:
	case 5:
	case 6:
	printKirby(canvas, img, 94, 15, 4, false);
	break;
	case 7:
	case 8:
	case 9:
	printKirby(canvas, img, 93, 15, 3, false);
	break;
	case 10:
	case 11:
	case 12:
	printKirby(canvas, img, 92, 15, 4, false);
	break;
	case 13:
	case 14:
	case 15:
	printKirby(canvas, img, 91, 15, 3, false);
	break;
	case 16:
	case 17:
	case 18:
	printKirby(canvas, img, 90, 15, 4, false);
	break;
	case 19:
	case 20:
	case 21:
	printKirby(canvas, img, 89, 15, 3, false);
	break;
	case 22:
	case 23:
	case 24:
	printKirby(canvas, img, 88, 15, 4, false);
	break;
	case 25:
	case 26:
	case 27:
	printKirby(canvas, img, 87, 15, 3, false);
	break;
	case 28:
	case 29:
	case 30:
	printKirby(canvas, img, 86, 15, 4, false);
	break;
	case 31:
	case 32:
	case 33:
	printKirby(canvas, img, 85, 15, 3, false);
	break;
	case 34:
	case 35:
	case 36:
	printKirby(canvas, img, 84, 15, 4, false);
	break;
	case 37:
	case 38:
	case 39:
	printKirby(canvas, img, 83, 15, 3, false);
	break;
	case 40:
	case 41:
	case 42:
	printKirby(canvas, img, 82, 15, 4, false);
	break;
	case 43:
	case 44:
	case 45:
	printKirby(canvas, img, 81, 15, 3, false);
	break;
	case 46:
	case 47:
	case 48:
	printKirby(canvas, img, 80, 15, 4, false);
	break;
	case 49:
	case 50:
	case 51:
	printKirby(canvas, img, 79, 15, 3, false);
	break;
	case 52:
	case 53:
	case 54:
	printKirby(canvas, img, 78, 15, 4, false);
	break;
	case 55:
	case 56:
	case 57:
	printKirby(canvas, img, 77, 15, 3, false);
	break;
	case 58:
	case 59:
	case 60:
	printKirby(canvas, img, 76, 15, 4, false);
	break;
	case 61:
	case 62:
	case 63:
	printKirby(canvas, img, 75, 15, 3, false);
	break;
	case 64:
	case 65:
	case 66:
	printKirby(canvas, img, 74, 15, 4, false);
	break;
	case 67:
	case 68:
	case 69:
	printKirby(canvas, img, 73, 15, 3, false);
	break;
	case 70:
	case 71:
	printKirby(canvas, img, 72, 15, 4, false);
	rgb_matrix::DrawText(canvas, font1, 95, 27, color, NULL, "TEST");
	break;
	case 72:
	case 73:
	printKirby(canvas, img, 72, 15, 0, false);
	rgb_matrix::DrawText(canvas, font1, 94, 27, color, NULL, "TEST");
	break;
	case 74:
	case 75:
	printKirby(canvas, img, 72, 15, 0, false);
	rgb_matrix::DrawText(canvas, font1, 93, 27, color, NULL, "TEST");
	break;
	case 76:
	printKirby(canvas, img, 72, 15, 0, false);
	rgb_matrix::DrawText(canvas, font1, 92, 27, color, NULL, "TEST");
	break;
	case 77:
	case 78:
	case 79:
	case 80:
	case 81:
	printKirby(canvas, img, 72, 15, 0, true);
	rgb_matrix::DrawText(canvas, font1, 92, 27, color, NULL, "TEST");
	break;
	case 82:
	printKirby(canvas, img, 72, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 92, 27, color, NULL, "TEST");
	break;
	case 83:
	case 84:
	printKirby(canvas, img, 71, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 91, 27, color, NULL, "TEST");
	break;
	case 85:
	case 86:
	printKirby(canvas, img, 70, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 90, 27, color, NULL, "TEST");
	break;
	case 87:
	case 88:
	printKirby(canvas, img, 69, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 89, 27, color, NULL, "TEST");
	break;
	case 89:
	case 90:
	printKirby(canvas, img, 68, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 88, 27, color, NULL, "TEST");
	break;
	case 91:
	case 92:
	printKirby(canvas, img, 67, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 87, 27, color, NULL, "TEST");
	break;
	case 93:
	case 94:
	printKirby(canvas, img, 66, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 86, 27, color, NULL, "TEST");
	break;
	case 95:
	case 96:
	printKirby(canvas, img, 65, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 85, 27, color, NULL, "TEST");
	break;
	case 97:
	case 98:
	printKirby(canvas, img, 64, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 84, 27, color, NULL, "TEST");
	break;
	case 99:
	case 100:
	printKirby(canvas, img, 63, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 83, 27, color, NULL, "TEST");
	break;
	case 101:
	case 102:
	printKirby(canvas, img, 62, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 82, 27, color, NULL, "TEST");
	break;
	case 103:
	case 104:
	printKirby(canvas, img, 61, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 81, 27, color, NULL, "TEST");
	break;
	case 105:
	case 106:
	printKirby(canvas, img, 60, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 80, 27, color, NULL, "TEST");
	break;
	case 107:
	case 108:
	printKirby(canvas, img, 59, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 79, 27, color, NULL, "TEST");
	break;
	case 109:
	case 110:
	printKirby(canvas, img, 58, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 78, 27, color, NULL, "TEST");
	break;
	case 111:
	case 112:
	printKirby(canvas, img, 57, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 77, 27, color, NULL, "TEST");
	break;
	case 113:
	case 114:
	printKirby(canvas, img, 56, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 76, 27, color, NULL, "TEST");
	break;
	case 115:
	case 116:
	printKirby(canvas, img, 55, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 75, 27, color, NULL, "TEST");
	break;
	case 117:
	case 118:
	printKirby(canvas, img, 54, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 74, 27, color, NULL, "TEST");
	break;
	case 119:
	case 120:
	printKirby(canvas, img, 53, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 73, 27, color, NULL, "TEST");
	break;
	case 121:
	case 122:
	printKirby(canvas, img, 52, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 72, 27, color, NULL, "TEST");
	break;
	case 123:
	case 124:
	printKirby(canvas, img, 51, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 71, 27, color, NULL, "TEST");
	break;
	case 125:
	case 126:
	printKirby(canvas, img, 50, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 70, 27, color, NULL, "TEST");
	break;
	case 127:
	case 128:
	printKirby(canvas, img, 49, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 69, 27, color, NULL, "TEST");
	break;
	case 129:
	case 130:
	printKirby(canvas, img, 48, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 68, 27, color, NULL, "TEST");
	break;
	case 131:
	case 132:
	printKirby(canvas, img, 47, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 67, 27, color, NULL, "TEST");
	break;
	case 133:
	case 134:
	printKirby(canvas, img, 46, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 66, 27, color, NULL, "TEST");
	break;
	case 135:
	case 136:
	printKirby(canvas, img, 45, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 65, 27, color, NULL, "TEST");
	break;
	case 137:
	case 138:
	printKirby(canvas, img, 44, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 64, 27, color, NULL, "TEST");
	break;
	case 139:
	case 140:
	printKirby(canvas, img, 43, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 63, 27, color, NULL, "TEST");
	break;
	case 141:
	case 142:
	printKirby(canvas, img, 42, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 62, 27, color, NULL, "TEST");
	break;
	case 143:
	case 144:
	printKirby(canvas, img, 41, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 61, 27, color, NULL, "TEST");
	break;
	case 145:
	case 146:
	printKirby(canvas, img, 40, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 60, 27, color, NULL, "TEST");
	break;
	case 147:
	case 148:
	printKirby(canvas, img, 39, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 59, 27, color, NULL, "TEST");
	break;
	case 149:
	case 150:
	printKirby(canvas, img, 38, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 58, 27, color, NULL, "TEST");
	break;
	case 151:
	case 152:
	printKirby(canvas, img, 37, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 57, 27, color, NULL, "TEST");
	break;
	case 153:
	case 154:
	printKirby(canvas, img, 36, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 56, 27, color, NULL, "TEST");
	break;
	case 155:
	case 156:
	printKirby(canvas, img, 35, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 55, 27, color, NULL, "TEST");
	break;
	case 157:
	case 158:
	printKirby(canvas, img, 34, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 54, 27, color, NULL, "TEST");
	break;
	case 159:
	case 160:
	printKirby(canvas, img, 33, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 53, 27, color, NULL, "TEST");
	break;
	case 161:
	case 162:
	printKirby(canvas, img, 32, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 52, 27, color, NULL, "TEST");
	break;
	case 163:
	case 164:
	printKirby(canvas, img, 31, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 51, 27, color, NULL, "TEST");
	break;
	case 165:
	case 166:
	printKirby(canvas, img, 30, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 50, 27, color, NULL, "TEST");
	break;
	case 167:
	case 168:
	printKirby(canvas, img, 29, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 49, 27, color, NULL, "TEST");
	break;
	case 169:
	case 170:
	printKirby(canvas, img, 28, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 48, 27, color, NULL, "TEST");
	break;
	case 171:
	case 172:
	printKirby(canvas, img, 27, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 47, 27, color, NULL, "TEST");
	break;
	case 173:
	case 174:
	printKirby(canvas, img, 26, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 46, 27, color, NULL, "TEST");
	break;
	case 175:
	case 176:
	printKirby(canvas, img, 25, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 45, 27, color, NULL, "TEST");
	break;
	case 177:
	case 178:
	printKirby(canvas, img, 24, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 44, 27, color, NULL, "TEST");
	break;
	case 179:
	case 180:
	printKirby(canvas, img, 23, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 43, 27, color, NULL, "TEST");
	break;
	case 181:
	case 182:
	printKirby(canvas, img, 22, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 42, 27, color, NULL, "TEST");
	break;
	case 183:
	case 184:
	printKirby(canvas, img, 21, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 41, 27, color, NULL, "TEST");
	break;
	case 185:
	case 186:
	printKirby(canvas, img, 20, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 40, 27, color, NULL, "TEST");
	break;
	case 187:
	case 188:
	printKirby(canvas, img, 19, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 39, 27, color, NULL, "TEST");
	break;
	case 189:
	case 190:
	printKirby(canvas, img, 18, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 38, 27, color, NULL, "TEST");
	break;
	case 191:
	case 192:
	printKirby(canvas, img, 17, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color, NULL, "TEST");
	break;
	case 193:
	case 194:
	printKirby(canvas, img, 16, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 36, 27, color, NULL, "TEST");
	break;
	case 195:
	case 196:
	printKirby(canvas, img, 15, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 35, 27, color, NULL, "TEST");
	break;
	case 197:
	case 198:
	printKirby(canvas, img, 14, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 34, 27, color, NULL, "TEST");
	break;
	case 199:
	case 200:
	printKirby(canvas, img, 13, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 33, 27, color, NULL, "TEST");
	break;
	case 201:
	case 202:
	printKirby(canvas, img, 12, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 32, 27, color, NULL, "TEST");
	break;
	case 203:
	case 204:
	printKirby(canvas, img, 11, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 31, 27, color, NULL, "TEST");
	break;
	case 205:
	case 206:
	printKirby(canvas, img, 10, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 30, 27, color, NULL, "TEST");
	break;
	case 207:
	case 208:
	printKirby(canvas, img, 10, 15, 4, true);
	rgb_matrix::DrawText(canvas, font1, 29, 27, color, NULL, "TEST");
	break;
	case 209:
	case 210:
	printKirby(canvas, img, 10, 15, 5, true);
	rgb_matrix::DrawText(canvas, font1, 28, 27, color, NULL, "TEST");
	break;
	case 211:
	printKirby(canvas, img, 10, 15, 5, true);
	rgb_matrix::DrawText(canvas, font1, 27, 27, color, NULL, "TEST");
	break;
	case 212:
	printKirbyBig(canvas, img, 10, 7, 0, true);
	rgb_matrix::DrawText(canvas, font1, 27, 27, color, NULL, "TEST");
	break;
	case 213:
	printKirbyBig(canvas, img, 10, 7, 0, true);
	fillCanvas(canvas, 27, (27 + move - 2), 15, 26, 255, 255, 0);
	break;
	case 214:
	printKirbyBig(canvas, img, 10, 7, 0, true);
	fillCanvas(canvas, 26, (26 + move - 4), 16, 25, 255, 255, 0);
	break;
	case 215:
	printKirbyBig(canvas, img, 10, 7, 0, true);
	fillCanvas(canvas, 26, (26 + move - 6), 17, 23, 255, 255, 0);
	break;
	case 216:
	printKirbyBig(canvas, img, 10, 7, 0, true);
	fillCanvas(canvas, 25, (25 + move - 9), 16, 22, 255, 255, 0);
	break;
	case 217:
	printKirbyBig(canvas, img, 10, 7, 0, true);
	fillCanvas(canvas, 25, (25 + move - 12), 17, 20, 255, 255, 0);
	break;
	case 218:
	printKirbyBig(canvas, img, 10, 7, 0, true);
	fillCanvas(canvas, 25, (25 + move - 15), 18, 20, 255, 255, 0);
	break;
	case 219:
	printKirbyBig(canvas, img, 10, 7, 0, true);
	fillCanvas(canvas, 25, (25 + move - 17), 19, 20, 255, 255, 0);
	break;
	case 220:
	case 221:
	case 222:
	case 223:
	case 224:
	case 225: 
	case 226:
	printKirbyBig(canvas, img, 10, 7, 1, true);
	break;
	case 227:
	printKirbyBig(canvas, img, 10, 7, 2, true);
	break;
	case 228:
	printKirbyBig(canvas, img, 10, 7, 2, true);
	fillCanvas(canvas, 25, (25 + move - 10), 19, 20, 0, 255, 255);
	break;
	case 229:
	printKirbyBig(canvas, img, 9, 7, 2, true);
	fillCanvas(canvas, 26, (26 + move - 8), 19, 21, 0, 255, 255);
	break;
	case 230:
	printKirbyBig(canvas, img, 9, 7, 2, true);
	fillCanvas(canvas, 26, (26 + move - 6), 18, 23, 0, 255, 255);
	break;
	case 231:
	printKirbyBig(canvas, img, 8, 7, 2, true);
	fillCanvas(canvas, 27, (27 + move - 4), 19, 21, 0, 255, 255);
	break;
	case 232:
	printKirbyBig(canvas, img, 7, 7, 2, true);
	fillCanvas(canvas, 27, (27 + move - 3), 18, 23, 0, 255, 255);
	break;
	case 233:
	printKirbyBig(canvas, img, 6, 7, 2, true);
	fillCanvas(canvas, 28, (28 + move - 2), 16, 24, 0, 255, 255);
	break;
	case 234:
	printKirbyBig(canvas, img, 5, 7, 2, true);
	fillCanvas(canvas, 28, (28 + move - 1), 15, 25, 0, 255, 255);
	break;
	case 235:
	printKirbyBig(canvas, img, 4, 7, 2, true);
	rgb_matrix::DrawText(canvas, font1, 29, 27, color2, NULL, "IS");
	break;
	case 236:
	printKirbyBig(canvas, img, 3, 7, 2, true);
	rgb_matrix::DrawText(canvas, font1, 29, 27, color2, NULL, "IS");
	break;
	case 237:
	printKirbyBig(canvas, img, 2, 7, 2, true);
	rgb_matrix::DrawText(canvas, font1, 30, 27, color2, NULL, "IS");
	break;
	case 238:
	printKirbyBig(canvas, img, 1, 7, 2, true);
	rgb_matrix::DrawText(canvas, font1, 30, 27, color2, NULL, "IS");
	break;
	case 239:
	printKirbyBig(canvas, img, 0, 7, 2, true);
	rgb_matrix::DrawText(canvas, font1, 31, 27, color2, NULL, "IS");
	break;
	case 240:
	printKirbyBig(canvas, img, -1, 7, 2, true);
	rgb_matrix::DrawText(canvas, font1, 31, 27, color2, NULL, "IS");
	break;
	case 241:
	printKirbyBig(canvas, img, -2, 7, 2, true);
	rgb_matrix::DrawText(canvas, font1, 32, 27, color2, NULL, "IS");
	break;
	case 242:
	printKirbyBig(canvas, img, -3, 7, 2, true);
	rgb_matrix::DrawText(canvas, font1, 33, 27, color2, NULL, "IS");
	break;
	case 243:
	printKirbyBig(canvas, img, -4, 7, 2, true);
	rgb_matrix::DrawText(canvas, font1, 33, 27, color2, NULL, "IS");
	break;
	case 244:
	printKirbyBig(canvas, img, -5, 7, 2, true);
	rgb_matrix::DrawText(canvas, font1, 34, 27, color2, NULL, "IS");
	break;
	case 245:
	printKirbyBig(canvas, img, -6, 7, 2, true);
	rgb_matrix::DrawText(canvas, font1, 34, 27, color2, NULL, "IS");
	break;
	case 246:
	printKirbyBig(canvas, img, -7, 7, 2, true);
	rgb_matrix::DrawText(canvas, font1, 35, 27, color2, NULL, "IS");
	break;
	case 247:
	printKirbyBig(canvas, img, -8, 7, 2, true);
	rgb_matrix::DrawText(canvas, font1, 35, 27, color2, NULL, "IS");
	break;
	case 248:
	printKirbyBig(canvas, img, -9, 7, 2, true);
	rgb_matrix::DrawText(canvas, font1, 36, 27, color2, NULL, "IS");
	break;
	case 249:
	printKirbyBig(canvas, img, -10, 7, 2, true);
	rgb_matrix::DrawText(canvas, font1, 36, 27, color2, NULL, "IS");
	break;
	case 250:
	printKirbyBig(canvas, img, -11, 7, 2, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 251:
	printKirbyBig(canvas, img, -12, 7, 2, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 252:
	printKirbyBig(canvas, img, -13, 7, 2, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 253:
	printKirbyBig(canvas, img, -14, 7, 2, true);
	printMario(canvas, img, 10, -15, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	
	// Mario
	case 254:
	printKirbyBig(canvas, img, -15, 7, 2, true);
	printMario(canvas, img, 10, -13, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 255:
	printKirbyBig(canvas, img, -16, 7, 2, true);
	printMario(canvas, img, 5, -12, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 256:
	printKirbyBig(canvas, img, -17, 7, 2, true);
	printMario(canvas, img, 5, -12, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 257:
	printKirbyBig(canvas, img, -18, 7, 2, true);
	printMario(canvas, img, 5, -11, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 258:
	printKirbyBig(canvas, img, -19, 7, 2, true);
	printMario(canvas, img, 5, -10, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 259:
	printKirbyBig(canvas, img, -20, 7, 2, true);
	printMario(canvas, img, 5, -9, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 260:
	printKirbyBig(canvas, img, -21, 7, 2, true);
	printMario(canvas, img, 5, -8, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 261:
	printKirbyBig(canvas, img, -22, 7, 2, true);
	printMario(canvas, img, 5, -7, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 262:
	printMario(canvas, img, 5, -6, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 263:
	printMario(canvas, img, 5, -5, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 264:
	printMario(canvas, img, 5, -4, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 265:
	printMario(canvas, img, 5, -3, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 266:
	printMario(canvas, img, 5, -2, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 267:
	printMario(canvas, img, 5, -1, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 268:
	printMario(canvas, img, 5, 0, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 269:
	printMario(canvas, img, 5, 1, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 270:
	printMario(canvas, img, 5, 2, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 271:
	printMario(canvas, img, 5, 3, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 272:
	printMario(canvas, img, 5, 4, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 273:
	printMario(canvas, img, 5, 5, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 274:
	printMario(canvas, img, 5, 6, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 275:
	printMario(canvas, img, 5, 7, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 276:
	printMario(canvas, img, 5, 8, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 277:
	printMario(canvas, img, 5, 9, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 278:
	printMario(canvas, img, 5, 10, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 279:
	printMario(canvas, img, 5, 11, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 280:
	printMario(canvas, img, 5, 12, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 281:
	printMario(canvas, img, 5, 13, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 282:
	printMario(canvas, img, 5, 14, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 283:
	printMario(canvas, img, 5, 15, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 284:
	case 285:
	case 286:
	case 287:
	case 288:
	printMario(canvas, img, 5, 15, 0, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 289:
	case 290:
	printMario(canvas, img, 5, 15, 1, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 291:
	printMario(canvas, img, 6, 15, 2, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 292:
	case 293:
	printMario(canvas, img, 7, 15, 3, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 294:
	printMario(canvas, img, 8, 15, 2, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 295:
	case 296:
	printMario(canvas, img, 9, 15, 1, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 297:
	printMario(canvas, img, 10, 15, 2, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 298:
	case 299:
	printMario(canvas, img, 11, 15, 3, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 300:
	printMario(canvas, img, 12, 15, 2, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 301:
	printMario(canvas, img, 13, 15, 1, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 302:
	printMario(canvas, img, 13, 15, 1, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 303:
	printMario(canvas, img, 14, 14, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 304:
	printMario(canvas, img, 15, 13, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 305:
	printMario(canvas, img, 15, 12, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 306:
	printMario(canvas, img, 16, 11, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 307:
	printMario(canvas, img, 17, 10, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 308:
	printMario(canvas, img, 17, 9, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 309:
	printMario(canvas, img, 18, 8, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 310:
	printMario(canvas, img, 19, 7, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 311:
	printMario(canvas, img, 19, 6, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 312:
	printMario(canvas, img, 20, 5, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 313:
	printMario(canvas, img, 21, 4, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 314:
	printMario(canvas, img, 21, 3, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 315:
	printMario(canvas, img, 22, 2, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 316:
	printMario(canvas, img, 23, 1, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 317:
	printMario(canvas, img, 23, -1, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 318:
	printMario(canvas, img, 24, -2, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 319:
	printMario(canvas, img, 25, -3, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 320:
	printMario(canvas, img, 25, -4, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 321:
	printMario(canvas, img, 26, -5, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 322:
	case 323:
	printMario(canvas, img, 27, -6, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 324:
	printMario(canvas, img, 28, -7, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 325:
	case 326:
	printMario(canvas, img, 29, -7, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 327:
	printMario(canvas, img, 30, -6, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 328:
	printMario(canvas, img, 31, -5, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 329:
	printMario(canvas, img, 31, -4, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 330:
	printMario(canvas, img, 32, -3, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 331:
	printMario(canvas, img, 33, -2, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 332:
	printMario(canvas, img, 33, -1, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 333:
	case 334:
	printMario(canvas, img, 34, 0, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 335:
	printMario(canvas, img, 35, -1, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 26, color2, NULL, "IS");
	break;
	case 336:
	printMario(canvas, img, 35, -2, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 25, color2, NULL, "IS");
	break;
	case 337:
	printMario(canvas, img, 36, -3, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 26, color2, NULL, "IS");
	break;
	case 338:
	printMario(canvas, img, 37, -4, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 27, color2, NULL, "IS");
	break;
	case 339:
	printMario(canvas, img, 37, -5, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 28, color2, NULL, "IS");
	break;
	case 340:
	printMario(canvas, img, 38, -6, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 29, color2, NULL, "IS");
	break;
	case 341:
	printMario(canvas, img, 39, -8, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 30, color2, NULL, "IS");
	break;
	case 342:
	printMario(canvas, img, 39, -9, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color2, NULL, "IS");
	break;
	case 343:
	printMario(canvas, img, 40, -10, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color2, NULL, "IS");
	break;
	case 344:
	printMario(canvas, img, 41, -11, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color2, NULL, "IS");
	break;
	case 345:
	printMario(canvas, img, 41, -12, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color2, NULL, "IS");
	break;
	case 346:
	printMario(canvas, img, 42, -13, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color2, NULL, "IS");
	break;
	case 347:
	printMario(canvas, img, 43, -14, 4, true);
	move = rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 348:
	printMario(canvas, img, 43, -15, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 349:
	printMario(canvas, img, 43, -15, 4, true);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 350:
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 351:
	case 352:
	case 353:
	case 354:
	rgb_matrix::DrawText(canvas, font1, 37, 31, color2, NULL, "IS");
	break;
	case 355:
	case 356:
	case 357:
	case 358:
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 359:
	case 360:
	case 361:
	case 362:
	rgb_matrix::DrawText(canvas, font1, 37, 31, color2, NULL, "IS");
	break;
	case 363:
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 364:

	// Pacman
	case 365:
	printPacman(canvas, img, 93, 15, 0, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 366:
	printPacman(canvas, img, 92, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 367:
	case 368:
	printPacman(canvas, img, 91, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 369:
	printPacman(canvas, img, 90, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 370:
	case 371:
	printPacman(canvas, img, 89, 15, 0, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 372:
	printPacman(canvas, img, 88, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 373:
	case 374:
	printPacman(canvas, img, 87, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 375:
	printPacman(canvas, img, 86, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 376:
	case 377:
	printPacman(canvas, img, 85, 15, 0, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 378:
	printPacman(canvas, img, 84, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 379:
	case 380:
	printPacman(canvas, img, 83, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 381:
	printPacman(canvas, img, 82, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 382:
	case 383:
	printPacman(canvas, img, 81, 15, 0, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 384:
	printPacman(canvas, img, 80, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 385:
	case 386:
	printPacman(canvas, img, 79, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 387:
	printPacman(canvas, img, 78, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 388:
	case 389:
	printPacman(canvas, img, 77, 15, 0, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 390:
	printPacman(canvas, img, 76, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 391:
	case 392:
	printPacman(canvas, img, 75, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 393:
	printPacman(canvas, img, 74, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 394:
	case 395:
	printPacman(canvas, img, 73, 15, 0, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 396:
	printPacman(canvas, img, 72, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 397:
	case 398:
	printPacman(canvas, img, 71, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 399:
	printPacman(canvas, img, 70, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 400:
	case 401:
	printPacman(canvas, img, 69, 15, 0, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 402:
	printPacman(canvas, img, 68, 15, 1, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 403:
	case 404:
	printPacman(canvas, img, 67, 15, 2, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 405:
	printPacman(canvas, img, 66, 15, 1, false);
	printPacman(canvas, img, 94, 15, 3, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 406:
	printPacman(canvas, img, 65, 15, 0, false);
	printPacman(canvas, img, 93, 15, 3, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 407:
	printPacman(canvas, img, 65, 15, 0, false);
	printPacman(canvas, img, 92, 15, 3, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 408:
	printPacman(canvas, img, 64, 15, 1, false);
	printPacman(canvas, img, 91, 15, 3, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 409:
	printPacman(canvas, img, 63, 15, 2, false);
	printPacman(canvas, img, 90, 15, 3, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 410:
	printPacman(canvas, img, 63, 15, 2, false);
	printPacman(canvas, img, 89, 15, 3, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 411:
	printPacman(canvas, img, 62, 15, 1, false);
	printPacman(canvas, img, 88, 15, 3, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 412:
	printPacman(canvas, img, 61, 15, 0, false);
	printPacman(canvas, img, 87, 15, 3, false);
	rgb_matrix::DrawText(canvas, font1, 37, 31, color3, NULL, "IN");
	break;
	case 413:
	printPacman(canvas, img, 61, 15, 0, false);
	printPacman(canvas, img, 86, 15, 3, false);
	fillCanvas(canvas, 38, 37 + move - 1, 22, 30, 250, 185, 176);
	break;
	case 414:
	printPacman(canvas, img, 60, 15, 1, false);
	printPacman(canvas, img, 85, 15, 3, false);
	fillCanvas(canvas, 40, 37 + move - 3, 21, 28, 250, 185, 176);
	break;
	case 415:
	printPacman(canvas, img, 59, 15, 2, false);
	printPacman(canvas, img, 84, 15, 3, false);
	fillCanvas(canvas, 42, 39 + move - 5, 20, 26, 250, 185, 176);
	break;
	case 416:
	printPacman(canvas, img, 59, 15, 2, false);
	printPacman(canvas, img, 83, 15, 3, false);
	fillCanvas(canvas, 44, 40 + move - 6, 19, 25, 250, 185, 176);
	break;
	case 417:
	printPacman(canvas, img, 58, 15, 1, false);
	printPacman(canvas, img, 82, 15, 3, false);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 4, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 3, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 2, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 1, color3);
	break;
	case 418:
	printPacman(canvas, img, 57, 15, 0, false);
	printPacman(canvas, img, 81, 15, 3, false);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 4, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 3, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 2, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 1, color3);
	break;
	case 419:
	printPacman(canvas, img, 57, 15, 0, false);
	printPacman(canvas, img, 80, 15, 3, false);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 4, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 3, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 2, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 1, color3);
	break;
	case 420:
	printPacman(canvas, img, 56, 15, 1, false);
	printPacman(canvas, img, 79, 15, 3, false);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 4, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 3, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 2, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 1, color3);
	break;
	case 421:
	printPacman(canvas, img, 55, 15, 2, false);
	printPacman(canvas, img, 78, 15, 3, false);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 4, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 3, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 2, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 1, color3);
	break;
	case 422:
	printPacman(canvas, img, 55, 15, 2, false);
	printPacman(canvas, img, 77, 15, 3, false);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 4, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 3, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 2, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 1, color3);
	break;
	case 423:
	printPacman(canvas, img, 54, 15, 1, false);
	printPacman(canvas, img, 76, 15, 3, false);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 4, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 3, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 2, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 1, color3);
	break;
	case 424:
	printPacman(canvas, img, 53, 15, 0, false);
	printPacman(canvas, img, 75, 15, 3, false);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 3, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 2, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 1, color3);
	break;
	case 425:
	printPacman(canvas, img, 53, 15, 0, false);
	printPacman(canvas, img, 74, 15, 3, false);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 3, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 2, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 1, color3);
	break;
	case 426:
	printPacman(canvas, img, 52, 15, 1, false);
	printPacman(canvas, img, 73, 15, 3, false);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 3, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 2, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 1, color3);
	break;
	case 427:
	printPacman(canvas, img, 51, 15, 2, false);
	printPacman(canvas, img, 72, 15, 3, false);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 3, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 2, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 1, color3);
	break;
	case 428:
	printPacman(canvas, img, 51, 15, 2, false);
	printPacman(canvas, img, 71, 15, 3, false);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 3, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 2, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 1, color3);
	break;
	case 429:
	printPacman(canvas, img, 50, 15, 1, false);
	printPacman(canvas, img, 70, 15, 3, false);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 3, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 2, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 1, color3);
	break;
	case 430:
	printPacman(canvas, img, 49, 15, 0, false);
	printPacman(canvas, img, 69, 15, 3, false);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 3, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 2, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 1, color3);
	break;
	case 431:
	printPacman(canvas, img, 49, 15, 0, false);
	printPacman(canvas, img, 68, 15, 3, false);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 3, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 2, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 1, color3);
	break;
	case 432:
	printPacman(canvas, img, 48, 15, 1, false);
	printPacman(canvas, img, 67, 15, 3, false);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 3, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 2, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 1, color3);
	break;
	case 433:
	printPacman(canvas, img, 47, 15, 2, false);
	printPacman(canvas, img, 65, 15, 3, false);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 3, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 2, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 1, color3);
	break;
	case 434:
	printPacman(canvas, img, 47, 15, 2, false);
	printPacman(canvas, img, 64, 15, 3, false);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 3, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 2, color3);
	rgb_matrix::DrawCircle(canvas, (74 + move) / 2, 21, 1, color3);
	break;
	case 435:
	case 436:
	case 437:
	case 438:
	case 439:
	case 440:
	printPacman(canvas, img, 46, 15, 1, false);
	printPacman(canvas, img, 64, 15, 3, false);
	break;
	case 441:
	case 442:
	case 443:
	case 444:
	case 445:
	case 446:
	printPacman(canvas, img, 46, 15, 1, false);
	printPacman(canvas, img, 64, 15, 4, true);
	break;
	case 447:
	case 448:
	case 449:
	printPacman(canvas, img, 47, 15, 0, true);
	printPacman(canvas, img, 65, 15, 4, false);
	break;
	case 450:
	printPacman(canvas, img, 48, 15, 1, true);
	printPacman(canvas, img, 66, 15, 4, false);
	break;
	case 451:
	printPacman(canvas, img, 49, 15, 0, true);
	printPacman(canvas, img, 66, 15, 4, false);
	break;
	case 452:
	printPacman(canvas, img, 49, 15, 0, true);
	printPacman(canvas, img, 67, 15, 4, false);
	break;
	case 453:
	printPacman(canvas, img, 50, 15, 1, true);
	printPacman(canvas, img, 67, 15, 4, false);
	break;
	case 454:
	printPacman(canvas, img, 51, 15, 2, true);
	printPacman(canvas, img, 68, 15, 4, false);
	break;
	case 455:
	printPacman(canvas, img, 51, 15, 2, true);
	printPacman(canvas, img, 69, 15, 4, false);
	break;
	case 456:
	printPacman(canvas, img, 52, 15, 1, true);
	printPacman(canvas, img, 69, 15, 4, false);
	break;
	case 457:
	printPacman(canvas, img, 53, 15, 0, true);
	printPacman(canvas, img, 70, 15, 4, false);
	break;
	case 458:
	case 459:
	printPacman(canvas, img, 54, 15, 1, true);
	printPacman(canvas, img, 71, 15, 4, false);
	break;
	case 460:
	printPacman(canvas, img, 55, 15, 2, true);
	printPacman(canvas, img, 72, 15, 4, false);
	break;
	case 461:
	printPacman(canvas, img, 55, 15, 2, true);
	printPacman(canvas, img, 72, 15, 4, false);
	break;
	case 462:
	printPacman(canvas, img, 56, 15, 1, true);
	printPacman(canvas, img, 73, 15, 4, false);
	break;
	case 463:
	printPacman(canvas, img, 57, 15, 0, true);
	printPacman(canvas, img, 73, 15, 4, false);
	break;
	case 464:
	printPacman(canvas, img, 57, 15, 0, true);
	printPacman(canvas, img, 74, 15, 4, false);
	break;
	case 465:
	printPacman(canvas, img, 58, 15, 1, true);
	printPacman(canvas, img, 75, 15, 4, false);
	break;
	case 466:
	printPacman(canvas, img, 59, 15, 2, true);
	printPacman(canvas, img, 75, 15, 4, false);
	break;
	case 467:
	printPacman(canvas, img, 59, 15, 2, true);
	printPacman(canvas, img, 76, 15, 4, false);
	break;
	case 468:
	printPacman(canvas, img, 60, 15, 1, true);
	printPacman(canvas, img, 76, 15, 4, false);
	break;
	case 469:
	printPacman(canvas, img, 61, 15, 0, true);
	printPacman(canvas, img, 77, 15, 4, false);
	break;
	case 470:
	printPacman(canvas, img, 61, 15, 0, true);
	printPacman(canvas, img, 78, 15, 4, false);
	break;
	case 471:
	printPacman(canvas, img, 62, 15, 1, true);
	printPacman(canvas, img, 78, 15, 4, false);
	break;
	case 472:
	case 473:
	printPacman(canvas, img, 63, 15, 2, true);
	printPacman(canvas, img, 79, 15, 4, false);
	break;
	case 474:
	printPacman(canvas, img, 64, 15, 1, true);
	printPacman(canvas, img, 80, 15, 4, false);
	break;
	case 475:
	case 476:
	printPacman(canvas, img, 65, 15, 0, true);
	printPacman(canvas, img, 81, 15, 4, false);
	break;
	case 477:
	printPacman(canvas, img, 66, 15, 1, true);
	printPacman(canvas, img, 82, 15, 4, false);
	break;
	case 478:
	printPacman(canvas, img, 67, 15, 2, true);
	printPacman(canvas, img, 82, 15, 4, false);
	break;
	case 479:
	printPacman(canvas, img, 67, 15, 2, true);
	printPacman(canvas, img, 83, 15, 4, false);
	break;
	case 480:
	case 481:
	printPacman(canvas, img, 69, 15, 0, true);
	printPacman(canvas, img, 84, 15, 4, false);
	break;
	case 482:
	printPacman(canvas, img, 69, 15, 0, true);
	printPacman(canvas, img, 85, 15, 4, false);
	break;
	case 483:
	printPacman(canvas, img, 70, 15, 1, true);
	printPacman(canvas, img, 85, 15, 4, false);
	break;
	case 484:
	printPacman(canvas, img, 71, 15, 2, true);
	printPacman(canvas, img, 86, 15, 4, false);
	break;
	case 485:
	printPacman(canvas, img, 71, 15, 2, true);
	printPacman(canvas, img, 87, 15, 4, false);
	break;
	case 486:
	printPacman(canvas, img, 72, 15, 1, true);
	printPacman(canvas, img, 87, 15, 4, false);
	break;
	case 487:
	case 488:
	printPacman(canvas, img, 73, 15, 0, true);
	printPacman(canvas, img, 88, 15, 4, false);
	break;
	case 489:
	printPacman(canvas, img, 74, 15, 1, true);
	printPacman(canvas, img, 89, 15, 4, false);
	break;
	case 490:
	case 491:
	printPacman(canvas, img, 75, 15, 2, true);
	printPacman(canvas, img, 90, 15, 4, false);
	break;
	case 492:
	printPacman(canvas, img, 76, 15, 1, true);
	printPacman(canvas, img, 91, 15, 4, false);
	break;
	case 493:
	printPacman(canvas, img, 77, 15, 0, true);
	printPacman(canvas, img, 91, 15, 4, false);
	break;
	case 494:
	printPacman(canvas, img, 77, 15, 0, true);
	printPacman(canvas, img, 92, 15, 4, false);
	break;
	case 495:
	printPacman(canvas, img, 78, 15, 1, true);
	printPacman(canvas, img, 93, 15, 4, false);
	break;
	case 496:
	printPacman(canvas, img, 79, 15, 2, true);
	printPacman(canvas, img, 93, 15, 4, false);
	break;
	case 497:
	printPacman(canvas, img, 79, 15, 2, true);
	printPacman(canvas, img, 94, 15, 4, false);
	break;
	case 498:
	printPacman(canvas, img, 80, 15, 1, true);
	printPacman(canvas, img, 94, 15, 4, false);
	break;
	case 499:
	case 500:
	printPacman(canvas, img, 81, 15, 0, true);
	break;
	case 501:
	printPacman(canvas, img, 82, 15, 1, true);
	break;
	case 502:
	case 503:
	printPacman(canvas, img, 83, 15, 2, true);
	break;
	case 504:
	printPacman(canvas, img, 84, 15, 1, true);
	break;
	case 505:
	case 506:
	printPacman(canvas, img, 85, 15, 0, true);
	break;
	case 507:
	printPacman(canvas, img, 86, 15, 1, true);
	break;
	case 508:
	case 509:
	printPacman(canvas, img, 87, 15, 2, true);
	break;
	case 510:
	printPacman(canvas, img, 88, 15, 1, true);
	break;
	case 511:
	case 512:
	printPacman(canvas, img, 89, 15, 0, true);
	break;
	case 513:
	printPacman(canvas, img, 90, 15, 1, true);
	break;
	case 514:
	case 515:
	printPacman(canvas, img, 91, 15, 2, true);
	break;
	case 516:
	printPacman(canvas, img, 92, 15, 1, true);
	break;
	case 517:
	case 518:
	printPacman(canvas, img, 93, 15, 0, true);
	break;
	case 519:
	printPacman(canvas, img, 94, 15, 1, true);
	break;
	case 520:
	printPacman(canvas, img, 95, 15, 2, true);
	break;

	// Sonic
	case 521:
	printSonic(canvas, img, -13, 7, 0, true);
	break;
	case 522:
	printSonic(canvas, img, -12, 7, 1, true);
	break;
	case 523:
	printSonic(canvas, img, -10, 7, 2, true);
	break;
	case 524:
	printSonic(canvas, img, -9, 7, 0, true);
	break;
	case 525:
	printSonic(canvas, img, -7, 7, 1, true);
	break;
	case 526:
	printSonic(canvas, img, -6, 7, 2, true);
	break;
	case 527:
	printSonic(canvas, img, -4, 7, 0, true);
	break;
	case 528:
	printSonic(canvas, img, -3, 7, 1, true);
	break;
	case 529:
	printSonic(canvas, img, -1, 7, 2, true);
	break;
	case 530:
	printSonic(canvas, img, 0, 7, 0, true);
	break;
	case 531:
	printSonic(canvas, img, 2, 7, 1, true);
	break;
	case 532:
	printSonic(canvas, img, 3, 7, 2, true);
	break;
	case 533:
	printSonic(canvas, img, 5, 7, 0, true);
	break;
	case 534:
	printSonic(canvas, img, 6, 7, 1, true);
	break;
	case 535:
	printSonic(canvas, img, 8, 7, 2, true);
	break;
	case 536:
	printSonic(canvas, img, 9, 7, 0, true);
	break;
	case 537:
	printSonic(canvas, img, 11, 7, 1, true);
	break;
	case 538:
	printSonic(canvas, img, 12, 7, 2, true);
	break;
	case 539:
	printSonic(canvas, img, 14, 7, 0, true);
	break;
	case 540:
	printSonic(canvas, img, 15, 7, 1, true);
	break;
	case 541:
	printSonic(canvas, img, 17, 7, 2, true);
	break;
	case 542:
	printSonic(canvas, img, 18, 7, 0, true);
	break;
	case 543:
	printSonic(canvas, img, 20, 7, 1, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	break;
	case 544:
	printSonic(canvas, img, 21, 7, 2, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	break;
	case 545:
	printSonic(canvas, img, 23, 7, 0, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	break;
	case 546:
	printSonic(canvas, img, 24, 7, 1, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	break;
	case 547:
	printSonic(canvas, img, 25, 7, 2, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	break;
	case 548:
	printSonic(canvas, img, 25, 7, 0, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	break;
	case 549:
	printSonic(canvas, img, 28, 7, 1, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	break;
	case 550:
	printSonic(canvas, img, 30, 7, 2, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	break;
	case 551:
	printSonic(canvas, img, 31, 7, 0, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	break;
	case 552:
	printSonic(canvas, img, 33, 7, 1, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	break;
	case 553:
	printSonic(canvas, img, 34, 7, 2, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	break;
	case 554:
	printSonic(canvas, img, 36, 7, 0, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	break;
	case 555:
	printSonic(canvas, img, 37, 7, 1, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	break;
	case 556:
	printSonic(canvas, img, 39, 7, 2, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	break;
	case 557:
	printSonic(canvas, img, 40, 7, 0, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	break;
	case 558:
	printSonic(canvas, img, 41, 7, 1, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	break;
	case 559:
	printSonic(canvas, img, 42, 7, 2, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	break;
	case 560:
	printSonic(canvas, img, 44, 7, 0, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	break;
	case 561:
	printSonic(canvas, img, 45, 7, 1, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	break;
	case 562:
	printSonic(canvas, img, 47, 7, 2, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	break;
	case 563:
	printSonic(canvas, img, 48, 7, 0, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	break;
	case 564:
	printSonic(canvas, img, 49, 7, 1, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	break;
	case 565:
	printSonic(canvas, img, 51, 7, 2, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	break;
	case 566:
	printSonic(canvas, img, 52, 7, 0, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	break;
	case 567:
	printSonic(canvas, img, 54, 7, 1, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	break;
	case 568:
	printSonic(canvas, img, 55, 7, 2, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	break;
	case 569:
	printSonic(canvas, img, 57, 7, 0, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	break;
	case 570:
	printSonic(canvas, img, 58, 7, 1, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	break;
	case 571:
	printSonic(canvas, img, 60, 7, 2, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	break;
	case 572:
	printSonic(canvas, img, 61, 7, 0, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	break;
	case 573:
	printSonic(canvas, img, 63, 7, 1, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	break;
	case 574:
	printSonic(canvas, img, 64, 7, 3, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	break;
	case 575:
	printSonic(canvas, img, 66, 7, 4, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	break;
	case 576:
	printSonic(canvas, img, 68, 7, 5, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	break;
	case 577:
	printSonic(canvas, img, 70, 7, 3, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	break;
	case 578:
	printSonic(canvas, img, 72, 7, 4, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	break;
	case 579:
	printSonic(canvas, img, 74, 7, 5, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	break;
	case 580:
	printSonic(canvas, img, 76, 7, 3, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	break;
	case 581:
	printSonic(canvas, img, 78, 7, 4, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	break;
	case 582:
	printSonic(canvas, img, 80, 7, 5, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	break;
	case 583:
	printSonic(canvas, img, 82, 7, 3, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	break;
	case 584:
	printSonic(canvas, img, 84, 7, 4, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	font1.DrawGlyph(canvas, 76, 27, color4, NULL, 'S');
	break;
	case 585:
	printSonic(canvas, img, 86, 7, 5, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	font1.DrawGlyph(canvas, 76, 27, color4, NULL, 'S');
	break;
	case 586:
	printSonic(canvas, img, 88, 7, 3, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	font1.DrawGlyph(canvas, 76, 27, color4, NULL, 'S');
	break;
	case 587:
	printSonic(canvas, img, 90, 7, 4, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	font1.DrawGlyph(canvas, 76, 27, color4, NULL, 'S');
	break;
	case 588:
	printSonic(canvas, img, 92, 7, 5, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	font1.DrawGlyph(canvas, 76, 27, color4, NULL, 'S');
	break;
	case 589:
	printSonic(canvas, img, 94, 7, 3, true);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	font1.DrawGlyph(canvas, 76, 27, color4, NULL, 'S');
	break;
	case 590:
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	font1.DrawGlyph(canvas, 76, 27, color4, NULL, 'S');
	break;
	case 591:
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	font1.DrawGlyph(canvas, 76, 27, color4, NULL, 'S');
	break;
	case 592:
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	font1.DrawGlyph(canvas, 76, 27, color4, NULL, 'S');
	break;
	case 593:
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	font1.DrawGlyph(canvas, 76, 27, color4, NULL, 'S');
	break;
	case 594:
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	font1.DrawGlyph(canvas, 76, 27, color4, NULL, 'S');
	break;
	case 595:
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	font1.DrawGlyph(canvas, 76, 27, color4, NULL, 'S');
	break;
	case 596:
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	font1.DrawGlyph(canvas, 76, 27, color4, NULL, 'S');
	break;
	case 597:
	printSonic(canvas, img, 94, 7, 3, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	font1.DrawGlyph(canvas, 76, 27, color4, NULL, 'S');
	break;
	case 598:
	printSonic(canvas, img, 92, 7, 4, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	font1.DrawGlyph(canvas, 76, 27, color4, NULL, 'S');
	break;
	case 599:
	printSonic(canvas, img, 90, 7, 5, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	font1.DrawGlyph(canvas, 76, 27, color4, NULL, 'S');
	break;
	case 600:
	printSonic(canvas, img, 88, 7, 3, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	font1.DrawGlyph(canvas, 76, 27, color4, NULL, 'S');
	break;
	case 601:
	printSonic(canvas, img, 86, 7, 4, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	font1.DrawGlyph(canvas, 76, 27, color4, NULL, 'S');
	break;
	case 602:
	printSonic(canvas, img, 84, 7, 5, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	font1.DrawGlyph(canvas, 76, 27, color4, NULL, 'S');
	break;
	case 603:
	printSonic(canvas, img, 82, 7, 3, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	break;
	case 604:
	printSonic(canvas, img, 80, 7, 4, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	break;
	case 605:
	printSonic(canvas, img, 78, 7, 5, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	break;
	case 606:
	printSonic(canvas, img, 76, 7, 3, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	font1.DrawGlyph(canvas, 67, 27, color4, NULL, 'S');
	break;
	case 607:
	printSonic(canvas, img, 74, 7, 4, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	break;
	case 608:
	printSonic(canvas, img, 72, 7, 5, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	break;
	case 609:
	printSonic(canvas, img, 70, 7, 3, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	break;
	case 610:
	printSonic(canvas, img, 68, 7, 4, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	break;
	case 611:
	printSonic(canvas, img, 66, 7, 5, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 58, 27, color4, NULL, 'E');
	break;
	case 612:
	printSonic(canvas, img, 64, 7, 3, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	break;
	case 613:
	printSonic(canvas, img, 62, 7, 4, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	break;
	case 614:
	printSonic(canvas, img, 60, 7, 5, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	break;
	case 615:
	printSonic(canvas, img, 58, 7, 3, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	break;
	case 616:
	printSonic(canvas, img, 56, 7, 4, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	font1.DrawGlyph(canvas, 49, 27, color4, NULL, 'R');
	break;
	case 617:
	printSonic(canvas, img, 54, 7, 3, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	break;
	case 618:
	printSonic(canvas, img, 52, 7, 4, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	break;
	case 619:
	printSonic(canvas, img, 50, 7, 5, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	break;
	case 620:
	printSonic(canvas, img, 48, 7, 3, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	font1.DrawGlyph(canvas, 40, 27, color4, NULL, 'G');
	break;
	case 621:
	printSonic(canvas, img, 46, 7, 4, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	break;
	case 622:
	printSonic(canvas, img, 44, 7, 5, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	break;
	case 623:
	printSonic(canvas, img, 42, 7, 3, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	break;
	case 624:
	printSonic(canvas, img, 40, 7, 4, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	break;
	case 625:
	printSonic(canvas, img, 38, 7, 5, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	font1.DrawGlyph(canvas, 31, 27, color4, NULL, 'O');
	break;
	case 626:
	printSonic(canvas, img, 36, 7, 3, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	break;
	case 627:
	printSonic(canvas, img, 34, 7, 4, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	break;
	case 628:
	printSonic(canvas, img, 32, 7, 5, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	break;
	case 629:
	printSonic(canvas, img, 30, 7, 3, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	font1.DrawGlyph(canvas, 22, 27, color4, NULL, 'R');
	break;
	case 630:
	printSonic(canvas, img, 28, 7, 4, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	break;
	case 631:
	printSonic(canvas, img, 26, 7, 5, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	break;
	case 632:
	printSonic(canvas, img, 24, 7, 3, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	break;
	case 633:
	printSonic(canvas, img, 22, 7, 4, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	break;
	case 634:
	printSonic(canvas, img, 20, 7, 5, false);
	font1.DrawGlyph(canvas, 13, 27, color4, NULL, 'P');
	break;
	case 635:
	printSonic(canvas, img, 18, 7, 3, false);
	break;
	case 636:
	printSonic(canvas, img, 16, 7, 4, false);
	break;
	case 637:
	printSonic(canvas, img, 14, 7, 5, false);
	break;
	case 638:
	printSonic(canvas, img, 12, 7, 3, false);
	break;
	case 639:
	printSonic(canvas, img, 10, 7, 4, false);
	break;
	case 640:
	printSonic(canvas, img, 8, 7, 5, false);
	break;
	case 641:
	printSonic(canvas, img, 6, 7, 3, false);
	break;
	case 642:
	printSonic(canvas, img, 4, 7, 4, false);
	break;
	case 643:
	printSonic(canvas, img, 2, 7, 5, false);
	break;
	case 644:
	printSonic(canvas, img, 0, 7, 3, false);
	break;
	case 645:
	printSonic(canvas, img, -2, 7, 4, false);
	break;
	case 646:
	printSonic(canvas, img, -4, 7, 5, false);
	break;
	case 647:
	printSonic(canvas, img, -6, 7, 3, false);
	break;
	case 648:
	printSonic(canvas, img, -8, 7, 4, false);
	break;
	case 649:
	printSonic(canvas, img, -10, 7, 5, false);
	break;
	case 650:
	printSonic(canvas, img, -12, 7, 3, false);
	break;
	case 651:
	printSonic(canvas, img, -14, 7, 4, false);
	break;
	case 652:
	break;
	case 653:
	break;

	// Pokemon
	case 654:
	printPikachu(canvas, img, 95, 10, true);
	break;
	case 655:
	printPikachu(canvas, img, 94, 10, true);
	break;
	case 656:
	printPikachu(canvas, img, 93, 10, true);
	break;
	case 657:
	printPikachu(canvas, img, 92, 10, true);
	break;
	case 658:
	printPikachu(canvas, img, 91, 10, true);
	break;
	case 659:
	printPikachu(canvas, img, 90, 10, true);
	break;
	case 660:
	printPikachu(canvas, img, 89, 10, true);
	break;
	case 661:
	printPikachu(canvas, img, 88, 10, true);
	break;
	case 662:
	printPikachu(canvas, img, 87, 10, true);
	break;
	case 663:
	printPikachu(canvas, img, 86, 10, true);
	break;
	case 664:
	printPikachu(canvas, img, 85, 10, true);
	break;
	case 665:
	printPikachu(canvas, img, 84, 10, true);
	break;
	case 666:
	printPikachu(canvas, img, 83, 10, true);
	break;
	case 667:
	printPikachu(canvas, img, 82, 10, true);
	break;
	case 668:
	printPikachu(canvas, img, 81, 10, true);
	break;
	case 669:
	printPikachu(canvas, img, 80, 10, true);
	break;
	case 670:
	printPikachu(canvas, img, 79, 10, true);
	break;
	case 671:
	printPikachu(canvas, img, 78, 10, true);
	break;
	case 672:
	printPikachu(canvas, img, 77, 10, true);
	break;
	case 673:
	printPikachu(canvas, img, 76, 10, true);
	break;
	case 674:
	printPikachu(canvas, img, 75, 10, true);
	break;
	case 675:
	printPikachu(canvas, img, 74, 10, true);
	break;
	case 676:
	printPikachu(canvas, img, 73, 10, true);
	break;
	case 677:
	printPikachu(canvas, img, 72, 10, true);
	break;
	case 678:
	printPikachu(canvas, img, 71, 10, true);
	break;
	case 679:
	printPikachu(canvas, img, 70, 10, true);
	break;
	case 680:
	printPikachu(canvas, img, 69, 10, true);
	break;
	case 681:
	printPikachu(canvas, img, 68, 10, true);
	break;
	case 682:
	printPikachu(canvas, img, 67, 10, true);
	break;
	case 683:
	printPikachu(canvas, img, 66, 10, true);
	break;
	case 684:
	printPikachu(canvas, img, 65, 10, true);
	break;
	case 685:
	printPikachu(canvas, img, 64, 10, true);
	break;
	case 686:
	printPikachu(canvas, img, 63, 10, true);
	break;
	case 687:
	printPikachu(canvas, img, 62, 10, true);
	break;
	case 688:
	printPikachu(canvas, img, 61, 10, true);
	break;
	case 689:
	printPikachu(canvas, img, 60, 10, true);
	break;
	case 690:
	printPikachu(canvas, img, 59, 10, true);
	break;
	case 691:
	printPikachu(canvas, img, 58, 10, true);
	break;
	case 692:
	printPikachu(canvas, img, 57, 10, true);
	break;
	case 693:
	printPikachu(canvas, img, 56, 10, true);
	break;
	case 694:
	printPikachu(canvas, img, 55, 10, true);
	break;
	case 695:
	printPikachu(canvas, img, 54, 10, true);
	break;
	case 696:
	printPikachu(canvas, img, 53, 10, true);
	break;
	case 697:
	printPikachu(canvas, img, 52, 10, true);
	break;
	case 698:
	printPikachu(canvas, img, 51, 10, true);
	break;
	case 699:
	printPikachu(canvas, img, 50, 10, true);
	break;
	case 700:
	printPikachu(canvas, img, 49, 10, true);
	break;
	case 701:
	printPikachu(canvas, img, 48, 10, true);
	break;
	case 702:
	printPikachu(canvas, img, 47, 10, true);
	break;
	case 703:
	printPikachu(canvas, img, 46, 10, true);
	break;
	case 704:
	printPikachu(canvas, img, 45, 10, true);
	break;
	case 705:
	printPikachu(canvas, img, 44, 10, true);
	break;
	case 706:
	printPikachu(canvas, img, 43, 10, true);
	break;
	case 707:
	printPikachu(canvas, img, 42, 10, true);
	break;
	case 708:
	printPikachu(canvas, img, 41, 10, true);
	break;
	case 709:
	printPikachu(canvas, img, 40, 10, true);
	break;
	case 710:
	printPikachu(canvas, img, 39, 10, true);
	break;
	case 711:
	printPikachu(canvas, img, 38, 10, true);
	break;
	case 712:
	printPikachu(canvas, img, 37, 10, true);
	break;
	case 713:
	printPikachu(canvas, img, 36, 10, true);
	break;
	case 714:
	printPikachu(canvas, img, 35, 10, true);
	break;
	case 715:
	printPikachu(canvas, img, 34, 10, true);
	break;
	case 716:
	printPikachu(canvas, img, 33, 10, true);
	break;
	case 717:
	printPikachu(canvas, img, 32, 10, true);
	break;
	case 718:
	printPikachu(canvas, img, 31, 10, true);
	break;
	case 719:
	printPikachu(canvas, img, 30, 10, true);
	break;
	case 720:
	case 721:
	case 722:
	case 723:
	case 724:
	case 725:
	case 726:
	case 727:
	case 728:
	case 729:
	case 730:
	case 731:
	case 732:
	case 733:
	case 734:
	case 735:
	case 736:
	case 737:
	case 738:
	case 739:
	case 740:
	case 741:
	printPikachu(canvas, img, 30, 10, false);
	break;
	case 742:
	printPikachu(canvas, img, 30, 10, false);
	printPokeball(canvas, img, 19, 22, 1);
	fillCanvas(canvas, 18, 35, 32, 33, 0, 0, 0);
	break;
	case 743:
	printPikachu(canvas, img, 30, 10, false);
	printPokeball(canvas, img, 20, 21, 1);
	break;
	case 744:
	printPikachu(canvas, img, 30, 10, false);
	printPokeball(canvas, img, 21, 20, 1);
	break;
	case 745:
	printPikachu(canvas, img, 30, 10, false);
	printPokeball(canvas, img, 22, 19, 1);
	break;
	case 746:
	printPikachu(canvas, img, 30, 10, false);
	printPokeball(canvas, img, 23, 18, 1);
	break;
	case 747:
	printPikachu(canvas, img, 30, 10, false);
	printPokeball(canvas, img, 24, 17, 1);
	break;
	case 748:
	printPikachu(canvas, img, 30, 10, false);
	printPokeball(canvas, img, 25, 16, 1);
	break;
	case 749:
	printPikachu(canvas, img, 30, 10, false);
	printPokeball(canvas, img, 26, 15, 1);
	break;
	case 750:
	printPikachu(canvas, img, 30, 10, false);
	printPokeball(canvas, img, 27, 14, 1);
	break;
	case 751:
	printPikachu(canvas, img, 30, 10, false);
	printPokeball(canvas, img, 28, 14, 1);
	break;
	case 752:
	printPikachu(canvas, img, 30, 10, false);
	printPokeball(canvas, img, 29, 13, 1);
	break;
	case 753:
	printPikachu(canvas, img, 30, 10, false);
	printPokeball(canvas, img, 30, 13, 1);
	break;
	case 754:
	printPikachu(canvas, img, 30, 10, false);
	printPokeball(canvas, img, 31, 12, 1);
	break;
	case 755:
	printPikachu(canvas, img, 30, 10, false);
	printPokeball(canvas, img, 32, 12, 1);
	break;
	case 756:
	printPikachu(canvas, img, 30, 10, false);
	printPokeball(canvas, img, 33, 11, 1);
	break;
	case 757:
	printPikachu(canvas, img, 30, 10, false);
	printPokeball(canvas, img, 34, 11, 1);
	break;
	case 758:
	printPikachu(canvas, img, 30, 10, false);
	printPokeball(canvas, img, 35, 12, 1);
	break;
	case 759:
	case 760:
	case 761:
	case 762:
	printPikachu(canvas, img, 30, 10, false);
	printPokeball(canvas, img, 36, 13, 1);
	break;
	case 763:
	printOtherSmoke(canvas, img, 32, 9);
	printPokeball(canvas, img, 36, 15, 3);
	printPokeball(canvas, img, 36, 11, 4);
	break;
	case 764:
	case 765:
	printOtherSmoke(canvas, img, 32, 9);
	printPokeball(canvas, img, 36, 16, 3);
	printPokeball(canvas, img, 36, 10, 4);
	break;
	case 766:
	printSmoke(canvas, img, 34, 11, 0);
	printPokeball(canvas, img, 36, 17, 3);
	printPokeball(canvas, img, 36, 9, 4);
	break;
	case 767:
	case 768:
	printSmoke(canvas, img, 34, 11, 1);
	printPokeball(canvas, img, 36, 18, 3);
	printPokeball(canvas, img, 36, 8, 4);
	break;
	case 769:
	case 770:
	printSmoke(canvas, img, 34, 11, 1);
	printPokeball(canvas, img, 36, 19, 3);
	printPokeball(canvas, img, 36, 7, 4);
	break;
	case 771:
	case 772:
	case 773:
	case 774:
	case 775:
	case 776:
	case 777:
	case 778:
	case 779:
	case 780:
	printPokeball(canvas, img, 36, 15, 3);
	printPokeball(canvas, img, 36, 11, 4);
	break;
	case 781:
	case 782:
	case 783:
	case 784:
	case 785:
	case 786:
	case 787:
	case 788:
	case 789:
	case 790:
	printPokeball(canvas, img, 36, 13, 1);
	break;
	case 791:
	printPokeball(canvas, img, 36, 14, 1);
	break;
	case 792:
	printPokeball(canvas, img, 36, 15, 1);
	break;
	case 793:
	printPokeball(canvas, img, 36, 17, 1);
	break;
	case 794:
	printPokeball(canvas, img, 36, 18, 1);
	break;
	case 795:
	case 796:
	case 797:
	case 798:
	case 799:
	case 800:
	case 801:
	case 802:
	case 803:
	case 804:
	case 805:
	printPokeball(canvas, img, 36, 20, 1);
	break;
	case 806:
	case 807:
	case 808:
	printPokeball(canvas, img, 35, 20, 0);
	break;
	case 809:
	case 810:
	printPokeball(canvas, img, 36, 20, 1);
	break;
	case 811:
	case 812:
	case 813:
	printPokeball(canvas, img, 37, 20, 2);
	break;
	case 814:
	case 815:
	case 816:
	case 817:
	case 818:
	case 819:
	case 820:
	case 821:
	case 822:
	printPokeball(canvas, img, 36, 20, 1);
	break;
	case 823:
	case 824:
	case 825:
	printPokeball(canvas, img, 35, 20, 0);
	break;
	case 826:
	case 827:
	printPokeball(canvas, img, 36, 20, 1);
	break;
	case 828:
	case 829:
	case 830:
	printPokeball(canvas, img, 37, 20, 2);
	break;
	case 831:
	case 832:
	case 833:
	case 834:
	case 835:
	case 836:
	case 837:
	case 838:
	case 839:
	printPokeball(canvas, img, 36, 20, 1);
	break;
	case 840:
	case 841:
	case 842:
	printPokeball(canvas, img, 35, 20, 0);
	break;
	case 843:
	case 844:
	printPokeball(canvas, img, 36, 20, 1);
	break;
	case 845:
	case 846:
	case 847:
	printPokeball(canvas, img, 37, 20, 2);
	break;
	case 848:
	case 849:
	case 850:
	case 851:
	case 852:
	case 853:
	case 854:
	case 855:
	printPokeball(canvas, img, 36, 20, 1);
	break;
	case 856:
	fillCanvas(canvas, 41, 43, 24, 25, 255, 0, 0);
	printSmoke(canvas, img, 34, 18, 1);
	fillCanvas(canvas, 32, 52, 32, 35, 0, 0, 0);
	printPokeball(canvas, img, 36, 21, 3);
	printPokeball(canvas, img, 36, 19, 4);
	break;
	case 857:
	fillCanvas(canvas, 38, 47, 23, 25, 255, 0, 0);
	printSmoke(canvas, img, 34, 18, 1);
	printPokeball(canvas, img, 36, 22, 3);
	fillCanvas(canvas, 32, 52, 32, 35, 0, 0, 0);
	printPokeball(canvas, img, 36, 18, 4);
	break;
	case 858:
	fillCanvas(canvas, 35, 51, 22, 25, 255, 0, 0);
	printSmoke(canvas, img, 34, 18, 0);
	printPokeball(canvas, img, 36, 22, 3);
	fillCanvas(canvas, 32, 52, 32, 35, 0, 0, 0);
	printPokeball(canvas, img, 36, 18, 4);
	break;
	case 859:
	fillCanvas(canvas, 32, 55, 21, 25, 255, 0, 0);
	printSmoke(canvas, img, 34, 18, 0);
	printPokeball(canvas, img, 36, 22, 3);
	fillCanvas(canvas, 32, 52, 32, 35, 0, 0, 0);
	printPokeball(canvas, img, 36, 18, 4);
	break;
	case 860:
	fillCanvas(canvas, 29, 60, 20, 25, 255, 0, 0);
	printOtherSmoke(canvas, img, 32, 16);
	printPokeball(canvas, img, 36, 22, 3);
	fillCanvas(canvas, 32, 52, 32, 35, 0, 0, 0);
	printPokeball(canvas, img, 36, 18, 4);
	break;
	case 861:
	fillCanvas(canvas, 26, 65, 19, 26, 255, 0, 0);
	printOtherSmoke(canvas, img, 32, 16);
	printPokeball(canvas, img, 36, 22, 3);
	fillCanvas(canvas, 32, 52, 32, 35, 0, 0, 0);
	printPokeball(canvas, img, 36, 18, 4);
	break;
	case 862:
	fillCanvas(canvas, 23, 70, 18, 26, 255, 0, 0);
	break;
	case 863:
	fillCanvas(canvas, 20, 75, 18, 26, 255, 0, 0);
	break;
	case 864:
	case 865:
	case 866:
	case 867:
	case 868:
	case 869:
	case 870:
	case 871:
	case 872:
	case 873:
	case 874:
	case 875:
	case 876:
	case 877:
	case 878:
	case 879:
	case 880:
	case 881:
	case 882:
	case 883:
	case 884:
	case 885:
	case 886:
	case 887:
	case 888:
	rgb_matrix::DrawText(canvas, font1, 17, 27, color5, NULL, "BEWARE!");
	break;
	case 889:
	rgb_matrix::DrawText(canvas, font1, 17, 26, color5, NULL, "BEWARE!");
	break;
	case 890:
	rgb_matrix::DrawText(canvas, font1, 17, 25, color5, NULL, "BEWARE!");
	break;
	case 891:
	rgb_matrix::DrawText(canvas, font1, 17, 24, color5, NULL, "BEWARE!");
	break;
	case 892:
	rgb_matrix::DrawText(canvas, font1, 17, 23, color5, NULL, "BEWARE!");
	break;
	case 893:
	rgb_matrix::DrawText(canvas, font1, 17, 22, color5, NULL, "BEWARE!");
	break;
	case 894:
	rgb_matrix::DrawText(canvas, font1, 17, 21, color5, NULL, "BEWARE!");
	break;
	case 895:
	rgb_matrix::DrawText(canvas, font1, 17, 20, color5, NULL, "BEWARE!");
	break;
	case 896:
	rgb_matrix::DrawText(canvas, font1, 17, 19, color5, NULL, "BEWARE!");
	break;
	case 897:
	rgb_matrix::DrawText(canvas, font1, 17, 18, color5, NULL, "BEWARE!");
	break;
	case 898:
	rgb_matrix::DrawText(canvas, font1, 17, 17, color5, NULL, "BEWARE!");
	break;
	case 899:
	rgb_matrix::DrawText(canvas, font1, 17, 16, color5, NULL, "BEWARE!");
	break;
	case 900:
	rgb_matrix::DrawText(canvas, font1, 17, 15, color5, NULL, "BEWARE!");
	break;
	case 901:
	rgb_matrix::DrawText(canvas, font1, 17, 14, color5, NULL, "BEWARE!");
	break;
	case 902:
	rgb_matrix::DrawText(canvas, font1, 17, 13, color5, NULL, "BEWARE!");
	break;
	case 903:
	rgb_matrix::DrawText(canvas, font1, 17, 12, color5, NULL, "BEWARE!");
	break;
	case 904:
	rgb_matrix::DrawText(canvas, font1, 17, 11, color5, NULL, "BEWARE!");
	break;
	case 905:
	rgb_matrix::DrawText(canvas, font1, 17, 10, color5, NULL, "BEWARE!");
	break;
	case 906:
	rgb_matrix::DrawText(canvas, font1, 17, 9, color5, NULL, "BEWARE!");
	break;
	case 907:
	rgb_matrix::DrawText(canvas, font1, 17, 8, color5, NULL, "BEWARE!");
	break;
	case 908:
	rgb_matrix::DrawText(canvas, font1, 17, 7, color5, NULL, "BEWARE!");
	break;
	case 909:
	rgb_matrix::DrawText(canvas, font1, 17, 6, color5, NULL, "BEWARE!");
	break;
	case 910:
	rgb_matrix::DrawText(canvas, font1, 17, 5, color5, NULL, "BEWARE!");
	break;
	case 911:
	rgb_matrix::DrawText(canvas, font1, 17, 4, color5, NULL, "BEWARE!");
	break;
	case 912:
	rgb_matrix::DrawText(canvas, font1, 17, 3, color5, NULL, "BEWARE!");
	break;
	case 913:
	rgb_matrix::DrawText(canvas, font1, 17, 2, color5, NULL, "BEWARE!");
	break;
	case 914:
	rgb_matrix::DrawText(canvas, font1, 17, 1, color5, NULL, "BEWARE!");
	break;
	}
}

int main(int argc, char *argv[]) {
  
	// Set up GPIO pins. This fails when not running as root.
	GPIO io;
	if (!io.Init())
		return 1;

	const char *bdf_font_file = "fonts/7x13.bdf"; // Font file
	const char *bdf_font_file_a = "fonts/10x20.bdf";
	int rows = 32;    // A 32x32 display. Use 16 when this is a 16x32 display.
	int chain = 3;    // Number of boards chained together.
	int parallel = 2; // Number of chains in parallel (1..3). > 1 for plus or Pi2
	int y_orig = 52;
	int speed = 5;	// Scrolling speed

	int opt;
  	while ((opt = getopt(argc, argv, "f:C:s:")) != -1) {
	switch (opt) {
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
	if (!font1.LoadFont(bdf_font_file_a)) {
    		fprintf(stderr, "Couldn't load font '%s'\n", bdf_font_file_a);
    		return 1;
 	}
	if (speed < 1 || speed > 10){
		fprintf(stderr, "Speed outside usable range.\n");
   		return 1;
  	}
	
	// Set up the RGBMatrix. It implements a 'Canvas' interface.
	RGBMatrix *canvas = new RGBMatrix(&io, rows, chain, parallel);
	canvas->SetBrightness(70);

	// Load and store the sprite sheet in 
	PPMImage *image;
    	image = readPPM("sprsheet.ppm");
	
	unsigned char imgarr[width][height][3]; // Will store image array
	
	int y = 0;
	int x = 0;	

	// Store the image in a 3D array ([x-position][y-position][color compononent].
	// For color component: 0 = red, 1 = green, 2 = blue) from the PPMImage struct.
	for(int i=0; i < width*height; i++){
		x = i - (y*width);
	
		imgarr[x][y][0] = image->data[i].red;
		imgarr[x][y][1] = image->data[i].green;
		imgarr[x][y][2] = image->data[i].blue;
		
		if(((i + 1) % width) == 0){		
			y++;			
		}
	}
	
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

	int sleept = 180000.0 / speed; // Sleep time determined by speed.
	int aframe = 1;	

	// Keep scrolling the text while the server is connected
	while(scroll){
	lck.lock();
	strcpy(letters, cmsg);
	printf("Displaying '%s' \n", letters);
	lck.unlock();

	usleep(10000);
	
	int move = rgb_matrix::DrawText(canvas, font, ((chain * 32) - 2), y_orig, color, NULL, letters); // LED lenght of text

	// Shift text to the left
	for(int i = 1; i < move + (chain * 32); i++){
		canvas->Clear();
		animate(canvas, imgarr, aframe);
		rgb_matrix::DrawText(canvas, font, ((chain * 32) - 2) - i, y_orig, color, NULL, letters);
		usleep(sleept);

		if(!scroll)
			break;

		aframe++;
		if(aframe > 930)
			aframe = 1;
	}
	}
	
	usleep(3000);
	std::cout << "Done \n";
	canvas->Clear();
	delete canvas;

	serverthread.join();
	return 0;
}