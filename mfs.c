#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#define NUM_BLOCKS 65536
#define BLOCK_SIZE 1024
#define BLOCKS_PER_FILE 1024
#define MAX_NUM_FILES 256
#define FIRST_DATA_BLOCK 1001
#define MAX_FILE_SIZE 1048576

#define HIDDEN 0x1
#define READ_ONLY 0x2

uint8_t data[NUM_BLOCKS][BLOCK_SIZE];

// 512 blocks just for free block map
uint8_t *free_blocks;
uint8_t *free_inodes;

// directory structure
struct _directoryEntry
{
  char name[64];
  bool inUse;
  int32_t inode;
};

// directory array
struct _directoryEntry *directory;

// inode structure
struct inode
{
  int32_t blocks[BLOCKS_PER_FILE];
  int block_length;
  bool inUse;
  bool hidden;
  bool readonly;
  uint32_t file_size;
  time_t creation_time;
};

// inode structure
struct inode *inodes;

// global variables that define the image
FILE* fp = NULL;
char image_name[64];
bool image_open;

int fileCount = 0;

#define WHITESPACE " \t\n" // We want to split our command line up into tokens
                           // so we need to define what delimits our tokens.
                           // In this case  white space
                           // will separate the tokens on our command line

#define MAX_COMMAND_SIZE 255 // The maximum command-line size

#define MAX_NUM_ARGUMENTS 11 // Mav shell only supports four arguments

// keep us from being contiguous
int32_t findFreeInode()
{
  for(int i = 0; i < MAX_NUM_FILES; i++)
  {
    if(free_inodes[i])
    {
      free_inodes[i] = 0;
      return i;
    }
  }
  return -1;
}

int32_t findFreeBlock()
{
  for(int i = 0; i < NUM_BLOCKS; i++)
  {
    if(free_blocks[i] == 1)
    {
      free_blocks[i] = 0;
      return i + FIRST_DATA_BLOCK;
    }
  }

  return -1;
}

int32_t findFreeInodeBlock(int32_t inode)
{
  for(int i = 0; i < BLOCKS_PER_FILE; i++)
  {
    if(inodes[inode].blocks[i] == -1)
    {

      return i;
    }
  }

  return -1;
}

// finds the given file and sets the file to not in use and
// as well as the associated inode and blocks with it
void delete(char *filename)
{
  int32_t index_found = -1;
  int32_t inode_index = -1;

  // Loops over entire directory until it finds the file and checks that
  // file is not read-only and that the file is in use 
  for(int i = 0; i < MAX_NUM_FILES; i++)
  {
    if(strcmp(directory[i].name, filename) == 0 
    && !inodes[directory[i].inode].readonly && directory[i].inUse)
    {
      index_found = i;
      inode_index = directory[i].inode;
      
      directory[i].inUse = false;
      inodes[inode_index].inUse = false;
    }
    //message if file is read only and exists
    else if(strcmp(directory[i].name, filename) == 0 
    && inodes[directory[i].inode].readonly && directory[i].inUse)
    {
      printf("File is labeled under READ ONLY, unable to delete\n");
      return;
    }
  }

  // The file is not found in the directory
  if(index_found == -1)
  {
    printf("ERROR: File not found.\n");
    return;
  }
  
  // Delete file by setting all blocks used by file to free
  for(int i = 0; i < inodes[inode_index].block_length; i++)
  {
    // the index substracts the offset mapping it back to the array of free_blocks
    free_blocks[inodes[inode_index].blocks[i] - FIRST_DATA_BLOCK] = 1;
  }
}

// finds the given file and sets the file to in use and
// as well as the associated inode and blocks with it
void undelete(char* filename)
{
  int32_t index_found = -1;
  int32_t inode_index = -1;

  for(int i = 0; i < MAX_NUM_FILES; i++)  //looks for the file name
  {
    if(strcmp(directory[i].name, filename) == 0)  //file exists
    {
      if(directory[i].inUse)  //notify if the file wasn't deleted
      {
        printf("File %s exists\n", filename);
      }
      else  //flip the deleted file back to inuse and it's inode back as well
      {
        directory[i].inUse = true;
        inodes[directory[i].inode].inUse = true;
        printf("\"%s\" recovered\n", filename); //notify user of success
      }
      index_found = i;
      inode_index = directory[i].inode; //save index
    }
  }

  if(index_found == -1) //notify user if the file doesn't exist
  {
    printf("ERROR: File not found.\n");
  }
  else  //if the file is found and was not in use, then flip the file blocks to in use
  {
    for(int k = 0; k < inodes[inode_index].block_length; k++)
    {
      free_blocks[inodes[inode_index].blocks[k] - FIRST_DATA_BLOCK] = 0;
    }
  }
}

// initialize all variables with default valuess
void init()
{
  directory = (struct _directoryEntry*)&data[0][0];
  inodes = (struct inode*)&data[20][0]; 
  free_blocks = (uint8_t*)&data[1000][0];
  free_inodes = (uint8_t*)&data[19][0];

  memset(image_name, 0, 64);

  for(int i = 0; i < MAX_NUM_FILES; i++)
  {
    directory[i].inUse = false;
    directory[i].inode = -1;
    free_inodes[i] = 1;
    memset(directory[i].name, 0, 64);

    for(int j = 0; j < NUM_BLOCKS; j++)
    {
      inodes[i].blocks[j] = -1;
      inodes[i].inUse = false;
      inodes[i].hidden = false;
      inodes[i].readonly = false;
      inodes[i].file_size = 0;
    }
  }

  for(int i = 0; i < NUM_BLOCKS; i++)
  {
    free_blocks[i] = 1;
  }
}

// calculate the free space avaialable in the disk image
uint32_t df()
{
  int count = 0;

  for(int i = 0; i < NUM_BLOCKS; i++)
  {
    if(free_blocks[i])
      count++;
  }

  return count * BLOCK_SIZE;
}

// create a file structure with the given name by the user
void createfs(char *filename)
{
  init();
  fp = fopen(filename, "w");

  strncpy(image_name, filename, strlen(filename));

  //Set all data in data array to 0
  memset(data, 0, NUM_BLOCKS * BLOCK_SIZE);
  image_open = true;

  //Initialize the entire directory
  for(int i = 0; i < MAX_NUM_FILES; i++)
  {
    directory[i].inUse = false;
    directory[i].inode = -1;
    free_inodes[i] = 1;
    memset(directory[i].name, 0, 64);

    for(int j = 0; j < NUM_BLOCKS; j++)
    {
      inodes[i].blocks[j] = -1;
      inodes[i].inUse = false;
      inodes[i].hidden = false;
      inodes[i].readonly = false;
      inodes[i].file_size = 0;
    }
  }

  // set all blocks as "free"
  for(int i = 0; i < NUM_BLOCKS; i++)
  {
    free_blocks[i] = 1;
  }


  fclose(fp);
}

// save the contents of the disk image to the file
void savefs()
{
  if(image_open == 0)
  {
    printf("ERROR: Disk image is not open.\n");
    return;
  }
  
  fp = fopen(image_name, "w");

  fwrite(&data[0][0], BLOCK_SIZE, NUM_BLOCKS, fp);

  fclose(fp);
}

// open the file structure specified by the user
void openfs(char *filename)
{
  init();
  fp = fopen(filename, "r");

  if(fp == NULL)
  {
    printf("ERROR: File could not be openned.\n");
    return;
  }

  strncpy(image_name, filename, strlen(filename));

  fread(&data[0][0], BLOCK_SIZE, NUM_BLOCKS, fp);

  image_open = true;

  fclose(fp);
}

// close the current openned file structure, if there is one open
void closefs()
{
  if(image_open == false)
  {
    printf("ERROR: Disk image is not open.\n");
    return;
  }
  
  //fclose(fp);
  
  image_open = false;
  memset(image_name, 0, 64);
}

// lists the file with the creation time, and size. also will print out
// if the file is hidden or read only if the flag is set
void list(char* first, char* second)
{
  bool not_found = true;
  bool h = false;
  bool a = false;
  printf("%-65s%-15s%-25s", "Directory List", "Byte Size", "Time");
  //Check if user wants to see hidden files
  if(strcmp(first, "-h") == 0  || strcmp(second, "-h") == 0)
  {
    h = true;
  }
  //Check if user wants to see attributes of file
  if(strcmp(first, "-a") == 0  || strcmp(second, "-a") == 0)
  {
    a = true;
    printf("%-15s%-15s", "Hidden", "Read Only");
  }
  printf("\n");
  
  for(int i = 0; i < MAX_NUM_FILES; i++)
  {
    if((directory[i].inUse && !inodes[directory[i].inode].hidden) || (directory[i].inUse && h))
    {
      not_found = false;
      char filename[65];
      memset(filename, 0, 65);
      strncpy(filename, directory[i].name, strlen(directory[i].name));
      //Get the size of the file
      int size = inodes[directory[i].inode].file_size;
      time_t filetime = inodes[directory[i].inode].creation_time;
      char str_time[120];
      //Formats creation time to string from time_t
      strftime(str_time, sizeof(str_time),"%Y-%m-%d %H:%M:%S",localtime(&filetime));
      printf("%-65s%-15d%-25s", filename, size, str_time);

      //If user requests attributes, display 1 indicating it hidden or read-only, otherwise 0
      if(a)
      {
        if(inodes[directory[i].inode].hidden)
        {
          printf("%-15d", 1);
        }
        else
        {
          printf("%-15d", 0);
        }
        if(inodes[directory[i].inode].readonly)
        {
          printf("%-15d", 1);
        }
        else
        {
          printf("%-15d", 0);
        }
      }
      printf("\n");
    }
  }

  if(not_found)
  {
    printf("No files found.\n");
  }
}

// inserts the file specified by the user into the disk image
void insert(char *filename)
{
  // verify the filename isn't NULL
  if(filename == NULL)
  {
    printf("ERROR: Unspecified file.\n");
    return;
  }

  // verify the file exists
  struct stat buf;
  int ret = stat(filename, &buf);

  if(ret == -1)
  {
    printf("ERROR: File does not exist.\n");
    return;
  }

  // checks to see if the filename length is 64 or less
  if(strlen(filename) > 64)
  {
    printf("ERROR: Filename is too large.\n");
    return;
  }

  // verify the file isn't too big
  if(buf.st_size > MAX_FILE_SIZE)
  {
    printf("ERROR: File is too large.\n");
    return;
  }

  // verify there is enough space
  if(buf.st_size > df())
  {
    printf("ERROR: Not enough free disk space.\n");
    return;
  }

  // find an empty directory entry
  int directory_entry = -1;
  for(int i = 0; i < MAX_NUM_FILES; i++)
  {
    if(directory[i].inUse == false)
    {
      directory_entry = i;
      break;
    }
  }

  if(directory_entry == -1)
  {
    printf("ERROR: Could not find a free directory entry.\n");
    return;
  }

  // open the input file read-only 
  FILE *ifp = fopen (filename, "r" ); 

  // declaring the time_t variable to store current time
  time_t now;

  // get the current time and format it
  time(&now);
  char str_time[120];
  strftime(str_time, sizeof(str_time),"%Y-%m-%d %H:%M:%S",localtime(&now));

  // store the file size to keep track of how much is left
  int32_t copy_size = buf.st_size;

  // use an offset to determine where to start copying 
  int32_t offset = 0;               

  // store the block index of the next free block
  int32_t block_index = -1;

  // find a free inode
  int32_t inode_index = findFreeInode();

  if(inode_index == -1)
  {
    printf("ERROR: Can not find free inode.\n");
    return;
  }

  // place the file info in to directory
  directory[directory_entry].inUse = 1;
  directory[directory_entry].inode = inode_index;

  // set the filename to the one specified by the user
  memset(directory[directory_entry].name, 0, 64);
  strncpy(directory[directory_entry].name, filename, strlen(filename));

  inodes[inode_index].file_size = buf.st_size;
  inodes[inode_index].block_length = 0;
  inodes[inode_index].creation_time = now;
  inodes[inode_index].hidden = false;
  inodes[inode_index].readonly = false;

  // copy_size is initialized to the size of the input file so each loop iteration we
  // will copy BLOCK_SIZE bytes from the file then reduce our copy_size counter by
  // BLOCK_SIZE number of bytes. When copy_size is less than or equal to zero we know
  // we have copied all the data from the input file.
  while(copy_size > 0)
  {
    // Index into the input file by offset number of bytes.  Initially offset is set to
    // zero so we copy BLOCK_SIZE number of bytes from the front of the file.  We 
    // then increase the offset by BLOCK_SIZE and continue the process.  This will
    // make us copy from offsets 0, BLOCK_SIZE, 2*BLOCK_SIZE, 3*BLOCK_SIZE, etc.
    fseek( ifp, offset, SEEK_SET );

    // find a free block
    block_index = findFreeBlock();

    if( block_index == -1)
    {
      printf("ERROR: Can not find a free block.\n");
      return;
    }

    // increases the block length
    inodes[inode_index].block_length += 1;

    // overrides the data in the block found to 0
    memset(data[block_index], 0, BLOCK_SIZE);

    // reads the data from the input file and sets it in the block found
    int bytes  = fread(data[block_index], BLOCK_SIZE, 1, ifp );

    // save the block in the inode
    int32_t inode_block = findFreeInodeBlock(inode_index);
    inodes[inode_index].blocks[inode_block] = block_index;

    // If bytes == 0 and we haven't reached the end of the file then something is 
    // wrong. If 0 is returned and we also have the EOF flag set then that is OK.
    // It means we've reached the end of our input file.
    if( bytes == 0 && !feof( ifp ) )
    {
      printf("ERROR: An error occured reading from the input file.\n");
      return;
    }

    // Clear the EOF file flag.
    clearerr( ifp );

    // Reduce copy_size by the BLOCK_SIZE bytes.
    copy_size -= BLOCK_SIZE;
    
    // Increase the offset into our input file by BLOCK_SIZE.  This will allow
    // the fseek at the top of the loop to position us to the correct spot.
    offset += BLOCK_SIZE;
  }

  // We are done copying from the input file so close it out.
  fclose( ifp );
}

// retrieves the file specified by the user from the disk image and places
// the file in the current working directory of the user
void retrieve(char *filename)
{
  bool not_found = true;
  int directory_index = -1;

  for(int i = 0; i < MAX_NUM_FILES; i++)
  {
    if(strcmp(directory[i].name, filename) == 0 && directory[i].inUse)
    {
      not_found = false;
      directory_index = i;
      break;
    }
  }

  if(not_found)
  {
    printf("ERROR: File does not exist in the disk image.\n");
    return;
  }

  printf("File found.\n");

  // Now, open the output file that we are going to write the data to.
  FILE *ofp;
  ofp = fopen(filename, "w");

  if( ofp == NULL )
  {
    printf("Could not open output file: %s\n", filename );
    perror("Opening output file returned");
    return;
  }

  // Initialize our offsets and pointers just we did above when reading from the file.
  int starting_inode = directory[directory_index].inode;
  int copy_size   = inodes[starting_inode].file_size;
  int block_index = 0;
  int offset      = 0;

  int i = 0;
  // int block_length = inodes[starting_inode].block_length;

  printf("Writing %d bytes to %s\n", copy_size, filename );

  // Using copy_size as a count to determine when we've copied enough bytes to the output file.
  // Each time through the loop, except the last time, we will copy BLOCK_SIZE number of bytes from
  // our stored data to the file fp, then we will increment the offset into the file we are writing
  // to. On the last iteration of the loop, instead of copying BLOCK_SIZE number of bytes we just
  // copy how ever much is remaining ( copy_size % BLOCK_SIZE ).  If we just copied BLOCK_SIZE on
  // the last iteration we'd end up with gibberish at the end of our file. 
  while( copy_size > 0 )
  { 

    int num_bytes;

    // If the remaining number of bytes we need to copy is less than BLOCK_SIZE then
    // only copy the amount that remains. If we copied BLOCK_SIZE number of bytes we'd
    // end up with garbage at the end of the file.
    if( copy_size < BLOCK_SIZE )
    {
      num_bytes = copy_size;
    }
    else 
    {
      num_bytes = BLOCK_SIZE;
    }

    block_index = inodes[starting_inode].blocks[i++];

    // Write num_bytes number of bytes from our data array into our output file.
    fwrite( data[block_index], num_bytes, 1, ofp ); 

    // Reduce the amount of bytes remaining to copy, increase the offset into the file
    // and increment the block_index to move us to the next data block.
    copy_size -= BLOCK_SIZE;
    offset    += BLOCK_SIZE;

    // Since we've copied from the point pointed to by our current file pointer, increment
    // offset number of bytes so we will be ready to copy to the next area of our output file.
    fseek( ofp, offset, SEEK_SET );
  }

  // Close the output file, we're done. 
  fclose( ofp );
}

// works similar to retrive function but with a designated output file
void retrieve_to_file(char *inFilename, char *outFilename)
{
  bool not_found = true;
  int directory_index = -1;
  for(int i = 0; i < MAX_NUM_FILES; i++)
  {
    if(strcmp(directory[i].name, inFilename) == 0 && directory[i].inUse)
    {
      not_found = false;
      directory_index = i;
      break;
    }
  }

  if(not_found)
  {
    printf("ERROR: File does not exist in the disk image.\n");
    return;
  }

  printf("File found.\n");

  // Now, open the output file that we are going to write the data to.
  FILE *ofp;
  ofp = fopen(outFilename, "w");

  if( ofp == NULL )
  {
    printf("Could not open output file: %s\n", outFilename );
    perror("Opening output file returned");
    return;
  }

  // Initialize our offsets and pointers just we did above when reading from the file.
  int starting_inode = directory[directory_index].inode;
  int copy_size   = inodes[starting_inode].file_size;
  int block_index = 0;
  int offset      = 0;

  int i = 0;
  // int block_length = inodes[starting_inode].block_length;

  printf("Writing %d bytes to %s\n", copy_size, outFilename );

  // Using copy_size as a count to determine when we've copied enough bytes to the output file.
  // Each time through the loop, except the last time, we will copy BLOCK_SIZE number of bytes from
  // our stored data to the file fp, then we will increment the offset into the file we are writing
  // to. On the last iteration of the loop, instead of copying BLOCK_SIZE number of bytes we just
  // copy how ever much is remaining ( copy_size % BLOCK_SIZE ).  If we just copied BLOCK_SIZE on
  // the last iteration we'd end up with gibberish at the end of our file. 
  while( copy_size > 0 )
  { 

    int num_bytes;

    // If the remaining number of bytes we need to copy is less than BLOCK_SIZE then
    // only copy the amount that remains. If we copied BLOCK_SIZE number of bytes we'd
    // end up with garbage at the end of the file.
    if( copy_size < BLOCK_SIZE )
    {
      num_bytes = copy_size;
    }
    else 
    {
      num_bytes = BLOCK_SIZE;
    }

    block_index = inodes[starting_inode].blocks[i++];

    // Write num_bytes number of bytes from our data array into our output file.
    fwrite( data[block_index], num_bytes, 1, ofp ); 

    // Reduce the amount of bytes remaining to copy, increase the offset into the file
    // and increment the block_index to move us to the next data block.
    copy_size -= BLOCK_SIZE;
    offset    += BLOCK_SIZE;

    // Since we've copied from the point pointed to by our current file pointer, increment
    // offset number of bytes so we will be ready to copy to the next area of our output file.
    fseek( ofp, offset, SEEK_SET );
  }

  // Close the output file, we're done. 
  fclose( ofp );

}

//reads a file byte by byte starting at the given byte and ending after
//traversing the provided number of bytes
void readfile(char* filename, int start, int numbytes)
{
  int32_t inode_index = -1;
  int32_t blocknum;

  if(filename == NULL)    //checks filename for NULL input
  {
    printf("ERROR: No filename provided.\n");  //print error if filename not given
  }
  else
  {
    for(int i = 0; i < MAX_NUM_FILES; i++)  //looks for inode of file and saves
    {
      if(strcmp(directory[i].name, filename) == 0 && directory[i].inUse)
      {
        inode_index = directory[i].inode; //saves the inode index
      }
    }

    if(inode_index != -1)   //if file exists, reads
    {
      blocknum = start/BLOCK_SIZE;
      //checks if byte input is within file's used blocks, error message if outside
      if(inodes[inode_index].block_length - 1 < blocknum)
      {
        printf("ERROR: Start byte outside of file range.\n");
      }
      else
      {
        int startbyte = start % BLOCK_SIZE; //byte number inside block
        int traverse = numbytes / BLOCK_SIZE + blocknum;  //last block
        int remainingbytes; //remaining bytes, not whole blocks
        //determines if end byte passes file end
        if(traverse >= inodes[inode_index].block_length)
        {
          traverse = inodes[inode_index].block_length -1;
          remainingbytes = BLOCK_SIZE;  //only reaches end of file, no surpassing
        }
        else
        {
          remainingbytes = numbytes % BLOCK_SIZE; //end byte < end file
        }
        int currblock;
        printf("File %s (in hexadec), from byte %d for %d bytes::\n", filename, start, numbytes);
        for(int k = blocknum; k < traverse; k++)    //iterates through every byte within bounds
        {
          currblock = inodes[inode_index].blocks[k];
          for(int j = startbyte; j < BLOCK_SIZE; j++)
          {
            if(data[currblock][j] != 0)
            {
              printf("%02hhx", data[currblock][j]);   //prints every byte in hexadec
            }
          }
          startbyte = 0;  //resets the start byte after the first block
        }
        currblock = inodes[inode_index].blocks[traverse]; //the final "incomplete" block
        for(int m = startbyte; m < remainingbytes; m++)
        {
          if(data[currblock][m] != 0)
          {
            printf("%02hhx", data[currblock][m]);   //prints every byte in hexadec
          }
        }
        printf("\n----File Reading finished----\n");  //message to signal end
      }
    }
    else
    {
      printf("ERROR: File not found.\n"); //file not found
    }
  }
}

//encrypts the given file using a XOR encryption and the given key
void encrypt(char* filename, char* keystr, char which)
{
  int32_t inode_index = -1;
  char key = keystr[0];

  if(filename == NULL)    //checks for NULL filename
  {
    printf("ERROR: No filename provided.\n");
  }
  else if( key >= 256)  //checks the size of the cipher
  {
    printf("ERROR: Cipher surpasses 256 bits.\n");
  }
  else
  {
    for(int i = 0; i < MAX_NUM_FILES; i++)  //looks for and retrieves file inode
    {
      if(strcmp(directory[i].name, filename) == 0 && directory[i].inUse)
      {
        inode_index = directory[i].inode;
      }
    }
    if(inode_index != -1)   //if the file exists, data is transformed byte by byte
    {
      int currblock;
      for(int k = 0; k < inodes[inode_index].block_length; k++)
      {
        currblock = inodes[inode_index].blocks[k];
        for(int j = 0; j < BLOCK_SIZE; j++)
        {
          if(data[currblock][j] != 0)
          {
            data[currblock][j] = data[currblock][j] ^ key;
          }
        }
      }
      if(which == 'e')    //checks which if statement called this function for print
      {
        printf("Encryption complete.\n");
      }
      else
      {
        printf("Decryption complete.\n");
      }
    }
    else
    {
      printf("ERROR: File not found.\n");
    }
  }
}

//adds or subtracts an attribute from the file, 
//depending on the attribute given
void attribute(char* filename, char* attri)
{
  uint32_t inode_index = -1;
  if(filename == NULL)  //handles the event of if a NULL value gets passed
  {
    printf("ERROR: No filename provided.\n");
  }
  else
  {
    for(int i = 0; i < MAX_NUM_FILES; i++)    //attempts to find the file and saves the inode
    {
      if(strcmp(directory[i].name, filename) == 0 && directory[i].inUse)
      {
        inode_index = directory[i].inode; 
      }
    }

    if(inode_index != -1)   //if the index exists, flip appropriate flag accordingly
    {
      if(attri[0] == '-' && attri[1] == 'h')
      {
        inodes[inode_index].hidden = false;
      }
      else if(attri[0] == '+' && attri[1] == 'h')
      {
        inodes[inode_index].hidden = true;
      }
      else if(attri[0] == '-' && attri[1] == 'r')
      {
        inodes[inode_index].readonly = false;
      }
      else if(attri[0] == '+' && attri[1] == 'r')
      {
        inodes[inode_index].readonly = true;
      }
    }
    else
    {
      printf("ERROR: File not found.\n");
    }
  }
}

int main()
{
  char *command_string = (char *)malloc(MAX_COMMAND_SIZE);

  init();

  while (1)
  {
    // Print out the mfs prompt
    printf("mfs> ");

    // Read the command from the commandline.  The
    // maximum command that will be read is MAX_COMMAND_SIZE
    // This while command will wait here until the user
    // inputs something since fgets returns NULL when there
    // is no input
    while (!fgets(command_string, MAX_COMMAND_SIZE, stdin))
        ;

    // Checks to see if the user entered nothing
    if(strcmp(command_string, "\n") == 0)
      continue;

    command_string[strlen(command_string) - 1] = 0;

    /* Parse input */
    char *token[MAX_NUM_ARGUMENTS];

    // Nulls out the token array
    for (int i = 0; i < MAX_NUM_ARGUMENTS; i++)
    {
      token[i] = NULL;
    }

    int token_count = 0;

    // Pointer to point to the token
    // parsed by strsep
    char *argument_ptr = NULL;

    char *working_string = strdup(command_string);

    // we are going to move the working_string pointer so we
    // keep track of its original value so we can deallocate
    // the correct amount at the end
    char *head_ptr = working_string;

    // Tokenize the input strings with whitespace used as the delimiter
    while (((argument_ptr = strsep(&working_string, WHITESPACE)) != NULL) &&
           (token_count < MAX_NUM_ARGUMENTS))
    {
      token[token_count] = strndup(argument_ptr, MAX_COMMAND_SIZE);
      if (strlen(token[token_count]) == 0)
      {
        token[token_count] = NULL;
      }
      token_count++;
    }

    if(strcmp(command_string, "\n") == 0)
      continue;

    if(strcmp(token[0], "quit") == 0)
    {
      // Cleanup allocated memory
      for (int i = 0; i < MAX_NUM_ARGUMENTS; i++)
      {
        if (token[i] != NULL)
        {
          free(token[i]);
        }
      }

      free(head_ptr);
      free(command_string);

      return(EXIT_SUCCESS);
    }

    if(strcmp(token[0], "open") == 0)
    {
      if(token_count != 2)
      {
        printf("ERROR: usage open <disk name>.\n");
        continue;
      }
      // open functionality
      if(token[1] == NULL) //file exists
      {
        printf("ERROR: File not found.\n");
        continue;
      }

      //open
      openfs(token[1]);
    }

    if(strcmp(token[0], "createfs") == 0 && token_count == 2)
    {
      // createfs functionality
      if(token[1] == NULL)
      {
        printf("ERROR: File name cannot be NULL.\n");
        continue;
      }

      createfs(token[1]);
    }

    if(image_open && (strcmp(token[0], "insert") == 0 || strcmp(token[0], "retrieve") == 0 
    || strcmp(token[0], "read") == 0 || strcmp(token[0], "delete") == 0 
    || strcmp(token[0], "undel") == 0 || strcmp(token[0], "list") == 0 
    || strcmp(token[0], "df") == 0 || strcmp(token[0], "close") == 0 
    || strcmp(token[0], "savefs") == 0 || strcmp(token[0], "attrib") == 0 
    || strcmp(token[0], "encrypt") == 0 || strcmp(token[0], "decrypt") == 0))
    {
      if(strcmp(token[0], "insert") == 0)
      {
        // insert functionality
        if(token_count != 2)
        {
          printf("ERROR: usage: insert <filename>\n");
          continue;
        }

        if(token[1] == NULL)
        {
          printf("ERROR: No filename specified.\n");
          continue;
        }

        insert(token[1]);
      }

      if(strcmp(token[0], "retrieve") == 0 && token_count == 2)
      {
        // retrieve #1 functionality
        if(token[1] == NULL)
        {
          printf("ERROR: File name cannot be NULL.\n");
          continue;
        }

        retrieve(token[1]);
      }
      else if(strcmp(token[0], "retrieve") == 0 && token_count == 3)
      {
        // retrieve #2 functionality
        if(token[1] == NULL || token[2] == NULL)
        {
          printf("ERROR: Both files must be specified.\n");
        }

        retrieve_to_file(token[1], token[2]);
      }

      if(strcmp(token[0], "read") == 0 && token_count == 4)
      {
        // read functionality
        readfile(token[1], atoi(token[2]), atoi(token[3]));
      }

      if(strcmp(token[0], "delete") == 0 && token_count == 2)
      {
        if(token[1] == NULL) //filename exists && not already deleted
        {
          printf("ERROR: File not specified.\n");
          continue;
        }

        delete(token[1]);
      }

      if(strcmp(token[0], "undel") == 0 && token_count == 2)
      {
        if(token[1] == NULL) //checks if a filename was provided
        {
          printf("ERROR: File not specified.\n");
          continue;
        }
        
        undelete(token[1]);
      }

      if(strcmp(token[0], "list") == 0)
      {
        // TODO: add size and time_added to print
        if(token[1] && (strcmp(token[1], "-h") == 0 || strcmp(token[1], "-a") == 0))
        {
          if(token[2] && ((strcmp(token[2], "-h") == 0 || strcmp(token[2], "-a") == 0) && strcmp(token[2], token[1]) != 0))
          {
            list(token[1], token[2]);
          }
          else
          {
            list(token[1], "throw away");
          }
        }
        else
        {
          list("hot", "garbage");
        }
      }

      if(strcmp(token[0], "df") == 0)
      {
        // df functionality
        printf("%d bytes free.\n", df());
      }

      if(strcmp(token[0], "close") == 0 && token_count == 1)
      {
        // close functionality
        closefs();
      }

      if(strcmp(token[0], "savefs") == 0)
      {
        // savefs functionality
        savefs(image_name);
      }

      if(strcmp(token[0], "attrib") == 0 && token_count == 3)
      {
        // attrib [+attribute][-attribute] functionality
        if(strcmp(token[1], "-h") == 0 || strcmp(token[1], "+h") == 0 
        || strcmp(token[1], "-r") == 0 || strcmp(token[1], "+r") == 0)
        {   //checks provided attributes prior to function call
          attribute(token[2], token[1]);
        }
        else
        {
          printf("\nERROR: Invalid attribute entry.\n");
        }
      }

      if(strcmp(token[0], "encrypt") == 0 && token_count == 3)
      {
        // encrypt functionality 
        if(strlen(token[2]) == 1)   //checks if key is a single char
        {
          encrypt(token[1], token[2], 'e');
        }
        else
        {
          printf("\nERROR: Invalid cipher.\n");
        }
      }

      if(strcmp(token[0], "decrypt") == 0 && token_count == 3)
      {
        // decrypt functionality
        if(strlen(token[2]) == 1) //checks if key is a single char
        {
          encrypt(token[1], token[2], 'd');
        }
        else
        {
          printf("\nERROR: invalid cipher.\n");
        }
      }
    }
    else if(!image_open && (strcmp(token[0], "insert") == 0 || strcmp(token[0], "retrieve") == 0 
    || strcmp(token[0], "read") == 0 || strcmp(token[0], "delete") == 0 
    || strcmp(token[0], "undel") == 0 || strcmp(token[0], "list") == 0 
    || strcmp(token[0], "df") == 0 || strcmp(token[0], "close") == 0 
    || strcmp(token[0], "savefs") == 0 || strcmp(token[0], "attrib") == 0 
    || strcmp(token[0], "encrypt") == 0 || strcmp(token[0], "decrypt") == 0))
    {
      printf("ERROR: Disk image is not opened.\n");
      continue;
    }
  }

  return 0;
}