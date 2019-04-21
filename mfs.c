
#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <sys/param.h>
#include <string.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>

#define WHITESPACE " \t\n" // We want to split our command line up into tokens \
                           // so we need to define what delimits our tokens.   \
                           // In this case  white space                        \
                           // will separate the tokens on our command line

#define MAX_COMMAND_SIZE 255 // The maximum command-line size

#define MAX_HISTORY_SIZE 50 // The maximum history size

#define MAX_PIDS_SIZE 15 //The maximum list of pids size

#define MAX_NUM_ARGUMENTS 12 // Mav shell only supports 10 arguments in additions \
                             //to commands and last one for null terminating for exec

int16_t BPB_BytesPerSec;
int8_t BPB_SecPerClus;
int16_t BPB_RsvdSecCnt;
int32_t BPB_FATSz32;
int8_t BPB_NumFATs;
FILE *file = NULL;
int firstClusterAddress; //to store the starting cluster of each working directory

//To handle signal send by the user from keyboard
//parameter-sig is the signal caught by sigaction
static void handle_signal(int sig)
{
}

/*
    Method that returns offset from cluster
    Parameter: Cluster number of FAT Table
    Returns: Offset in the cluster region.
*/

int LBAtoOffset(int32_t sector)
{
  return (((sector - 2) * BPB_BytesPerSec) + (BPB_BytesPerSec * BPB_RsvdSecCnt) +
          (BPB_NumFATs * BPB_FATSz32 * BPB_BytesPerSec));
}

/*
  Method returns the next cluster number
  parameter: cluster number of FAT Table
  Returns : next cluster number.
*/
int16_t NextLB(uint32_t sector)
{
  uint32_t FATAddress = (BPB_BytesPerSec * BPB_RsvdSecCnt) + (sector * 4);
  int16_t val;
  fseek(file, FATAddress, SEEK_SET);
  fread(&val, 2, 1, file);
  return val;
}

/*
  Method to compare two strings.
  Parameters: IMG_Name- DIR_Name of file, input- file name to be compared to IMG_Name
  returns: 1 if they are same, 0 otherwise
*/
int compare(char *IMG_Name, char *input)
{
  char expanded_name[12];
  memset(expanded_name, ' ', 12);

  char *token2 = strtok(input, ".");

  strncpy(expanded_name, token2, strlen(token2));

  token2 = strtok(NULL, ".");

  if (token2)
  {
    strncpy((char *)(expanded_name + 8), token2, strlen(token2));
  }

  expanded_name[11] = '\0';

  int i;
  for (i = 0; i < 11; i++)
  {
    expanded_name[i] = toupper(expanded_name[i]);
  }

  if (strncmp(expanded_name, IMG_Name, 11) == 0)
  {
    return 1;
    printf("%s\n", input);
  }
  else
  {
    return 0;
    printf("%s\n", input);
  }
}

struct __attribute__((__packed__)) DirectoryEntry
{
  char DIR_Name[11];
  uint8_t DIR_Attr;
  uint8_t Unused1[8];
  uint16_t DIR_FirstClusterHigh;
  uint8_t Unused2[4];
  uint16_t DIR_FirstClusterLow;
  uint32_t DIR_FileSize;
};
struct DirectoryEntry dir[16]; //array to store the directory entry of current directory

/*
    Function to read all the directory entries of working directory
    parameter file- fat32 image file pointer
    parameter firstCluster- starting address of the directory to be read
    parameter dir- array to store all the directory entry
*/
void readDirectoryEntry(FILE *file, int firstCluster, struct DirectoryEntry dir[])
{
  int i;
  int tempCluster = firstCluster;
  for (i = 1; i <= 16; i++)
  {
    //reading file name, 11 bytes
    fseek(file, tempCluster, SEEK_SET);
    fread(&dir[i - 1].DIR_Name, 11, 1, file);

    //reading file attribute, 12th byte
    fseek(file, tempCluster + 11, SEEK_SET);
    fread(&dir[i - 1].DIR_Attr, 1, 1, file);

    //reading clustor high
    fseek(file, tempCluster + 20, SEEK_SET);
    fread(&dir[i - 1].DIR_FirstClusterHigh, 2, 1, file);

    //reading clustor low, 26th to 28th byte
    fseek(file, tempCluster + 26, SEEK_SET);
    fread(&dir[i - 1].DIR_FirstClusterLow, 2, 1, file);

    //reading file size, 28th to 32th byte
    fseek(file, tempCluster + 28, SEEK_SET);
    fread(&dir[i - 1].DIR_FileSize, 4, 1, file);

    tempCluster = firstCluster + (i * 32); //iterating to next directory entry
  }
}
/*
    method to tokenize the command string entered by user
    paremeter- command string and the token pointer array
    returns- the number of strings in the token array
*/
int tokenize_inputs(char *cmd_str, char *token[])
{
  int token_count = 0;

  // Pointer to point to the token
  // parsed by strsep
  char *arg_ptr;

  char *working_str = strdup(cmd_str);

  // we are going to move the working_str pointer so
  // keep track of its original value so we can deallocate
  // the correct amount at the end
  char *working_root = working_str;

  // Tokenize the input stringswith whitespace used as the delimiter
  while (((arg_ptr = strsep(&working_str, WHITESPACE)) != NULL) &&
         (token_count < MAX_NUM_ARGUMENTS))
  {
    token[token_count] = strndup(arg_ptr, MAX_COMMAND_SIZE);
    if (strlen(token[token_count]) == 0)
    {
      token[token_count] = NULL;
    }
    token_count++;
  }
  free(working_root);
  return token_count;
}

int main()
{

  //string to store the commands entered by the user
  char *cmd_str = (char *)malloc(MAX_COMMAND_SIZE);

  int i;
  while (1)
  {

    // Print out the msh prompt
    printf("mfs> ");
    // Read the command from the commandline.  The
    // maximum command that will be read is MAX_COMMAND_SIZE
    // This while command will wait here until the user
    // inputs something since fgets returns NULL when there
    // is no input
    while (!fgets(cmd_str, MAX_COMMAND_SIZE, stdin))
      ;

    // If user just hits enter, then we just continue without
    // doing anything to get another command input.
    if (strlen(cmd_str) == 1)
    {
      continue;
    }

    /* Parse input */
    char *token[MAX_NUM_ARGUMENTS];
    int token_count = 0;

    //call the method to tokenize the command string
    token_count = tokenize_inputs(cmd_str, token);

    // if the entered command is exit or quit, then we quit the mav shell.
    if (strcmp(token[0], "exit") == 0 || strcmp(token[0], "quit") == 0)
    {
      free(cmd_str);
      if (file != NULL)
      {
        fclose(file);
      }
      exit(0);
    }

    // this handles open file command.
    if (strcmp(token[0], "open") == 0)
    {
      if (token[1] == NULL) //checking if file name is provided or not
      {
        printf("Please enter the file name to open.\n");
        continue;
      }
      if (file == NULL) //checking if file is already is opened or not
      {
        file = fopen(token[1], "r+");
        if (file == NULL)
        {
          printf("Error: File system image not found.\n");
          continue;
        }
        else
        {
          //reading BPB_BytesPerSec of fat32.img
          fseek(file, 11, SEEK_SET);
          fread(&BPB_BytesPerSec, 2, 1, file);

          //reading BPB_RsvdSecCnt
          fseek(file, 14, SEEK_SET);
          fread(&BPB_RsvdSecCnt, 2, 1, file);

          //reading BPB_SecPerClus
          fseek(file, 13, SEEK_SET);
          fread(&BPB_SecPerClus, 1, 1, file);

          //reading BPB_FATSz32
          fseek(file, 36, SEEK_SET);
          fread(&BPB_FATSz32, 2, 2, file);

          //reading BPB_NumFATs
          fseek(file, 16, SEEK_SET);
          fread(&BPB_NumFATs, 1, 1, file);

          //This gives the first cluster address of fat32.img
          firstClusterAddress = (BPB_NumFATs * BPB_FATSz32 * BPB_BytesPerSec) +
                                (BPB_RsvdSecCnt * BPB_BytesPerSec);

          //reading root directory entry
          readDirectoryEntry(file, firstClusterAddress, dir);
          continue;
        }
      }
      else //if file is already opened
      {
        printf("Error: File system image already open.\n");
        continue;
      }
    }

    // this handles open file command.
    if (strcmp(token[0], "close") == 0)
    {
      if (file != NULL) //checking is already closed or not.
      {
        fclose(file);
        file = NULL;
        continue;
      }
      else //if file is not opened
      {
        printf("Error: Error: File system not open.\n");
        continue;
      }
    }

    //this handles info command.
    if (strcmp(token[0], "info") == 0)
    {
      if (file != NULL) //if file is opened
      {
        printf("BPB_BytesPerSector:0x%x %d\n", BPB_BytesPerSec, BPB_BytesPerSec);
        printf("BPB_SecPerClus:0x%x %d\n", BPB_SecPerClus, BPB_SecPerClus);
        printf("BPB_RsvdSecCnt:0x%x %d\n", BPB_RsvdSecCnt, BPB_RsvdSecCnt);
        printf("BPB_NumFATs:0x%x %d\n", BPB_NumFATs, BPB_NumFATs);
        printf("BPB_FATSz32:0x%x %d\n", BPB_FATSz32, BPB_FATSz32);
        continue;
      }
      else
      {
        printf("Error: File system image must be opened first.\n");
        continue;
      }
    } //handling info command ends.

    //this handles ls command.
    if (strcmp(token[0], "ls") == 0)
    {
      if (file != NULL) // if file is opened
      {
        for (i = 0; i < 16; i++)
        {

          /*
            0xE5- dont show if file is deleted
            0x10- show if the directory entry is subdirectory
            0x20- show if directory entry is archived file
            0x01-  show if directory entry is read only file
            Ignore all other files and directories with other attributes
          */
          if (dir[i].DIR_Name[0] != (char)0xE5 && (dir[i].DIR_Attr == (char)0x30 ||
                                                   dir[i].DIR_Attr == (char)0x10 ||
                                                   dir[i].DIR_Attr == (char)0x01 ||
                                                   dir[i].DIR_Attr == (char)0x20))
          {
            printf("%s\n", strndup(dir[i].DIR_Name, 11));
          }
        }
      }
      else
      {
        printf("Error: File system image must be opened first.\n");
        continue;
      }
    } //ls command handling ends

    //this handles stat command.
    if (strcmp(token[0], "stat") == 0)
    {
      if (token[1] == NULL)
      {
        continue;
      }
      int fileFound = 0; //flag to check if file entered by user is found or not
      if (file != NULL)
      {
        for (i = 0; i < 16; i++)
        {
          if (compare(dir[i].DIR_Name, strdup(token[1]))) //comparing the file entered by
                                                          //user to directory entires
          {
            printf("Attributes: %d\nSize: %d\nStarting Cluster Number: %d\n", dir[i].DIR_Attr,
                   dir[i].DIR_FileSize, dir[i].DIR_FirstClusterLow);
            fileFound = 1; //file is found, yay!!
            break;
          }
        }
        if (fileFound == 0)
        {
          printf("Error: File not found\n");
        }
      }
      else
      {
        printf("Error: File system image must be opened first.\n");
        continue;
      }
    } //stat command handling ends

    // Handling 'cd' commands
    if (strcmp(token[0], "cd") == 0)
    {
      int dirFound = 0; //flag to check if directory is found or not

      if (token[1] == NULL)
      {
        continue;
      }

      if (file != NULL)
      {
        char *temp;
        char *token2 = strtok_r(token[1], " /", &temp); //to handle the relative cd
        while (token2 != NULL)
        {
          for (i = 0; i < 16; i++) //comparing with each directory entries
          {
            if ((strcmp(token2, ".") != 0) && (strcmp(token2, "..") != 0)) //to handle other commands than
                                                                           //cd . and ..
            {
              if (compare(dir[i].DIR_Name, strdup(token2)))
              {
                if (dir[i].DIR_Attr == (char)0x10) //checking if the token entered by user
                                                   //matches with directory name and
                                                   //not with file name
                {
                  dirFound = 1;
                  firstClusterAddress = LBAtoOffset(dir[i].DIR_FirstClusterLow);
                  readDirectoryEntry(file, firstClusterAddress, dir);
                  break;
                }
                else //if cd is followed by file name, not the directory name
                {
                  dirFound = 1; //dirfound is set to 1, so that we can dispay not a directory
                                // message instead of directory not found.
                  printf("%s is not a directory\n", strndup(dir[i].DIR_Name, 11));
                  token2 = NULL; //to terminate the while loop of tokenization, we wont go further
                                 // if the starting cd directory in relative path is not found
                  break;
                }
              }
            }
            else //to handle cd . and cd ..
            {
              //do nothing with cd .
              if ((strcmp(token2, ".") == 0))
              {
                dirFound = 1;
                break;
              }
              if ((strcmp(token2, "..") == 0)) //for cd ..
              {
                if (strncmp(dir[i].DIR_Name, "..", 2) == 0)
                {
                  if (dir[i].DIR_FirstClusterLow == 0x0000) // the subdirectory's clusterLow is
                                                            // this,then its parent directory is
                                                            // the root directory
                  {
                    dirFound = 1;
                    //finding the firstCluster of the root directory
                    firstClusterAddress = (BPB_NumFATs * BPB_FATSz32 * BPB_BytesPerSec) +
                                          (BPB_RsvdSecCnt * BPB_BytesPerSec);
                    readDirectoryEntry(file, firstClusterAddress, dir);
                    break;
                  }
                  else //for all other sub directories
                  {
                    dirFound = 1;
                    //finding starting cluster of parent directory of current directory
                    firstClusterAddress = LBAtoOffset(dir[i].DIR_FirstClusterLow);
                    //reading directoryentry of parent directory of current directory
                    readDirectoryEntry(file, firstClusterAddress, dir);
                    break;
                  }
                }
              }
            }
          }
          if (dirFound == 1)
          {
            token2 = strtok_r(NULL, " /", &temp); //checking if there is another relative path
          }
          else
          {
            token2 = NULL; //no directory is found,then we stop looking for next directory path
            dirFound = 0;
          }
        }
        if (dirFound == 0)
        {
          printf("Error: Directory not found\n");
        }
      }
      else
      {
        printf("Error: File system image must be opened first.\n");
        continue;
      }
    } // Handling CD command ends here finally. Fun with nested ifs.

    //handling READ command
    if (strcmp(token[0], "read") == 0)
    {
      if (token[1] == NULL || token[2] == NULL || token[3] == NULL)
      {
        printf("Error: read <filename> <position> <number of bytes>\n");
        continue;
      }

      int fileFound = 0;
      if (file != NULL)
      {
        int numberOfBytestoRead = atoi(token[3]);
        int positionToRead = atoi(token[2]);
        for (i = 0; i < 16; i++)
        {
          if (compare(dir[i].DIR_Name, strdup(token[1])))
          {
            fileFound = 1;
            if (dir[i].DIR_Attr == (char)0x10) //reject if directory name entered
            {
              printf("Error: Enter file name\n");
              break;
            }
            // checking if user is trying to read more bytes than the size of file
            if (numberOfBytestoRead > dir[i].DIR_FileSize)
            {
              printf("Error: Maximum size to read: %d\n", dir[i].DIR_FileSize);
              break;
            }

            //checking if user is trying to access byte that is more than the size of file
            if (positionToRead > dir[i].DIR_FileSize)
            {
              printf("Error: Maximum position to read: %d\n", dir[i].DIR_FileSize);
              break;
            }

            int clusterLow = dir[i].DIR_FirstClusterLow;         //low cluster of file in FAT
            int startingClusterNumber = LBAtoOffset(clusterLow); //starting cluster of file's data
            int remainingBytes = numberOfBytestoRead;

            //if position to read is > 512, then we move to next cluster becuase we dont
            //need to read current clusters data
            while (positionToRead > 512)
            {
              positionToRead = positionToRead - 512;
              startingClusterNumber = LBAtoOffset(NextLB(clusterLow));
            }

            //looping until all the bytes entered by the user are read
            while (remainingBytes > 0)
            {
              //sometime, user might enter 500 position, we can only read 12 bytes from current
              // cluster. this handles this situation
              int maxBytesToReadFromCurrentCluster = BPB_BytesPerSec - (positionToRead - 1);
              remainingBytes = numberOfBytestoRead - maxBytesToReadFromCurrentCluster;
              if (remainingBytes <= 0) //if remaining bytes is 0 or negative from current custer,
                                       //this handles reading
                                       //the acutal remaining bytes to read from current cluster
              {
                char *temp = (char *)malloc(numberOfBytestoRead);
                memset(temp, '\0', strlen(temp));
                fseek(file, startingClusterNumber + positionToRead - 1, SEEK_SET);
                fread(temp, numberOfBytestoRead, 1, file);
                printf("%s\n", strndup(temp, numberOfBytestoRead));
                free(temp);
              }
              else //if remaining bytes if more than 0 from current cluster, we read the bytes
                   //from current clusters and find another cluster of data and read it again
              {
                char *temp = (char *)malloc(numberOfBytestoRead - remainingBytes);
                memset(temp, '\0', strlen(temp));
                fseek(file, startingClusterNumber + positionToRead - 1, SEEK_SET);
                fread(temp, numberOfBytestoRead - remainingBytes, 1, file);
                numberOfBytestoRead = remainingBytes;
                printf("%s", temp);
                positionToRead = 1;                              //setting 1 becase we wanna read frm begining of next cluseter
                clusterLow = NextLB(clusterLow);                 //finding next cluster nubmer in FAT
                startingClusterNumber = LBAtoOffset(clusterLow); //finding the next block of data
                                                                 //in cluster region
                free(temp);
              }
            }
            break;
          }
        }
        if (fileFound == 0)
        {
          printf("Error: File not found\n");
        }
      }
      else
      {
        printf("Error: File system image must be opened first.\n");
        continue;
      }
    } //read command ends here.

    //handling GET command
    if (strcmp(token[0], "get") == 0)
    {
      if (token[1] == NULL)
      {
        printf("Error: Enter File name\n");
        continue;
      }
      int fileFound = 0;
      if (file != NULL)
      {
        for (i = 0; i < 16; i++)
        {
          if (compare(dir[i].DIR_Name, strdup(token[1])))
          {
            fileFound = 1;
            if (dir[i].DIR_Attr == (char)0x10) //checking if direcotry name instead
                                               //of file name is entered
            {
              printf("Error: Enter file name\n");
              break;
            }

            int bytesToRead = dir[i].DIR_FileSize; //we have to read this amount of
                                                   //bytes of the file
            char *result = (char *)malloc(dir[i].DIR_FileSize);
            int lowCluster = dir[i].DIR_FirstClusterLow;

            //looping until all the clusters of the file are read
            while (lowCluster != -1)
            {
              int startingCluster = LBAtoOffset(lowCluster); //finding the starting cluster of file
              fseek(file, startingCluster, SEEK_SET);
              if (bytesToRead < BPB_SecPerClus * BPB_BytesPerSec) //if bytes to read < 512 bytes
                                                                  //all data is in one cluster
              {
                fread(result, bytesToRead, 1, file);
                lowCluster = NextLB(lowCluster);
              }
              else //data is in different clusters or data is more than 513 bytes.
              {
                char *temp = (char *)malloc(BPB_SecPerClus * BPB_BytesPerSec);
                fread(temp, BPB_SecPerClus * BPB_BytesPerSec, 1, file);
                strcat(result, temp);
                lowCluster = NextLB(lowCluster); //finding the next cluster in the fat table.
                free(temp);
              }
            }
            char fileName[12]; //name of file to be created in sub directory
            memset(&fileName, '\0', sizeof(fileName));
            char *tok = strtok(strndup(dir[i].DIR_Name, 11), " ");
            int count = 0;
            while (tok != NULL)
            {
              strcat(fileName, tok);
              if (count == 0)
              {
                strcat(fileName, "."); //. for extension
                count++;
              }
              tok = strtok(NULL, " ");
            }

            FILE *filep = fopen(fileName, "w"); //create new file in current working directory
            fputs(result, filep);
            fclose(filep);
            free(result);
            break;
          }
        }
        if (fileFound == 0)
        {
          printf("Error: File not found\n");
        }
      }
      else
      {
        printf("Error: File system image must be opened first.\n");
        continue;
      }
    } //GET command ends here.

  } //initial while loop ends here.
  return 0;
} //end of main
