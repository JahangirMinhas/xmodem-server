#ifndef PORT
    #define PORT 57681
#endif

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "crc16.h"
#include "xmodemserver.h"

// Defining some global variables
#define MAXBUFFER 1024
struct client *head = NULL;

// Function to add a client
static void add_client(int fd){
    struct client *new = malloc(sizeof(struct client));
    new->fd = fd;
    new->fp = NULL;
    new->inbuf = 0;
    new->state = initial;
    new->current_block = 0;
    new->next = NULL;
    if(head == NULL){
        head = new;
    }else{
        struct client *curr = head;
        while(curr->next != NULL){
            curr = curr->next;
        }
        curr->next = new;
    }
    printf("%s\n", "Added a client !");
}

// Function to remove a client
static void remove_client(int fd)
{
    struct client *rest;
    struct client *curr = head;

    // If the head is the node to be removed
    if(curr != NULL && curr->fd == fd){
        head = curr->next;
        free(curr);
        printf("%s\n", "Removed a client !");
        return;
    }

    // Otherwise search for the node to be deleted
    while(curr != NULL && curr->fd != fd){
        rest = curr;
        curr = curr->next;
    }

    rest->next = curr->next;
    free(curr);
    printf("%s\n", "Removed a client !");
}

// Function to create a file in "filestore" directory
FILE *open_file_in_dir(char *filename, char *dirname) {
    char buffer[MAXBUFFER];
    strncpy(buffer, "./", MAXBUFFER);
    strncat(buffer, dirname, MAXBUFFER - strlen(buffer) - 1);

    // create the directory dirname; fail silently if directory exists
    if(mkdir(buffer, 0700) == -1) {
        if(errno != EEXIST) {
            perror("mkdir");
            exit(1);
        }
    }
    strncat(buffer, "/", MAXBUFFER - strlen(buffer));
    strncat(buffer, filename, MAXBUFFER - strlen(buffer));
    return fopen(buffer, "wb");
}

int main(int argc, char **argv){
    // Declaring variables
    int read_count;
    int maxfd;
    int block_number;
    int inverse_block;
    int length = 0;
    static int listenfd;

    char character;
    char filename[20];
    char temp;
    
    struct sockaddr_in server;
    struct client *client;
    
    unsigned short crc;
    unsigned char low_client;
    unsigned char high_client;
    unsigned char low_server;
    unsigned char high_server;
    fd_set fds;
    

    // Calling socket to create fd
    if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("socket");
        exit(1);
    }

    // Code to relase server port when server terminates
    int yes = 1;
    if((setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))) == -1) {
        perror("setsockopt");
    }


    // Initializing the sockaddr struct
    memset(&server, '\0', sizeof server);
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    // Binding the IP and Socket to the server
    if(bind(listenfd, (struct sockaddr *) &server, sizeof server)){
        perror("bind");
        exit(1);
    }

    // Listening for clients on the server
    if(listen(listenfd, 5)){
        perror("listen");
        exit(1);
    }

    // Running infinite loop to allow multiple connections
    while(1){
        // Setting up the file descriptor set
        FD_ZERO(&fds);
        FD_SET(listenfd, &fds);

        maxfd = listenfd;
        client = head;
        while(client != NULL){
            FD_SET(client->fd, &fds);
            if (client->fd > maxfd){
                maxfd = client->fd;
            }
            client = client->next;
        }

        // Calling select to check fds for readable data.
        if(select(maxfd + 1, &fds, NULL, NULL, NULL) < 0){
            perror("select");
        }
        
        // Handle a new client
        if(FD_ISSET(listenfd, &fds)){
            int fd;
            struct sockaddr_in server;
            socklen_t socklen = sizeof server;
            if((fd = accept(listenfd, (struct sockaddr *)&server, &socklen)) < 0){
                perror("accept");
            }else{
                add_client(fd);
                FD_SET(fd, &fds);
            }
        }

        // Searching for the latest client ready with data and saving it into 'client'
        client = head;
        while(client != NULL){
            if(FD_ISSET(client->fd, &fds)){
                break;
            }
            client = client->next;
        }

        // Checking for the initial state
            // Reads the filename
            // Sends a 'C' to the client
        if(client->state == initial){
            int filename_length = 0;
            int filename_check = 0;

            // Reads one character at a time: stopping when filename > 19 or if the /r charatcer is found.
            while((read_count = read(client->fd, &character, 1)) > 0){
                if(filename_length > 19){
                    client->state = finished;
                    filename_check = 1;
                    break;
                }
                if(character == '\r'){
                    break;
                }
                filename[filename_length] = character; 
                filename_length++;
            }

            // If the filename is valid
            if(filename_check == 0){
                filename[filename_length] = '\0';
                strcpy(client->filename, filename);
                FILE *fp = open_file_in_dir(filename, "filestore");
                client->fp = fp;
                char c = 'C';
                write(client->fd, &c, 1);
                client->state = pre_block;
            }
        
        // Checking for the pre_block state
            // Read reply from client
            // Handle EOT, SOH, STX accordingly
        }else if(client->state == pre_block){
            read_count = read(client->fd, &character, 1);
            if(read_count < 0){
                perror("read");
                client->state = finished;
            }else if(read_count == 0){
                client->state = finished;
            }else{
                if(character == EOT){
                    temp = ACK;
                    write(client->fd, &temp, 1);
                    client->state = finished;
                }else if(character == SOH){
                    client->blocksize = 128;
                    client->state = get_block;
                }else if(character == STX){
                    client->blocksize = 1024;
                    client->state = get_block;
                }
            }

        // Checking for the get_block state
            // Read the current block and the inverse block
            // Read the file data
            // Read the high and low byte of the crc16;
        }else if(client->state == get_block){
            // Read the current block
            unsigned char block_numc;
            read(client->fd, &block_numc, 1);
            block_number = block_numc;

            // Read the inverse block
            unsigned char block_inversec;
            read(client->fd, &block_inversec, 1);
            inverse_block = block_inversec;

            // Read the file data
            length = 0;
            while(length != client->blocksize){
                read(client->fd, &character, 1);
                client->buf[length] = character;
                length++;
                if(character != 26){
                    client->inbuf++;
                }
            }
            
            // Read the high and low byte of the crc16
            read(client->fd, &high_client, 1);
            read(client->fd, &low_client, 1);
            client->state = check_block;
        }
        
        // Checking for the check_block state
            // Calculate the CRC16
            // Check every condition where file is received incorrectly
            // Write data to the file if data is received correctly
        if(client->state  == check_block){
            // Calculate the low and high byte of the crc16
            crc = crc_message(XMODEM_KEY, (unsigned char*) client->buf, client->blocksize);
            high_server = crc >> 8;
            low_server = crc;

            // If block and inverse block do not correspond
            if(inverse_block != 255 - block_number){
                client->state  = finished;

            // if the block number on server side and client side are the same
            }else if(block_number == client->current_block){
                // Server received duplicate block so tell client to send next block
                temp = ACK;
                write(client->fd, &temp, 1);
                client->state  = pre_block;
            
            // If the block number from the client is not what we expect then drop the client
            }else if((block_number != client->current_block + 1 && client->current_block != 255) || (block_number != 0 && client->current_block == 255)){
                client->state = finished;

            // If the crc16 is incorrect then tell client to resend the same block
            }else if(low_server != low_client || high_server != high_client){
                temp = NAK;
                write(client->fd, &temp, 1);
                client->state = pre_block;

            // If file is received correctly, then write it to the server's files and get ready for next block
            }else{
                write(fileno(client->fp), client->buf, client->inbuf);
                client->inbuf = 0;
                client->current_block++;
                temp = ACK;
                write(client->fd, &temp, 1);
                client->state = pre_block;
            }
        }

        // Checking for the finished state
            // Close the client's file pointer
            // Disconnect the client
            // Remove the client
        if(client->state == finished){
            if(client->fp != NULL){
                close(fileno(client->fp));
            }
            close(client->fd);
            remove_client(client->fd);
        }
    }
}