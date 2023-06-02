/*
File : Server program in basic chat system with custom protocol
Author : Connor Allred (cpa180001)
Modified : November 28, 2021 @ The Uniersity of Texas at Dallas
Description :
        - Program is a basic chat system that operates on the Linux OS
        - Program will consist of a server and client, with the server creating the socket
          and the client making a connection request and starting the chat
        - The chat will facilitate a "ping-pong" communication style with the client first
          sending a message and the server responding
        - Either the server or client can request a file to be transferred by sending FILE
          as a message and stating the file desired
        - The inputted file by the server/client will be used to create a new file in the 
          respective system and store the transferred data

        - As a result, THE FULL PATH OF THE DESIRED FILENAME WHEN REQUESTING IS NEEDED FOR THE
          REQUESTER'S USE IN SAVING THE TRANSFERRED FILE

        - When file request is made, the client/server (that did not make the request) will be
          prompted on whether to accept or decline the file request by a Y/N input
        - If Y is inuptted, the full filepath of the file will be needed by the respective user
          to send the full file, otherwise an empty file of the same name will be sent after
          a message acknowledging the file transfer acceptance and declaring when the file transfer
          is complete
        - The user that sends the file will wait for the other's response after a complete transfer
        - If N is inputted, the user that requested the file will be sent the rejection message
          and will be set to send another message

main()
    - Creates socket to performs communications
    - Intialzes all variables to be used in between functions and sockets other then temp variables
      used in each function individually
    - Closes all open file descriptors and performs necessary cleanup
    - Involved in establishing, connecting, and intiating sockets to be used in the chat program

FileSend()
    - Function for initiating the sending of files
    - Obtains filename and desired approach after receiving request
    - Sends files of any type

FileReceive()
    - Function for receiving file data from other user after sending request from SendMessage()
    - Returns back to sending a message if other user rejects request, otherwise file receiving 
      begins and saves in inputted file name/path

CheckConnection()
    - Checks connection with client before starting chat

SendMessage()
    - Function for initiating the sending of messages, file requests/refusals, and control messages
      to the other user (server in this case)
    - Also controls exit function of both users by sending exit messsage/flag to both connections (server and client)

ReceiveMessage()
    - Function for receiving all messages sent by client to manage file requests, control messages, or simple
      display of client's messages
    - Displays error messages if any

CreateHeader()
    - Creates the packet header to be sent through sockets

Exit()
    - Performs exit functions and ends program

Chat()
    - Endless loop that performs SendMessage() and ReceiveMessage until server or client chooses to leave
    - Also closes all file descriptors created in main

Header Fields :
Flags - 
        0 - 000 : EXIT code
        1 - 001 : (NONE - Simple Message/File sent)
        2 - 010 : Message Corruption/Error
        3 - 011 : File Corruption/Error 
        4 - 100 : ACK ACK
        6 - 110 : Connection Request ACK
        7 - 111 : Connection Request

Types -
        0 - 00 : Message Sent
        1 - 01 : File Request
        2 - 10 : File Request ACK
        3 - 11 : File Request IGNORED

Message Length

Message{ .................... }
*/

#include <iostream>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>


#ifdef _WIN32
  #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0501  /* Windows XP. */
  #endif
  #include <winsock2.h>
  #include <Ws2tcpip.h>
#else
  /* Assume that any non-Windows platform uses POSIX-style sockets instead. */
  #include <sys/socket.h>
  #include <arpa/inet.h>
  #include <netdb.h>  /* Needed for getaddrinfo() and freeaddrinfo() */
  #include <unistd.h> /* Needed for close() */
#endif

//List of predefined and global variables for easy scalability
#define DEFAULT_PORT 12345  //Default port number used if one is not entered
#define MAX_LENGTH 1024     //Max length of message that can be sent or received
#define MAX_CLIENTS 2       //Max number of clients that can connect to server
#define MAX_SIZE 1024       //Max size of buffer for transferring files

//Protocol header structure for sending messages/files/and error/control signals
struct MessageProtocol{
    unsigned int Type : 2;       //Specifies whether it is file or message base (2 bits)
    unsigned int Flags : 3;      //Delivers requests/errors/success messages (3 bits)
    unsigned int Length;         //Specifies length of message being sent
    char Message[MAX_LENGTH];    //Message or file being sent
};

//Function Prototypes for file transfer and 
bool FileSend(int, struct MessageProtocol, bool[]);        //Function for requesting to send file from server side
bool FileReceive(int, struct MessageProtocol, bool[]);     //Function for receiving file from client side
bool CheckConnection(int, struct MessageProtocol);         //Function for checking connection to client
bool SendMessage(int, struct MessageProtocol, bool[]);     //Function for sending message to client
bool ReceiveMessage(int, struct MessageProtocol, bool[]);   //Function for receiving and displaying message from client
struct MessageProtocol CreateHeader(int, int, char[]);      //Function for creating header for packet to be sent
void Exit(int, struct MessageProtocol);     //Function for sending exit signal to Server and ending chat
void Chat(int);                             //Function for performing chat functions

int main(int argc, char *argv[]){
    
    //Initialize the variables to be used
    int BaseSocketFD, NewSocketFD, PortNum;                 //Socket file descriptor for communicating with client and port number being used
    struct sockaddr_in ServerAddress, ClientAddress;        //Internet address of server and client
    socklen_t ClientAddressSize = sizeof(ClientAddress);    //Sizze of client struct used for accepting connection

    //Check to see if port number is given, otherwise default is used
    if(argc == 1){
        PortNum = DEFAULT_PORT;     //Use default port number since none is given
    }else{
        /***********************************************************
            atoi() throws no exception and instead will return 0 if 
            invalid input or failure in conversion is given, end 
            program if this happens
        ************************************************************/
        PortNum = atoi(argv[1]);    //Converts the first argument into int (inputted port number)
        if(PortNum == 0){
            std::cerr << "Invalid port number given, program terminated" << std::endl;
            exit(-1);   //End program on invalid entry
        }
    }

    //If on windows OS
    #ifdef _WIN32
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(1,1), &wsa_data);
    #endif

    //Create the socket to communicate with the client
    BaseSocketFD = socket(AF_INET, SOCK_STREAM, 0);     //Creates socket for IPv4, TCP, and default protocol
    if(BaseSocketFD < 0){
        std::cerr << "Socket creation for server failed, program terminated" << std::endl;   //Check if socket is properly created
        close(BaseSocketFD);
        exit(0);    //End program if not created
    }

    //Set up Socket and format respective server struct socket
    memset(&ServerAddress, '\0', sizeof(ServerAddress));
    ServerAddress.sin_family = AF_INET;                    //Set IP address used to IPv4
    ServerAddress.sin_addr.s_addr = INADDR_ANY;     //Avoids binding socket to specific IP address, accept any address on socket
    ServerAddress.sin_port = htons(PortNum);               //Port number to be used, given by user or the default value

    //Bind socket to port and begin listening for connection to be made with client
    if(bind(BaseSocketFD, (struct sockaddr *) &ServerAddress, sizeof(ServerAddress))){
        std::cerr << "Socket binding for server failed, program terminated" << std::endl;
        exit(-2);
    }

    //Server will listen for client to make a request before entering endless loop of sending/receiving
    std::cout << "Waiting for client to connect... " << std::endl;

    listen(BaseSocketFD, MAX_CLIENTS);    //Listen for MAX_CLIENTS for new connection on set socket
    NewSocketFD = accept(BaseSocketFD, (struct sockaddr *) &ClientAddress, &ClientAddressSize);

    //Check if connection was made sucessfully with client and server
    if(NewSocketFD < 0){
        std::cerr << "Socket connection for server/client failed, program terminated" << std::endl;
        exit(0);    
    }

    //Once request is made by client, client and server are connected and chat may begin
    std::cout << "Client connected! " << std::endl;

    //Display basic info and set format of chat
    std::cout << "\nChat is in session (EXIT to exit chat, FILE to request sending a file)" << std::endl;

    //Once connection is made, enter endless loop until Client or Server choose to exit
    Chat(NewSocketFD);

    //If on windows OS
    #ifdef _WIN32
        WSACleanup();
    #endif

    //Chat has been ended, close all sockets
    int status; 
    #ifdef _WIN32
        status = shutdown(BaseSocketFD, SD_BOTH);
        if (status == 0) { status = closesocket(BaseSocketFD); }
        status = shutdown(NewSocketFD, SD_BOTH);
        if (status == 0) { status = close(NewSocketFD); }
    #else
        status = shutdown(BaseSocketFD, SHUT_RDWR);
        if (status == 0) { status = close(BaseSocketFD); }
        status = shutdown(NewSocketFD, SHUT_RDWR);
        if (status == 0) { status = close(NewSocketFD); }
    #endif
}

void Chat(int NewSocketFD){
    struct MessageProtocol Packet;      //Create header packet to be used for sending data
    bool connected, End [1] = {false};

    //Check connection with client first
    do{
        connected = CheckConnection(NewSocketFD, Packet);
        //If connection is unsuccessful, try again
        if(!connected)std::cout << "Connection could not be established, trying again" << std::endl;
    }while(!connected);

    //Enter endless loop for sending and receiving messages
    while(true){
        //Wait first for client to send first message/file
        if(ReceiveMessage(NewSocketFD, Packet, End)){
            //Check if client has exited the chat
            if(End[0]){
                break; //End program
            }
            //Send response to Clients message/file. If false, Server has exited
            if(!SendMessage(NewSocketFD, Packet, End)){
                break; //End program
            }
        }
    }
}

struct MessageProtocol CreateHeader(int Type, int Flags, char Message[]){
    //Create temp header to be loaded with info and sent to Client
    struct MessageProtocol Packet;
    Packet.Type = Type;                         //Set Type depending on Server's request 
    Packet.Flags = Flags;                       //Set flag based on Server's request
    strcpy(Packet.Message, Message);            //Set Checksum for message being sent
    Packet.Length = strlen(Packet.Message);     //Attach message being sent
    //Return packet to be sent to Client
    return Packet;
}

bool CheckConnection(int NewSocketFD, struct MessageProtocol Packet){
    //Wait for client to send connection request
    #ifdef _WIN32
        recv(NewSocketFD, (char *)&Packet, sizeof(Packet), 0);
    #else
        recv(NewSocketFD, &Packet, sizeof(Packet), 0);
    #endif 

    //Check Flag
    if(Packet.Flags == 7){  //Connection request
        //Send ACK of connection request and then wait for ACK ACK from client
        Packet.Flags = 6;   //ACK connection request
        #ifdef _WIN32
            send(NewSocketFD, (char*)&Packet, sizeof(Packet), 0);
            //Wait for Server to send back ACK
            recv(NewSocketFD, (char *)&Packet, sizeof(Packet), 0);
        #else
            send(NewSocketFD, &Packet, sizeof(Packet), 0);
            //Wait for Server to send back ACK
            recv(NewSocketFD, &Packet, sizeof(Packet), 0);
        #endif
        
        //Check if ACK ACK is sent
        if(Packet.Flags != 4){
            return false;       //Connection interrupted, unsucessful, try again
        }else{
            return true;
        }
    }else{
       return false;    //Connection interrupted, unsucessful, try again
    }
}

bool FileSend(int NewSocketFD, struct MessageProtocol Packet, bool End[]){
    //Client is requesting file, select whether to send or not
    std::string input = "";
    std::cout << "Server is requesting " << Packet.Message << ". Send (Y/N) : " << std::endl;
    //Loop until valid input is given
    getline(std::cin, input);
    while(input != "Y" && input != "N"){
        std::cout << "Invalid input, try again : " << std::endl;
        getline(std::cin, input);
    }

    //Either send rejection signal or ACK signal
    if(input == "N"){
        //Send file transfer rejection packet adn wait for response back from Client
        Packet = CreateHeader(3,1,strcpy(Packet.Message, "Reject File Request"));
        #ifdef _WIN32
            send(NewSocketFD, (char *)&Packet, sizeof(Packet), 0);
        #else
            send(NewSocketFD, &Packet, sizeof(Packet), 0);
        #endif
        return true;
    }else{
        //Send file ACK to Client and begin preparing transfer
        Packet = CreateHeader(2,1,strcpy(Packet.Message, "Accepted File Request"));
        #ifdef _WIN32
            send(NewSocketFD, (char *)&Packet, sizeof(Packet), 0);
        #else
            send(NewSocketFD, &Packet, sizeof(Packet), 0);
        #endif
    }

    //Open desired file to be sent and calculate file size to send to Client
    std::string filename;
    std::cout << "Enter filename (include path if in different folder) : " << std::endl;
    getline(std::cin, filename);
    const char* Filename = filename.c_str();
    
    /*
        IF INPUTTED FILE IS WRONG OR EMPTY, EMPTY FILE WILL BE TRANSFERRED TO CLIENT
        AND FILE REQUEST WILL BE NEEDED AGAIN
    */

    //open file in binary to allow transfer of any file type
    FILE* File = fopen(Filename, "rb"); 

    //Send size of file to client for knowing when to stop
    fseek(File, 0L, SEEK_END);
    long int Size = ftell(File);
    #ifdef _WIN32
        send(NewSocketFD, (char *)&Size, sizeof(Size), 0);
    #else
        send(NewSocketFD, &Size, sizeof(Size), 0);
    #endif
    fclose(File);   //Close file to reset pointer to begining

    fopen(Filename, "rb");
    int bytes, total = 0;   //Track total bytes written and total bytes received at a time to send full file completely
    char Buffer[MAX_SIZE];  //Will hold data from file to be placed in socket and transfered
    //Loop till all bytes are read from file
    while(true){
        bytes = fread(Buffer, 1, MAX_SIZE, File);
        total += bytes;
        if(bytes > 0){
            //Send buffer if data is contained inside
            send(NewSocketFD, Buffer, bytes, 0);
        }else{
            //End loop if complete file has been sent
            if(total >= Size){
                break;
            }
        }
        //Clear buffer after each send to allow more data to be placed in
        #ifdef _WIN32
            memset(Buffer, '\0', MAX_SIZE);
        #else
            bzero(Buffer, MAX_SIZE);
        #endif
    }
    
    //Close output file
    fclose(File);
    std::cout<<"File Transfer complete! Client is replying"<<std::endl<<std::endl;
    return false;
}

bool FileReceive(int NewSocketFD, struct MessageProtocol Packet, bool End[]){
    //Send a request for desired file from Server
    std::string filename;
    std::cout << "Enter filename (include path if in different folder) : " << std::endl;
    getline(std::cin, filename);
    const char* Filename = filename.c_str();

    //Send filename to client, requesting transfer
    Packet = CreateHeader(1,1,strcpy(Packet.Message, Filename));
    #ifdef _WIN32
        send(NewSocketFD, (char *)&Packet, sizeof(Packet), 0);
    #else
        send(NewSocketFD, &Packet, sizeof(Packet), 0);
    #endif
    
    //Check response from client concerning file transfer
    if(ReceiveMessage(NewSocketFD, Packet, End)){
        long int FileSize;  //Store filesize to track if all data is received
        FILE *File;
        char Buffer[MAX_SIZE];  //Buffer for placing data into to allow file insertion
        
        //Receive file size
        #ifdef _WIN32
            recv(NewSocketFD, (char *)&FileSize, sizeof(FileSize), 0);
        #else
            recv(NewSocketFD, &FileSize, sizeof(FileSize), 0);
        #endif   

        File = fopen(Filename, "wb");   //Open output file as binary as data is given as binary
        int bytes;  //Track bytes receive for knowing when file is complete
        while((bytes = recv(NewSocketFD, Buffer, sizeof(Buffer), 0)) > 0){
            fwrite(&Buffer, 1, bytes, File);    //Wrtie data into file
            fflush(File);
            //Check if file is complete
            if(ftell(File) < FileSize){    

            }else{
                break;
                return true;
            }
            //Clear buffer before inputting additional data
            #ifdef _WIN32
                memset(Buffer, '\0', MAX_SIZE);
            #else
                bzero(Buffer, MAX_SIZE);
            #endif
        }
        //Close file and signal to client of its completion
        fclose(File);
        std::cout<<"File Transfer complete! Server waiting on reply"<<std::endl<<std::endl;
    }
    return true;
}

bool ReceiveMessage(int NewSocketFD, struct MessageProtocol Packet, bool End[]){
    //Get information sent from Client, message, file request, or control message
    #ifdef _WIN32
        recv(NewSocketFD, (char *)&Packet, sizeof(Packet), 0);
    #else
        recv(NewSocketFD, &Packet, sizeof(Packet), 0);
    #endif 

    //Check type for deciding correct process to proceed with
    switch(Packet.Type){
        case 0:     //Message has been sent, check if it is complete
            //Check if checksum is equal
            if(Packet.Length != strlen(Packet.Message)){
                //Packet corrupt, send error message
                Packet = CreateHeader(0,2,strcpy(Packet.Message, "Error, last message corrupted. Please try again"));
                #ifdef _WIN32
                    send(NewSocketFD, (char *)&Packet, sizeof(Packet), 0);
                #else
                    send(NewSocketFD, &Packet, sizeof(Packet), 0);
                #endif
                return false;
            }
            //Simple message sent, check if message is error to be handled
            switch (Packet.Flags){
                case 2:
                    //Packet corrupt, send error message
                    Packet = CreateHeader(0,2,strcpy(Packet.Message, "Error, last message corrupted. Please try again"));
                    #ifdef _WIN32
                        send(NewSocketFD, (char *)&Packet, sizeof(Packet), 0);
                    #else
                        send(NewSocketFD, &Packet, sizeof(Packet), 0);
                    #endif
                    return false;
                case 3:
                    //File corrupt, send error message
                    Packet = CreateHeader(0,3,strcpy(Packet.Message, "Error, last message corrupted. Please try again"));
                    #ifdef _WIN32
                        send(NewSocketFD, (char *)&Packet, sizeof(Packet), 0);
                    #else
                        send(NewSocketFD, &Packet, sizeof(Packet), 0);
                    #endif
                    return false;
                default:
                    //Display exit message from client and end chat
                    std::cout<<"- - CLIENT - -"<<std::endl;
                    std::cout<<Packet.Message<<std::endl << std::endl;
                    if(Packet.Flags == 0){End[0] = true;}
                    return true;
            }
        case 1:     //File request message
            //Client is requesting to send file, accept or ignore then wait for Client's reply
            return FileSend(NewSocketFD, Packet, End);
        case 2:     //File request approved
            //Client has ACK request to send file, display approval
            std::cout<<"- - CLIENT - -"<<std::endl;
            std::cout<<Packet.Message<<std::endl << std::endl;
            return true;
        case 3:     //File request ignored message
            //Client has ignored request to send file, display denial
            std::cout<<"- - CLIENT - -"<<std::endl;
            std::cout<<Packet.Message<<std::endl << std::endl;
            std::cout<<"File Transfer Rejected... Server waiting on reply"<<std::endl<<std::endl;
            return false;
        default:
            //Corruption in packet, invalid type provided
            Packet = CreateHeader(0,1,strcpy(Packet.Message, "Error, last request corrupted. Please try again"));
            #ifdef _WIN32
                send(NewSocketFD, (char *)&Packet, sizeof(Packet), 0);
            #else
                send(NewSocketFD, &Packet, sizeof(Packet), 0);
            #endif  //Send error message to client, asking for request to be sent again
            return false;   //Perform receive loop again in chat funtion
    }
}

bool SendMessage(int NewSocketFD, struct MessageProtocol Packet, bool End[]){
    //Get user input first (check if input is message or attempt to send file)
    std::string Input;
    getline(std::cin, Input);

    if(Input == "FILE"){
        //Client is requesting for file
        FileReceive(NewSocketFD, Packet, End);
        //Server will reply to client after transfer
        SendMessage(NewSocketFD, Packet, End);
    }else if(Input == "EXIT"){
        //Server is exiting the program, send exit code to server to follow suit
        Exit(NewSocketFD, Packet);
        return false;
    }else{
        //Send corresponding flags/type and message and then wait for response
        Packet = CreateHeader(0,1,strcpy(Packet.Message, Input.c_str()));
        #ifdef _WIN32
            send(NewSocketFD, (char *)&Packet, sizeof(Packet), 0);
        #else
            send(NewSocketFD, &Packet, sizeof(Packet), 0);
        #endif
    }
    return true;
}

void Exit(int NewSocketFD, struct MessageProtocol Packet){
    //Exiting program is represented by all 000, send exit code and exit chat
    Packet = CreateHeader(0,0,strcpy(Packet.Message, "Server has exited the chat..."));
    #ifdef _WIN32
        send(NewSocketFD, (char *)&Packet, sizeof(Packet), 0);
    #else
        send(NewSocketFD, &Packet, sizeof(Packet), 0);
    #endif
}