#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;


/* attempts to read n (len) bytes from fd; returns true on success and false on failure. 
It may need to call the system call "read" multiple times to reach the given size len. 
*/
static bool nread(int fd, int len, uint8_t *buf) {
  //Call read()
  int net_read = read(fd, buf, len);

  //Check if read is success
  if(net_read == len){
    return true;
  }

  else{
    printf("Error reading network data [%s]\n", strerror(errno));
    return false;
  }

  return false;
}

/* attempts to write n bytes to fd; returns true on success and false on failure 
It may need to call the system call "write" multiple times to reach the size len.
*/
static bool nwrite(int fd, int len, uint8_t *buf) {
  //Call write()
  int net_write = write(fd, buf, len);

  //Check if write is success
  if(net_write == len){
    return true;
  }

  else{
    printf("Error writing network data [%s]\n", strerror(errno));
    return false;
  }
  
  return false;
}

/* Through this function call the client attempts to receive a packet from sd 
(i.e., receiving a response from the server.). It happens after the client previously 
forwarded a jbod operation call via a request message to the server.  
It returns true on success and false on failure. 
The values of the parameters (including op, ret, block) will be returned to the caller of this function: 

op - the address to store the jbod "opcode"  
ret - the address to store the return value of the server side calling the corresponding jbod_operation function.
block - holds the received block content if existing (e.g., when the op command is JBOD_READ_BLOCK)

In your implementation, you can read the packet header first (i.e., read HEADER_LEN bytes first), 
and then use the length field in the header to determine whether it is needed to read 
a block of data from the server. You may use the above nread function here.  
*/
static bool recv_packet(int sd, uint32_t *op, uint16_t *ret, uint8_t *block) {
  

  uint8_t received_header[8]; //Create header array to receive

  bool net_header_read = nread(sd, 8, received_header); //Call nread to make received_header array have received header

  if(net_header_read == false){//Check if header is successfully read
    return false;
  }

  //Initialize length variable
  uint16_t len;

  //Extract length, op code, return value from the received packet header to parameter pointer op, ret and len variable
  memcpy(&len, &received_header[0], 2);
  memcpy(op, &received_header[2], 4);
  memcpy(ret, &received_header[6], 2);
  

  //Change unsigned integer netlong/short from network byte order to host byte order
  len = ntohs(len);
  *op = ntohl(*op);
  *ret = ntohs(*ret);

  //Get the block(jbod bytes from server) if the length received from server is not 8 which the packet is not only header
  if(len != 8){
    bool net_packet_read = nread(sd, 256, block);
    if(net_packet_read != true){ //Check if packet read is successful
      return false;
    }

    return true;
  }

  
  return true;
}



/* The client attempts to send a jbod request packet to sd (i.e., the server socket here); 
returns true on success and false on failure. 

op - the opcode. 
block- when the command is JBOD_WRITE_BLOCK, the block will contain data to write to the server jbod system;
otherwise it is NULL.

The above information (when applicable) has to be wrapped into a jbod request packet (format specified in readme).
You may call the above nwrite function to do the actual sending.  
*/
static bool send_packet(int sd, uint32_t op, uint8_t *block) {

  //Initialize length variable
  uint16_t len;

  //If there is block existing within which means the operation is JBOD_read/write
  if(block != NULL){
    //Initialize 264 byte packet
    uint8_t packet[264];
    
    //Set packet length to 264
    len = 264;

    //Convert unsigned integer netlong/short from host byte order to network byte order
    len = htons(len);
    op = htonl(op);

    //Put corresponding values into the packet that wish to be sent
    memcpy(&packet[0], &len, 2); //Len as 0-1 byte
    memcpy(&packet[2], &op, 4); //Op as 2-5 byte, ret is ignored due to packet is to be sent
    memcpy(&packet[8], block, 256); //Block from mdadm.c as 8-263 byte

    //Sent packet to server
    bool net_write = nwrite(sd, 264, packet);

    //Check if send packet is successful
    if(net_write == true){
      return true;
    }

    return false;

  }

  //If no block to be sent which means JBOD_operation is other than read/write
  if(block == NULL){
    //Initialize packet header
    uint8_t packet_header[8];

    //Set len to 8
    len = 8;

    //Convert unsigned integer netlong/short from host byte order to network byte order
    len = htons(len);
    op = htonl(op);

    //Put corresponding values into the packet that wish to be sent
    memcpy(&packet_header[0], &len, 2); //Len as 0-1 byte
    memcpy(&packet_header[2], &op, 4); //Op as 2-5 byte, ret is ignored due to packet is to be sent

    //Send packet to server 
    bool net_write = nwrite(sd, 8, packet_header);

    //Check if send packet is successful
    if(net_write == true){
      return true;
    }

    return false;
  }

  return false;

  
}



/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not. 
 * this function will be invoked by tester to connect to the server at given ip and port.
 * you will not call it in mdadm.c
*/
bool jbod_connect(const char *ip, uint16_t port) {

  //Initialize sock structure
  struct sockaddr_in client_addr;

  //Define port to use connection
  client_addr.sin_family = AF_INET;
  client_addr.sin_port = htons(port);
  
  //converts the Internet host address cp from the IPv4 numbers-and-dots notation into binary form (in network byte order) and stores it in the structure that inp points to.
  if( inet_aton(ip, &client_addr.sin_addr) == 0){
    return false;
  }

  //Create socket
  cli_sd = socket(AF_INET, SOCK_STREAM, 0);

  //Check if fd is valid
  if (cli_sd == -1){
    printf("Error on socket creation [%s]\n", strerror(errno));
    return false;
  }

  //Connect
  int connect_server = connect(cli_sd, (struct sockaddr*)&client_addr, sizeof(client_addr));

  //Check if connect is successful
  if (connect_server == -1){
    printf("Error on socket connect [%s]\n", strerror(errno));
    return false;
  }

  //If connect success
  return true;
}



/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void) {
  //Disconnect
  close(cli_sd);

  //Reset fd to -1
  cli_sd = -1;
}



/* sends the JBOD operation to the server (use the send_packet function) and receives 
(use the recv_packet function) and processes the response. 

The meaning of each parameter is the same as in the original jbod_operation function. 
return: 0 means success, -1 means failure.
*/
int jbod_client_operation(uint32_t op, uint8_t *block) {
  
  //Initialize return value
  uint16_t return_value;
  
  //Send packet
  send_packet(cli_sd, op, block);

  //Receive packet, op/return value/ block is returned from server as well
  recv_packet(cli_sd, &op, &return_value, block);

  //Check if return is correct
  if (return_value == 0){
    return 0;
  }

  return -1;
}
