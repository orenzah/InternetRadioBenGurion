

/**************************************************************/
/* This program uses the Select function to control sockets   */
/**************************************************************/
#include <stdio.h> 
#include <stdlib.h> 
#include <errno.h> 
#include <string.h> 
#include <sys/types.h> 
#include <sys/ipc.h>
#include <sys/msg.h>
#include <netinet/in.h> 
#include <sys/socket.h> 
#include <sys/wait.h> 
#include <pthread.h> 
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <time.h>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/ethernet.h> /* the L2 protocols */

#include "tcp_server.h"
#define MYPORT 3456    /* the port users will be connecting to */
#define BACKLOG 100    /* how many pending connections queue will hold */
#define BUFFER_SIZE 1024
#define MAX_SONGS 30

struct alloc_t
{
	void* p;
	struct alloc_t* next;
} typedef alloc_t;


/* Global variables */
node*	head;
int msqid;
uint32_t mcast_g;
uint16_t mcast_p;
key_t msgbox_key = 0;


song_node song_arr[MAX_SONGS] = {{ 0 }};
int	song_count = 0;
pthread_mutex_t fastmutex = PTHREAD_MUTEX_INITIALIZER;
key_t	msg_boxes[100]	= {0};
int		clients			= 1;		
client_node* clientsList = 0;
int tcp_port_g = 0;
int 					sockfd;
alloc_t* allocations = 0;

/* functions declarations */
void*				th_tcp_control(void *parg);
int					get_msg_type(char * buffer, size_t size);
upsong_msg			get_upsong_details(char * buffer, size_t size);
int					get_asksong_station(char * buffer, size_t size);
void print_ip(uint32_t ip);
void create_songs();
void create_song_transmitter();
void init_newstations_procedure(void);
void* malloc_and_cascade(size_t size);
void free_and_decascade(void* p);
void free_all_fd(client_node* head);
void free_all(alloc_t* tof);
void* song_transmitter(void* arg);
void signalStopHandler(int signo);
int check_msg_size(int type , size_t numBytes, char* buffer);
int main(int argc, char* argv[])
{
	int new_fd;  /* listen on sock_fd, new connection on new_fd */
	struct 	sockaddr_in 	my_addr;    /* my address information */
	struct 	sockaddr_in 	their_addr; /* connector's address information */
	size_t 					sin_size;
		
	struct 	timeval 		tv = {0};//The time wait for socket to be changed	*/
	fd_set 					readfds, writefds, exceptfds; /*File descriptors for read, write and exceptions */
	uint16_t tcp_port;
	if (argc < 5)
	{
		printf("error: not enough arguments\n");
		exit(1);
	}
	msgbox_key = ftok("/tmp/msgBox", 25);
	if ((msqid = msgget(15/*Warning key_t*/, IPC_CREAT | 0666 )) < 0) 
	{
		perror("msgget");
		exit(1);
	}	
	signal(SIGINT, signalStopHandler);

	sscanf(argv[1], "%hu", &tcp_port);
	inet_pton(AF_INET, argv[2], &(mcast_g));
	sscanf(argv[3], "%hu", &mcast_p);
	int i;
	for(i = 4; i < argc; i++)
	{
		FILE* songFile = fopen(argv[i], "r");
		song_node song = {0};
		int length = strlen(argv[i]);
		fseek(songFile, 0L, SEEK_END);
		size_t sz = ftell(songFile);
		song.songSize	= sz;
		song.nameLength = length;
		song.name = (char*)malloc_and_cascade(length);
		strcpy(song.name, argv[i]);
		song.station = song_count;
		pthread_t* songPlayer = (pthread_t*)malloc_and_cascade(sizeof(pthread_t));
		song.thread_p = songPlayer;
		song_arr[song_count] = song;
		song_count++;
		fclose(songFile);
		
		int* newStationPointer = (int*)malloc_and_cascade(sizeof(int));
		*newStationPointer = i-4;
		pthread_create(songPlayer, NULL, song_transmitter,newStationPointer/* &newStation*/);
	}
	//create_songs();
	
	

	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	FD_ZERO(&exceptfds);

	

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) 
	{
		perror("socket");
		exit(1);
	}
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) < 0)
	{
		perror("setsockopt(SO_REUSEADDR) failed");
	}
    
	//cascadeClient(sockfd, 0, &clientsList);
	my_addr.sin_family = AF_INET;         /* host byte order */
	tcp_port_g = tcp_port;
	my_addr.sin_port = htons(tcp_port);     /* short, network byte order */
	my_addr.sin_addr.s_addr = INADDR_ANY; /* auto-fill with my IP */
	bzero(&(my_addr.sin_zero), 8);        /* zero the rest of the struct */
	
	if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) \
																  == -1) {
		perror("bind");
		exit(1);
	}
	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	while(1) 
	{  /* main accept() loop */
	FD_SET(sockfd, &readfds); /*Add sock_fd to the set of file descriptors to read from */
	tv.tv_sec = 30; 				/*Initiate time to wait for fd to change */
	if (select(sockfd + 1, &readfds, 0, 0, &tv) < 0) {
		   perror("select");
		   continue;
		}
		sin_size = sizeof(struct sockaddr_in);
		if ((new_fd = accept(sockfd, (struct sockaddr *)&their_addr, \
													  (socklen_t *)&sin_size)) == -1) {
			perror("accept");
			continue;
		}
		pthread_t* thread_pt = (pthread_t*)malloc_and_cascade(sizeof(pthread_t));
		node* temp = (node*)malloc_and_cascade(sizeof(node));
		temp->pointer = thread_pt;
		temp->next = head;
		head = temp;
		int* pfd = (int*)malloc_and_cascade(sizeof(int));
		
		
		//*p_key = ftok("msgFile", 1024); // Create message boxex
		
		void* args[3];
		args[0] = (void*)2; //how many args been passed
		(*pfd) 	= new_fd;
		args[1] = pfd;
		int* client_id = (int*)malloc_and_cascade(sizeof(int));
		*client_id = clients;
		clients++;
		args[2] = client_id;
		
		cascadeClient(new_fd, client_id, &clientsList);
		printf("Server got a %snew connection%s with fd = %d\n",KGRN ,KNRM,new_fd);
		pthread_create(thread_pt, NULL, th_tcp_control, (void*)args);

	
	}
}

void *th_tcp_control(void *parg)
{
	void** args = (void**)parg;
	int mytype	= *((int*)args[2]);
	int client_fd = *((int*)args[1]);
	char buffer[BUFFER_SIZE] = {0};
	
	
	size_t struct_size;
	char* buf2snd;

	printf("New client thread created, controlling socket %d\n\r", client_fd);
	struct timeval timeout = {0};
	timeout.tv_usec = 1000000;
	setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
	ssize_t numBytesRcvd = recv(client_fd, buffer, BUFFER_SIZE, 0);
	buffer[numBytesRcvd] = '\0';
	if(get_msg_type(buffer, numBytesRcvd) != 0)
	{

		//prepare InvalidCommand
		invalid_msg inv_msg = {0};
		inv_msg.replyType = 3;
		strcpy(inv_msg.text ,"Shtok Tzair"); 
		inv_msg.replySize = strlen(inv_msg.text);
		
		size_t buf_size = inv_msg.replySize + 2;
		buf2snd = (char*)malloc_and_cascade(buf_size);
	
		memcpy(buf2snd, &inv_msg, buf_size);
		if (send(client_fd, buf2snd, buf_size, 0) == -1)
		{
			perror("send invalid_command");
		}
		printf("error: ");
		printf("rude client has been connected without saying Hello\n");		
		free_and_decascade(buf2snd);
	}
	else
	{
		struct welcome_msg msg	= {0};
		struct_size =  9;//sizeof(struct welcome_msg);
		msg.replyType			= 0;
		msg.numStations			= htons(song_count);
		msg.multicastGroup		= htonl(mcast_g);
		msg.portNumber			= htons(mcast_p);
		
		//struct_size = 1+2+4+2;
		
		buf2snd = (char*)malloc_and_cascade(struct_size);
		memcpy(buf2snd, &(msg.replyType), 1);
		memcpy((uint16_t*)(buf2snd + 1), &(msg.numStations), 2);
		memcpy((uint32_t*)(buf2snd + 3), &(msg.multicastGroup), 4);
		memcpy((uint16_t*)(buf2snd + 7), &(msg.portNumber), 2);
		//memcpy(buf2snd, &msg, struct_size);
		
		send(client_fd, buf2snd, struct_size, 0);
		free_and_decascade(buf2snd);
	}
	while(1)
	{
		ssize_t numBytesRcvd = recv(client_fd, buffer, BUFFER_SIZE, 0);
		if (numBytesRcvd == 0) 
		{
			//close connection

			printf("%sClosing%s socket and thread\n", KRED, KNRM);
			client_node* temp = clientsList;
			while (temp && (temp->clientId != mytype))
			{			
				if ((temp->next) == NULL)
				{
					break;
				}	
				temp = temp->next;			
			}
			if (temp->prev)
				(temp->prev)->next = temp->next;
			if (temp->next)
				(temp->next)->prev = temp->prev;

			free_and_decascade(temp);			
			close(client_fd);
			pthread_exit(0);
		}
		buffer[numBytesRcvd] = '\0';
		if(numBytesRcvd == 0)
			continue;
		int msg_type = get_msg_type(buffer, numBytesRcvd);
		if (check_msg_size(msg_type, numBytesRcvd, buffer))
		{
			
		}
		else
		{
			msg_type = -1; /// Go to Invalid Command
		}
		switch(msg_type)
		{
			case 1: /*	Client AskSong */
				/*TODO: Server tell client Announce*/
				{
					
					announce_msg msg = {0};
					
					uint16_t station = get_asksong_station(buffer, numBytesRcvd);					
					if (station > 100)
						break;
					struct_size =  sizeof(struct announce_msg);					
					
					if(song_arr[station].name == NULL)
					{
						msg.songNameSize	= 0;
						//msg.text			= NULL;
					}
					else
					{
						strcpy(msg.text, song_arr[station].name);
						msg.songNameSize = song_arr[station].nameLength;
					}							
					size_t buf_size = struct_size - 100 + strlen(msg.text);
					msg.replyType = 1;
					buf2snd = (char*)malloc_and_cascade(buf_size);
					memcpy(buf2snd, &(msg.replyType), 1);
					memcpy(buf2snd + 1, &(msg.songNameSize), 1);
					memcpy(buf2snd + 2, msg.text,strlen(msg.text));
					send(client_fd, buf2snd, struct_size, 0);
					free_and_decascade(buf2snd);
				}
				break;
			case 2: /*	Client UpSong */				
				/*TODO: Server tell client PermitSong*/
				{
					struct permit_msg msg = {0};
					enum permitEnum per;
					msg.replyType = 2;
					int status;
					if((status = pthread_mutex_trylock(&fastmutex)))
					{
						if(status == EBUSY)
						{
							per = no;
							msg.permit_value = (uint8_t)per;
							printf("%spermit: No.%s\n", KRED, KNRM);
						}
						else
						{
							perror("mutex try lock");
							exit(1); /* usage of exit is neccesary*/
						}
					}
					else
					{
						per = yes;
						msg.permit_value = (uint8_t)per;
						printf("%spermit: Yes.%s\n", KGRN, KNRM);
					}
					if (per != yes)
					{
						
					}
					
					/*TODO: check mutex, then answer permit*/
					buf2snd = (char*)malloc_and_cascade(2);
					memcpy(buf2snd, &msg, 2);
					send(client_fd, buf2snd, 2, 0);
					free_and_decascade(buf2snd);
					if (msg.permit_value)
					{
						upsong_msg theSong = get_upsong_details(buffer,numBytesRcvd);
						song_node song = {0};
						char songBuffer[BUFFER_SIZE] = {0};
						int len = 0, remain_data = theSong.songSize;
						char* songNameText = (char*)malloc_and_cascade(theSong.songNameSize*sizeof(char));
						strcpy(songNameText, theSong.songName);
						FILE* newsong = fopen(songNameText, "wb");
						
						song.songSize = theSong.songSize;
						song.nameLength = theSong.songNameSize;
						song.name = songNameText;
						song.station = song_count;
						song_arr[song_count++] = song;
						printf("Start downloading %s\n", songNameText);
						struct 	timeval tv = {0};		/*The time wait for socket to be changed	*/
						tv.tv_usec = 1000000;
						
						setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
						while ((len = recv(client_fd, songBuffer, BUFFER_SIZE, 0)) > 0)
						{
							if(len == 0)
							{
								break;
							}
							if(len < 0)
							{
								perror("recv");
								break;
							}
							if (len >= remain_data)
							{
								//printf("remain data: %d\n", remain_data);
								fwrite(songBuffer, sizeof(char), remain_data, newsong);
								break;
							}
							else
							{
								if(fwrite(songBuffer, sizeof(char), len, newsong) != len)
								{
									perror("fwrite");
								}
							}
							remain_data -= len;
							//fprintf(stdout, "Receive %d bytes and we hope : %d bytes\n", len, remain_data);
								
						}
						if (len == -1)
						{
							if (errno == EAGAIN)
							{
								//Timeout occured
								//Send Invalid Command
								invalid_msg msg = {0};
								char inv_buf[140] = {0};
								msg.replyType = 3;
								strcpy(msg.text, "Invalid Command has been asserted");
								msg.replySize = strlen(msg.text);
								size_t buf_size = msg.replySize + 2;
								memcpy(inv_buf, &msg, buf_size);
								if (send(client_fd, inv_buf, buf_size, 0) == -1)
								{
									perror("send invalid_command");
								}
								printf("%sClosing%s socket and thread\n", KRED, KNRM);
								client_node* temp = clientsList;
								while (temp && (temp->clientId != mytype))
								{			
									if ((temp->next) == NULL)
									{
										break;
									}	
									temp = temp->next;			
								}
								if (temp->prev)
									(temp->prev)->next = temp->next;
								if (temp->next)
									(temp->next)->prev = temp->prev;

								free_and_decascade(temp);			
								close(client_fd);
								fclose(newsong);
								pthread_exit(0);
									
							}
						}
						fclose(newsong);
						printf("Song has been closed\n");						
						if(pthread_mutex_unlock(&fastmutex))
						{
							perror("mutex try lock");			
							close(client_fd);
							pthread_exit(0);
							exit(1); 
						}
						pthread_t* songPlayer = (pthread_t*)malloc_and_cascade(sizeof(pthread_t));
						int* newStation = (int*)malloc_and_cascade(sizeof(int));
						*newStation = song.station;
						pthread_create(songPlayer, NULL, song_transmitter, newStation);
						/* Recv the upload*/
						init_newstations_procedure();
						printf("done\n");
						continue;
					}
				}
				break;
			case 3:
				break;
			default:
				{
					invalid_msg msg = {0};
					char inv_buf[140] = {0};
					msg.replyType = 3;
					strcpy(msg.text, "Invalid Command has been asserted");
					msg.replySize = strlen(msg.text);
					size_t buf_size = msg.replySize + 2;
					memcpy(inv_buf, &msg, buf_size);
					if (send(client_fd, inv_buf, buf_size, 0) == -1)
					{
						perror("send invalid_command");
					}
					printf("%sClosing%s socket and thread\n", KRED, KNRM);
					client_node* temp = clientsList;
					while (temp && (temp->clientId != mytype))
					{			
						if ((temp->next) == NULL)
						{
							break;
						}	
						temp = temp->next;			
					}
					if (temp->prev)
						(temp->prev)->next = temp->next;
					if (temp->next)
						(temp->next)->prev = temp->prev;

					free_and_decascade(temp);			
					close(client_fd);
					pthread_exit(0);
									
				}
				break;
		}




		printf("Received %d bytes from fd: %d\n", numBytesRcvd, client_fd);		
	}
}
void init_newstations_procedure(void)
{
	//msgbox mymsg = {0};
	client_node* temp = clientsList;
	//strcpy(mymsg.text, "newstatio");
	newstations_msg temp_msg = {0};
	temp_msg.replyType = 4;
	temp_msg.station_number = htons(song_count - 1);
	size_t size_send = 3;//sizeof(temp_msg);
	char msgBuf[100] = {0};
	memcpy(msgBuf, &(temp_msg.replyType), 1);
	memcpy((uint16_t*)(msgBuf + 1), &(temp_msg.station_number), 2);
	//memcpy(msgBuf, &temp_msg, size_send);
	
	while (temp)
	{
		/*
		mymsg.mtype = temp->clientId;
		printf("mymsg.mtype: %d\n", mymsg.mtype);
		msgsnd(msqid, &mymsg, sizeof(mymsg), 0);
		*/
		if (temp->clientId < 0)
		{
			temp = temp->next;
			continue;
		}
		send(temp->fileDescriptor, msgBuf, size_send, 0);
		temp = temp->next;
		
	}
}
int get_msg_type(char * buffer, size_t size)
{
	uint8_t	type;
	memcpy(&type, buffer, sizeof(uint8_t));
	return type;
}

upsong_msg get_upsong_details(char * buffer, size_t size)
{
	struct upsong_msg msg =	{0};
	memcpy(&(msg.replyType), buffer, 1);
	memcpy(&(msg.songSize), (uint32_t*)(buffer + 1), 4);
	msg.songSize = ntohl(msg.songSize);
	memcpy(&(msg.songNameSize), buffer + 5, 1);
	memcpy(&(msg.songName), buffer + 6, msg.songNameSize);
	return msg;
}

int	get_asksong_station(char * buffer, size_t size)
{
	struct asksong_msg msg =	{0};
	memcpy(&(msg.replyType), buffer, 1);
	memcpy(&(msg.station_number), buffer + 1, 2);
	msg.station_number = ntohs(msg.station_number) - 1;
	if (msg.station_number < 0)
		return 0;
	return msg.station_number;
}

void create_songs()
{
	DIR *dirp;
    struct dirent *dp;
    dirp = opendir(".");
    if (!dirp) 
    {
        perror("opendir()");
        exit(1);
    }
	printf("Looking for songs\n");
	
	while ((dp = readdir(dirp))) 
	{
		int length = strlen(dp->d_name);
		if(dp->d_name[length-1] != '3' ||
		dp->d_name[length-2] != 'p' ||
		dp->d_name[length-3] != 'm' ||
		dp->d_name[length-4] != '.' )
		{
			continue;
		}
		
		printf("Song: %s\n", dp->d_name);
		song_node song = {0};
		/*
		 *
 *  	size_t	songSize;
		uint32_t nameLength;
		char* name;
		uint16_t station;
		* */
		FILE* songFile = fopen(dp->d_name, "r");
		fseek(songFile, 0L, SEEK_END);
		size_t sz = ftell(songFile);
		song.songSize	= sz;
		song.nameLength = length;
		song.name = (char*)malloc_and_cascade(length);
		strcpy(song.name, dp->d_name);
		song.station = song_count;
		song_arr[song_count] = song;
		song_count++;
		fclose(songFile);
	}
}

void print_ip(uint32_t ip)
{
    unsigned char bytes[4];
    bytes[0] = ip & 0xFF;
    bytes[1] = (ip >> 8) & 0xFF;
    bytes[2] = (ip >> 16) & 0xFF;
    bytes[3] = (ip >> 24) & 0xFF;   
    printf("%d.%d.%d.%d\n", bytes[0], bytes[1], bytes[2], bytes[3]);        
}

void* malloc_and_cascade(size_t size)
{
	//printf("going to allocate\n");
	if (!allocations)
	{
		void* p = malloc(size);
		alloc_t* ps = (alloc_t*)malloc(sizeof(alloc_t));
		ps->p = p;
		ps->next = 0;
		allocations = ps;
		return p;
	}
	else
	{
		alloc_t* temp = allocations;
		void* p = malloc(size);
		alloc_t* ps = (alloc_t*)malloc(sizeof(alloc_t));
		ps->p = p;
		ps->next = 0;
		while (temp->next)
		{
			temp = temp->next;
		}
		temp->next = ps;
		//printf("allocated %p with size %u\n", ps, size);
		return p;
	}
}
void free_and_decascade(void* p)
{
	//printf("dealloc %p\n", p);
	alloc_t* temp = allocations;
	alloc_t* prev = 0;
	
	if (allocations && (allocations->p == p))
	{
		free(p);
		free(allocations);
		allocations = 0;
		return;
	}
	while (temp->next)
	{
		prev = temp;
		if (temp->p == p)
		{
			prev->next = temp->next;
			free(p);
			free(temp);
			return;
		}
		
		temp = temp->next;
		
	}
	
	
}
void free_all_fd(client_node* head)
{
	if (head->next)
	{
		free_all_fd(head->next);
	}
	else
	{
		return;
	}
	//printf("closing: %d\n", head->fileDescriptor);
	close(head->fileDescriptor);
	free(head);
}
void free_all(alloc_t* tof)
{
	if (tof->next)
	{
		free_all(tof->next);
	}
	else
	{
		return;
	}
	free(tof->p);
	free(tof);
	return;
}
void signalStopHandler(int signo)
{
	if (signo != SIGINT)
	{
		return;
	}
	printf("\nFreeing all\n");
	free_all_fd(clientsList);
	free_all(allocations);
	close(sockfd);
	exit(0);
	
}


void* song_transmitter(void* arg)
{
	int station = *((int*)arg);
	free_and_decascade(arg);
	int sd;
	
	struct ip_mreq group;
	FILE* inputfile = fopen(song_arr[station].name,"rb");
	cascadeClient(fileno(inputfile),0 ,&clientsList);
	printf("Song file name: %s%s%s\n", KBLU,song_arr[station].name, KNRM);
	
	struct sockaddr_in multicastAddr;
		
	sd = socket(AF_INET, SOCK_DGRAM, 0 & IPPROTO_UDP);
	if (sd < 0)
	{
		perror("socket");
		exit(1);
	}
	
	int reuse = 1;
	if(setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse)) < 0) 
	{
	    perror("Setting SO_REUSEADDR error\n\r");
	    close(sd);
	    exit(1);
	}




	group.imr_multiaddr.s_addr = mcast_g + (station << 24);
	group.imr_interface.s_addr = INADDR_ANY;

	int mutlicastttl = 255;
	
	struct in_addr localInterface;
	localInterface.s_addr = INADDR_ANY;
	if(setsockopt(sd, IPPROTO_IP, IP_MULTICAST_IF, (char *)&localInterface, sizeof(localInterface)) < 0)
	{
	  perror("Setting local interface error");
	  exit(1);
	}
	if(setsockopt(sd, IPPROTO_IP, IP_MULTICAST_TTL, (void*)&mutlicastttl, sizeof(mutlicastttl)) < 0) 
	{
		perror("Adding ttl multicast group error");
		close(sd);
		exit(1);
	} 
	if(setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void*)&group, sizeof(group)) < 0) 
	{
		perror("Adding multicast group error");
		close(sd);
		exit(1);
	}
	

	


	/* Construct local address structure */
	memset(&multicastAddr, 0, sizeof(multicastAddr));   /* Zero out structure */
	multicastAddr.sin_family = AF_INET;                 /* Internet address family */
	multicastAddr.sin_addr.s_addr = mcast_g + (station << 24);		/* Multicast IP address */
	multicastAddr.sin_port = htons(mcast_p);       /* Multicast port */
	printf("Group Address: %s", KBLU); // %d\n", mcast_g + (station << 24));
	print_ip( mcast_g + (station << 24));
	printf("%s", KNRM);
	//printf("Group Address: %d\n", mcast_g + (station << 24));
	
	int bytes_streamed = 0;
	while(1)
	{
		char songBuffer[BUFFER_SIZE] = {0};		
				
		if (feof(inputfile))
		{
			rewind(inputfile);									
		}	
		size_t bytes = fread(songBuffer, 1, 1024, inputfile);
		if(sendto(sd, songBuffer, bytes, 0, (struct sockaddr*)&multicastAddr, sizeof(multicastAddr)) == -1) 
		{
			perror("Writing datagram message error");
			close(sd);
			exit(1);
		}
		/*
		if(send(raw_sd, songBuffer, 1024, 0) == -1) 
		{
			perror("Writing datagram message error");
			close(sd);
			exit(1);
		}*/
		bytes_streamed += 1024;
		usleep(62500);
		//TODO read again at section 3.3
		
		/*
		if (bytes_streamed > 128*128) 
		{
			end = clock();
			double cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
			int micro = (int)((1 - cpu_time_used)*1000*1000);
			printf("wait: %lf\n\r", micro);
			if (micro > 0)
			{
					usleep(micro);
			}
			printf("Delay by: %f\n\r", (0.25 - cpu_time_used));
			
			
			start = clock();
			bytes_streamed = 0;
			
		}
			*/	
		//maybe add sleep after full second passed
	}
	
}

int check_msg_size(int type , size_t numBytes, char* buffer)
{
	switch(type)
	{
		case 0:
			type = type*(numBytes == 3);
			break;
		case 1:
			type = type*(numBytes == 3);
			break;	
		case 2:
			if (numBytes > 6)
			{
				uint8_t len = *(buffer + 5);
				type = type*(numBytes == (6 + len));
			}
			else
			{
				type = 0;
			}			
			break;
		default:
			type = 0;
			break;					
	}
	return type;	
}
