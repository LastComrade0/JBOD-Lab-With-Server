//CMPSC 311 SP22
//LAB 2

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mdadm.h"
#include "jbod.h"
#include "cache.h"
#include "net.h"

//Global variables
int mount_status;

//Function to construct op code
uint32_t construct_op(uint8_t command, uint8_t disk_id, uint16_t reserved, uint8_t block_id){
    uint32_t op_code = 0x0, temp_command, temp_disk_id, temp_reserved, temp_block_id; //Local temporary op fields
    temp_command = (command & 0xffffffff) << 26; //Move command left 26 bits
    temp_disk_id = (disk_id & 0xffffffff) << 22;  //Move disk ID left 22 bits
    temp_reserved = (reserved & 0xffffffff) << 8; //Move reserved left 8 bits
    temp_block_id = (block_id & 0xffffffff); //Block ID remains in current position
    op_code = temp_command | temp_disk_id | temp_reserved | temp_block_id; //Concatenate 4 command fields to single 32 bit op code

    return op_code;

}

int mdadm_mount(void) {
  
  //If already mounted, return error
  if (mount_status == 1){
    return -1;
  }

  //Else mount the disk
  else{
    uint32_t disk_op = construct_op(JBOD_MOUNT,0,0,0); //Construct op code
    int mount = jbod_client_operation(disk_op, NULL); //Syscall mount, and will return 0(Success) or 1(Failure)

    //If the returned value from syscall is 0, change global variable mount status to 1 (Success)
    if(mount == 0){
      mount_status = 1;
    }

    //If disk is not mounted, return error
    if (mount_status != 1){
      return -1;
    }
    
    //Return 1 as success
    return 1;
  }
  
}

int mdadm_unmount(void) {
  //If disk is not mounted, return error
  if (mount_status == 0){
    return -1;
  }
  

  else{
    uint32_t disk_op = construct_op(JBOD_UNMOUNT,0,0,0); //Construct op code
    int unmount = jbod_client_operation(disk_op, NULL); //Syscall mount, and will return 0(Success) or 1(Failure)

    //If returned value is 0(successfully unmounted), change global variable mount_status to 0
    if(unmount == 0){
      mount_status = 0;
    }

    //If disk is not unmounted, return error
    if(mount_status != 0){
      return -1;
    }

    //Return 1 as success
    return 1;
  }
  
  
}

int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf) {
  //Set local variable in read function
  int disk; //Current disk ID
  int block; //Current block ID
  int position = addr; //Current position within 16 Disks, will not become 0 after a block
  uint8_t temp_buf[256]; //Temporary buffer to store a whole read block, then copy desired segment into *buf


  
  int num_byte_read = 0; //Bytes already read after an action
  int local_len = len; //Local count value which decrements every time the read operation executes
  
  //If disk is not mounted, return error
  if(mount_status == 0){
  return -1;
  }

  //If address is greater than 16 Disk Spaces, return error
  if(addr >= 1048570){
    return -1;
  }

  //If length is larger than 1KB, return error
  if(len > 1024){
    return -1;
  }

  //If buf is NULL but len is not 0, return error. But NULL buf with 0 length is fine
  if(buf == NULL && len != 0){
    return -1;
  }

  //Seek to disk
  //If address greater than spaces of DISK 0 which is located in Disk 1-15, use division to find corresponding Disk
  if(addr > 65535){
    disk = (addr/JBOD_DISK_SIZE);
  }

  //If address is less than or equal to spaces of DISK 0 which is located in Disk 0, the disk local variable will be 0
  if(addr <= 65535){
    disk = 0;
  }
  
  //Seek to block
  //If address is less than a block space which is in block 0, the block local variable will be 0
  if(addr <= 255){
    block = 0;
  }

  //If address greater than spaces of block 0 which is located in block 1-255, use division to find corresponding Disk
  //Address will substract to current disk size(65536) multiply with current disk ID. Then divide by 256 to find block.
  if(addr > 255){
    block = (addr - (JBOD_DISK_SIZE * disk)) / 256;
  }

  //While loop to control the reading, every operation will increment num_byte_read until reached len to break the loop
  while(num_byte_read < len){
    int byte_to_read = 0; //Bytes to read in current action
    //Boolean to decide increment block by 1 or not
    int block_increment_boolean = 0;

    //Get relative position from each block(Always 0 to 255)
    int relative_position = position % 256;

    //If current disk is in ID 0
    if(disk == 0){

      //If (current position + local_len) is greater than 256*(current block + 1), the variable byte_to_read will be bytes left in the block
      //This is the case if desired length of reading exceeds the block size, then boolean for incrementing block will be TRUE
      if((position + local_len) > (JBOD_BLOCK_SIZE * ( block+1 ) )){
        byte_to_read = 256 - relative_position; //Byte to read is the space left for the block
        block_increment_boolean = 1; //Need to increment a block after all
      }

      //If (current position + local_len) is less than 256*(current block + 1) 
      //This is the case if desired length of reading is still within the block size
      if((position + local_len) <= (JBOD_BLOCK_SIZE * ( block+1 ) )){
        //The variable byte_to_read will be the len(from function parameter) - bytes that has already read
        byte_to_read += (len - num_byte_read);
      }
    }

    //If current disk ID is not 0
    if(disk != 0){
      //If (current position + local_len) is greater than 256^2*current disk ID + (current block ID + 1)
      //Same as previous, only block determination is different
      if(((position + local_len) > 256*( 256*(disk) + (block + 1)))){
        byte_to_read = 256 - relative_position;
        block_increment_boolean = 1;
      }

      if(((position + local_len) <= 256*( 256*(disk) + (block + 1)))){
        byte_to_read += (len - num_byte_read);
      }

    }

    //Look for cache
    //If cache exists
    if(cache_enabled() == true){
      //If cache entry exists
      if(cache_lookup(disk, block, temp_buf) == 1){
        
        //Memcpy temp_buf that just got from cache to buf
        memcpy(&buf[num_byte_read], &temp_buf[relative_position], byte_to_read); //Copy the value starting from 'relative_position' on block to buf which starts from 'num_byte_read'
      
      }

      //If no cache entry
      if(cache_lookup(disk, block, temp_buf) == -1){
        //After determining bytes to write and the boolean of incrementing block, seek disk
        uint32_t seek_disk_op = construct_op(JBOD_SEEK_TO_DISK,disk,0,0);
        int seek_to_disk = jbod_client_operation(seek_disk_op,NULL);
        if(seek_to_disk == -1){
          printf("Seek to disk failed.");
        }

        //Seek block
        uint32_t seek_block_op = construct_op(JBOD_SEEK_TO_BLOCK,0,0,block);
        int seek_to_block = jbod_client_operation(seek_block_op,NULL);
        if(seek_to_block == -1){
          printf("Seek to block failed.");
        }   

        //Read action
        uint32_t read_disk_op = construct_op(JBOD_READ_BLOCK,0,0,0);
        jbod_client_operation(read_disk_op, temp_buf); //Read the blocks and copy the value on block to a temporary buffer which is 256 bytes long
        cache_insert(disk, block, temp_buf);
        memcpy(&buf[num_byte_read], &temp_buf[relative_position], byte_to_read); //Copy the value starting from 'relative_position' on block to buf which starts from 'num_byte_read'
      }
    }

    //If no cache exist
    if(cache_enabled() == false){
      //After determining bytes to write and the boolean of incrementing block, seek disk
      uint32_t seek_disk_op = construct_op(JBOD_SEEK_TO_DISK,disk,0,0);
      int seek_to_disk = jbod_client_operation(seek_disk_op,NULL);
      if(seek_to_disk == -1){
        printf("Seek to disk failed.");
      }

      //Seek block
      uint32_t seek_block_op = construct_op(JBOD_SEEK_TO_BLOCK,0,0,block);
      int seek_to_block = jbod_client_operation(seek_block_op,NULL);
      if(seek_to_block == -1){
        printf("Seek to block failed.");
      }

      //Read action
      uint32_t read_disk_op = construct_op(JBOD_READ_BLOCK,0,0,0);
      jbod_client_operation(read_disk_op, temp_buf); //Read the blocks and copy the value on block to a temporary buffer which is 256 bytes long
      cache_insert(disk, block, temp_buf);
      memcpy(&buf[num_byte_read], &temp_buf[relative_position], byte_to_read); //Copy the value starting from 'relative_position' on block to buf which starts from 'num_byte_read'
    }
    
    //Increase local values
    num_byte_read += byte_to_read; //Byte count already read is incremented by desired byte count to read
    position += byte_to_read; //Update position by incrementing by desired byte count to read
    local_len -= byte_to_read; //Byte that hasn't been read will be decremented by desired byte count to read
   

    //If the boolean from previous step is set to TRUE
    if(block_increment_boolean == 1){
      block += 1;//Increment current block by 1
    }
    

    //Case if block increments out of disk bound
    if (block > 255){
      disk += 1; //Increment current disk by 1
      block = 0; //Reset current block to 0
    }

  }

  return len;
}


int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf) {
  //Set local variable in write function
  int disk; //Current disk ID
  int block; //Current block ID
  int position = addr; //Current position within 16 Disks, will not become 0 after a block
  uint8_t temp_buf[256]; //Temporary buffer to store a whole block, then copy the const buf to temp_buf in correct position


  
  int num_byte_write = 0; //Bytes already read after an action
  int local_len = len; //Local count value which decrements every time the read operation executes
  
  //If disk is not mounted, return error
  if(mount_status == 0){
  return -1;
  }

  //If address is greater than 16 Disk Spaces, return error
  if(addr > 1048560){
    return -1;
  }

  //If length is larger than 1KB, return error
  if(len > 1024){
    return -1;
  }

  //If buf is NULL but len is not 0, return error. But NULL buf with 0 length is fine
  if(buf == NULL && len != 0){
    return -1;
  }

  //Seek to disk
  //If address greater than spaces of DISK 0 which is located in Disk 1-15, use division to find corresponding Disk
  if(addr > 65535){
    disk = (addr/JBOD_DISK_SIZE);
  }

  //If address is less than or equal to spaces of DISK 0 which is located in Disk 0, the disk local variable will be 0
  if(addr <= 65535){
    disk = 0;
  }
  
  //Seek to block
  //If address is less than a block space which is in block 0, the block local variable will be 0
  if(addr <= 255){
    block = 0;
  }

  //If address greater than spaces of block 0 which is located in block 1-255, use division to find corresponding Disk
  //Address will substract to current disk size(65536) multiply with current disk ID. Then divide by 256 to find block.
  if(addr > 255){
    block = (addr - (JBOD_DISK_SIZE * disk)) / 256;
  }

  while(num_byte_write < len){
    int byte_to_write = 0;
    int block_increment_boolean = 0;

    int relative_position = position % 256;
    
    


    //If current disk is in ID 0
    if(disk == 0){


      if((position + local_len) > (JBOD_BLOCK_SIZE * ( block+1 ) )){
        byte_to_write = 256 - relative_position; //Byte to write is the space left for the block
        block_increment_boolean = 1; //Need to increment a block after all
      }

      
      if((position + local_len) <= (JBOD_BLOCK_SIZE * ( block+1 ) )){
        //The variable byte_to_write will be the len(from function parameter) - bytes that has already written
        byte_to_write += (len - num_byte_write);
      }
    }


    //If current disk ID is not 0
    if(disk != 0){
      //If (current position + local_len) is greater than 256^2*(current disk ID + (current block ID + 1)))
      //Same as previous, only block determination is different
      if(( (position + local_len) > 256*( 256*(disk) + (block + 1) ) ) ){
        byte_to_write = 256 - relative_position;
        block_increment_boolean = 1;
      }

      if(( (position + local_len) <= 256*( 256*(disk) + (block + 1) ) ) ){
        byte_to_write += (len - num_byte_write);
      }

    }

    



    //Look for cache
    //If cache exists
    if(cache_enabled() == true){

      //If the entry does not exist
      if(cache_lookup(disk, block, temp_buf) == -1){

        //After determining bytes to write and the boolean of incrementing block, seek disk
        uint32_t seek_disk_op = construct_op(JBOD_SEEK_TO_DISK,disk,0,0);
        int seek_to_disk = jbod_client_operation(seek_disk_op,NULL);
        if(seek_to_disk == -1){
          printf("Seek to disk failed.");
        }

        //Seek block
        uint32_t seek_block_op = construct_op(JBOD_SEEK_TO_BLOCK,0,0,block);
        int seek_to_block = jbod_client_operation(seek_block_op,NULL);
        if(seek_to_block == -1){
          printf("Seek to block failed.");
        }

        //Read action before write to get the whole block to temp_buf
        uint32_t read_disk_op = construct_op(JBOD_READ_BLOCK,0,0,0);
        jbod_client_operation(read_disk_op, temp_buf); //Read the blocks and copy the value on block to a temporary buffer which is 256 bytes long

        //After determining bytes to write and the boolean of incrementing block, seek disk
        seek_disk_op = construct_op(JBOD_SEEK_TO_DISK,disk,0,0);
        seek_to_disk = jbod_client_operation(seek_disk_op,NULL);
        if(seek_to_disk == -1){
          printf("Seek to disk failed.");
        }

        //Seek block
        seek_block_op = construct_op(JBOD_SEEK_TO_BLOCK,0,0,block);
        seek_to_block = jbod_client_operation(seek_block_op,NULL);
        if(seek_to_block == -1){
          printf("Seek to block failed.");
        }

        //Write
        memcpy(&temp_buf[relative_position], &buf[num_byte_write], byte_to_write); //Copy the value starting from 'num_byte_write' of const buf to temp_buf with length of 'byte_to_write'
        uint32_t write_disk_op = construct_op(JBOD_WRITE_BLOCK,0,0,0);
        jbod_client_operation(write_disk_op, temp_buf); //Write the updated temp_buf to the block

        //Insert cache after write
        cache_insert(disk, block, temp_buf);

      }

      //If the entry exists
      if(cache_lookup(disk, block, temp_buf) == 1){

        //Memcpy the buf to desired part of temp_buf
        memcpy(&temp_buf[relative_position], &buf[num_byte_write], byte_to_write); //Copy the value starting from 'num_byte_write' of const buf to temp_buf with length of 'byte_to_write'
        
        //Update cache since the disk/block number are identical
        cache_update(disk, block, temp_buf);

        //After determining bytes to write and the boolean of incrementing block, seek disk
        uint32_t seek_disk_op = construct_op(JBOD_SEEK_TO_DISK,disk,0,0);
        int seek_to_disk = jbod_client_operation(seek_disk_op,NULL);
        if(seek_to_disk == -1){
          printf("Seek to disk failed.");
        }

        //Seek block
        uint32_t seek_block_op = construct_op(JBOD_SEEK_TO_BLOCK,0,0,block);
        int seek_to_block = jbod_client_operation(seek_block_op,NULL);
        if(seek_to_block == -1){
          printf("Seek to block failed.");
        }

        //Write
        uint32_t write_disk_op = construct_op(JBOD_WRITE_BLOCK,0,0,0);
        jbod_client_operation(write_disk_op, temp_buf); //Write the updated temp_buf to the block
        
      }
      
    }

    //If no cache exist
    if(cache_enabled() == false){

      //After determining bytes to write and the boolean of incrementing block, seek disk
      uint32_t seek_disk_op = construct_op(JBOD_SEEK_TO_DISK,disk,0,0);
      int seek_to_disk = jbod_client_operation(seek_disk_op,NULL);
      if(seek_to_disk == -1){
        printf("Seek to disk failed.");
      }

      //Seek block
      uint32_t seek_block_op = construct_op(JBOD_SEEK_TO_BLOCK,0,0,block);
      int seek_to_block = jbod_client_operation(seek_block_op,NULL);
      if(seek_to_block == -1){
        printf("Seek to block failed.");
      }

      //Read action before write to get the whole block to temp_buf
      uint32_t read_disk_op = construct_op(JBOD_READ_BLOCK,0,0,0);
      jbod_client_operation(read_disk_op, temp_buf); //Read the blocks and copy the value on block to a temporary buffer which is 256 bytes long
    
      //Seek disk again due to previous jbod operation increments block by 1
      construct_op(JBOD_SEEK_TO_DISK,disk,0,0);
      jbod_client_operation(seek_disk_op,NULL);


      //Seek block again due to previous jbod operation increments block by 1
      construct_op(JBOD_SEEK_TO_BLOCK,0,0,block);
      jbod_client_operation(seek_block_op,NULL);


      //Write
      memcpy(&temp_buf[relative_position], &buf[num_byte_write], byte_to_write); //Copy the value starting from 'num_byte_write' of const buf to temp_buf with length of 'byte_to_write'
      uint32_t write_disk_op = construct_op(JBOD_WRITE_BLOCK,0,0,0);
      jbod_client_operation(write_disk_op, temp_buf); //Write the updated temp_buf to the block
    }




    //Increase local values
    num_byte_write += byte_to_write; //Byte count already written is incremented by desired byte count to write
    position += byte_to_write; //Update position by incrementing by desired byte count to write
    local_len -= byte_to_write; //Byte that hasn't been written will be decremented by desired byte count to write
   

    //If the boolean from previous step is set to TRUE
    if(block_increment_boolean == 1){
      block += 1;//Increment current block by 1
    }
    

    //Case if block increments out of disk bound
    if (block > 255){
      disk += 1; //Increment current disk by 1
      block = 0; //Reset current block to 0
    }
  }

  return len;
}
