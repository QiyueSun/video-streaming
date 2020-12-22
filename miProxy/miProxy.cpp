#include <arpa/inet.h> //close
#include <array>       // std::array
#include <cstdio>
#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <iostream>
#include <string.h> //strlen
#include <string>
#include <vector>
#include <sys/socket.h>
#include <sys/time.h> //FD_SET, FD_ISSET, FD_ZERO, FD_SETSIZE macros
#include <sys/types.h>
#include <unistd.h> //close
#include <unordered_map>
#include <assert.h>
#include <iomanip>
#include <fstream>


#define MAXCLIENTS 30
#define SERVERPORT 80
#define MAX_MESSAGE_LEN 1000000


using namespace std;

unordered_map<int, int> server_client_map;
unordered_map<int, int> client_server_map;
vector<int> available_bitrates;
double T_cur = 0; // unit of Kbps
double alpha;
int PORT = 8888;
int lowest_bitrate = 100000;
string path_to_logging = "";

struct logging_info {
    string browser_ip;
    string chunkname;
    string server_ip;
    double duration;
    double tput;
    double avg_tput;
    int bitrate;
    bool valid;
};

void logging(struct logging_info * info) {
    // <browser-ip> <chunkname> <server-ip> <duration> <tput> <avg-tput> <bitrate>
    // TODO: overwrite log
    if (info->valid) {
        ofstream logging_file;
        logging_file.open(path_to_logging, ios_base::app);

        logging_file << info->browser_ip << " " << info->chunkname << " " << info->server_ip << " " <<
                        fixed << setprecision(3) << info->duration << " " << info->tput << " " <<
                        info->avg_tput << " " << info->bitrate << endl;
        
        logging_file.close();
    }
}

vector<int> get_bitrates(char* big_buck_bunny) {
    vector<int> result;
    char* start_pointer = big_buck_bunny;
    char* end_pointer = big_buck_bunny;
    while (true) {
        start_pointer = strstr(end_pointer, "<media");
        if (start_pointer == nullptr) {
            break;
        }
        end_pointer = strstr(start_pointer, "</media>");
        char* bitrate_pointer = strstr(start_pointer, "bitrate=");
        int bitrate_idx = bitrate_pointer - big_buck_bunny;
        int i = bitrate_idx + 9;
        assert(*(bitrate_pointer+8) == '"');
        while (true) {
            if (*(big_buck_bunny+i) == '"') {
                int b = stoi(string(bitrate_pointer + 9, i - (bitrate_idx + 9)));
                // find lowest_bitrate
                if (b < lowest_bitrate) {
                    lowest_bitrate = b;
                }
                result.push_back(b);
                break;
            }
            i++;
        }
    }
    T_cur = lowest_bitrate;

    printf ("parsed manifest file, available bitrates: ")
    for (int i = 0; i < result.size(); ++i) {
        printf("%d ", result[i]);
    }
    printf("\n");
    return result;
}

string change_request_bitrate(string request, double tput, struct logging_info* info) {
    double highest = tput / 1.5;

    int bitrate = lowest_bitrate;
    for (int i = 0; i < available_bitrates.size(); ++i) {
        if (available_bitrates[i] > bitrate && available_bitrates[i] < highest) {
            bitrate = available_bitrates[i];
        }
    }

    size_t start = request.find("/vod/") + 5;
    size_t chunkname = request.find("Seg");
    size_t end = request.find(" HTTP");
    
    info->bitrate = bitrate;
    info->chunkname = request.substr(chunkname, end - chunkname);

    string new_request = request.substr(0, start) + to_string(bitrate) + request.substr(chunkname);
    return new_request;
}

int get_master_socket(struct sockaddr_in *address) {
    int yes = 1;
    int master_socket;
    // create a master socket
    master_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (master_socket <= 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // set master socket to allow multiple connections ,
    // this is just a good habit, it will work without this
    int success =
        setsockopt(master_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (success < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // type of socket created
    address->sin_family = AF_INET;
    address->sin_addr.s_addr = INADDR_ANY;
    address->sin_port = htons(PORT);

    // bind the socket to localhost port 8888
    success = ::bind(master_socket, (struct sockaddr *)address, sizeof(*address));
    if (success < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    printf("---Listening on port %d---\n", PORT);

    // try to specify maximum of 3 pending connections for the master socket
    if (listen(master_socket, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    return master_socket;
}


int get_server_socket(int & client_sock, struct sockaddr_in *addr) {
    if (client_server_map.find(client_sock) != client_server_map.end()) {
        return client_server_map[client_sock];
    }

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
		perror("Error opening stream socket");
		return -1;
	}

	if (connect(sockfd, (struct sockaddr *) addr, sizeof(*addr)) == -1) {
		perror("Error connecting server socket");
		return -1;
	}

    server_client_map[sockfd] = client_sock;
    client_server_map[client_sock] = sockfd;

    return sockfd;
}

void recv_message(int sockfd, char *message_char, int& message_length, struct logging_info * info) {
    int MESSAGE_SIZE = 1024;
    char * buffer = new char[MESSAGE_SIZE + 1];
    string message = "";

    message_length = 0;
    int content_length;
    int header_end_pos;
    bool found_content_length = false;

    struct timeval start, end;
	  gettimeofday(&start, NULL);

    while (true) {
        int valread = read(sockfd, buffer, 1024);
        if (valread == 0)
            printf("server sends message length 0");

        if (valread < 0)
            perror("ERROR on receive from server");

        buffer[valread] = '\0';
        memcpy(message_char + message_length, buffer, valread);
        message += buffer;
        message_length += valread;

        if (!found_content_length) {
            char* header_end_pointer = strstr(message_char, "\r\n\r\n");
            header_end_pos = header_end_pointer - message_char;

            if (header_end_pointer != nullptr) {
                char* content_length_pos = strstr(message_char, "Content-Length: ");
                assert(content_length_pos != nullptr);
                char* end = content_length_pos + 16;
                while (*end != '\r') {
                    assert(*end >= '0' && *end <= '9');
                    end++;
                }
                content_length = stoi(string(content_length_pos+16, end - (content_length_pos + 16)));
                found_content_length = true;
            }
        }

        if (found_content_length) {
            if (message_length - (header_end_pos + 4) >= content_length) {
                gettimeofday(&end, NULL); 
                break;
            }
        }
    }
    double time_taken = (end.tv_sec - start.tv_sec) * 1e6; 
	  time_taken = (time_taken + (end.tv_usec - start.tv_usec)) * 1e-6;
    double T_new = message_length / 1024 / time_taken * 8;
    T_cur = alpha * T_new + (1 - alpha) * T_cur;

    info->duration = time_taken;
    info->tput = T_new;
    info->avg_tput = T_cur;

    delete[] buffer;
}

int send_message(int sockfd, const char* to_send, int to_send_length) {
    int MESSAGE_SIZE = 1024;
    char * msg = new char[MESSAGE_SIZE + 1];
    int send_start = 0;
    int message_size;

    while (send_start < to_send_length) {
        if (to_send_length - send_start < MESSAGE_SIZE) {
            message_size = to_send_length - send_start;
        }
        else {
            message_size = MESSAGE_SIZE;
        }

        // strcpy(msg, to_send.substr(send_start, message_size).c_str());
        memcpy(msg, to_send + send_start, message_size);
        int send_len = send(sockfd, msg, message_size, 0);
        if (send_len == -1) {
            perror("Error sending on stream socket");
            delete[] msg;
            return -1;
        }
        send_start += send_len;
    }
    assert(send_start == to_send_length);

    delete[] msg;
    return 0;
}

/*
    Listen video messages from server, forward to client
*/
int send_to_client(int & server_sock, string path_to_logging, struct logging_info * info) {
    auto it = server_client_map.find(server_sock);
    assert(it != server_client_map.end());
    int client_sock = it->second;
    char* message_char = new char[MAX_MESSAGE_LEN];
    int message_length = 0;
    recv_message(server_sock, message_char, message_length, info);
    // forward to client
    send_message(client_sock, message_char, message_length);
    
    delete[] message_char;
    return 0;
}


/*
    Receive http requests from client_sock, forward to server
*/
int send_to_server(int& client_sock, struct sockaddr_in *client_addr, struct sockaddr_in *serv_addr, struct logging_info * info) {
    int server_socket, valread, header_len = 0;
    char buffer[1025];
    char* client_header = new char[MAX_MESSAGE_LEN];
    int addrlen = sizeof(client_addr);

    getpeername(client_sock, (struct sockaddr *)client_addr, (socklen_t *)&addrlen);

    info->browser_ip = string(inet_ntoa(client_addr->sin_addr));
    info->server_ip = string(inet_ntoa(serv_addr->sin_addr));

    while (true) {
        valread = read(client_sock, buffer, 1024);

        if (valread < 0)
            perror("ERROR on receive");

        if (valread == 0) {
            close(client_sock);
            client_sock = 0;
        }

        buffer[valread] = '\0';
        memcpy(client_header + header_len, buffer, valread);
        header_len += valread;
        client_header[header_len] = '\0';

        if (header_len > 4 && strcmp(client_header+header_len-4, "\r\n\r\n") == 0) {
            printf("\n---New message from client---\n");
            printf("Received from: ip %s , port %d \n",
                inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port));
            break;
        }
    }

    server_socket = get_server_socket(client_sock, serv_addr);
    char* second_space = strstr(client_header+4, " ");
    string path_to_file = string(client_header+4, second_space - client_header - 4);

    if (path_to_file == "/vod/big_buck_bunny.f4m") {
        // if path == "/vod/big_buck_bunny.f4m"
        if (available_bitrates.size() == 0) {
            if (send_message(server_socket, client_header, header_len) == -1) {
                perror("Error sending on stream socket");
                delete[] client_header;
                return -1;
            }
            char* message_char = new char[MAX_MESSAGE_LEN];
            int message_length = 0;
            recv_message(server_socket, message_char, message_length, info);
            available_bitrates = get_bitrates(message_char);
            delete[] message_char;
        }

        char* end_of_line_one = strstr(client_header, "\r\n");
        char* get_nolist_http = new char[MAX_MESSAGE_LEN];
        strcpy(get_nolist_http, "GET /vod/big_buck_bunny_nolist.f4m HTTP/1.1\r\n");
        strcpy(get_nolist_http + strlen(get_nolist_http), end_of_line_one+2);
        printf("miProxy send nolist to server\n\n");

        if (send_message(server_socket, get_nolist_http, strlen(get_nolist_http)) == -1) {
            perror("Error sending on stream socket");
            delete[] client_header;
            delete[] get_nolist_http;
            return -1;
        }
        delete[] get_nolist_http;
    }
    else if (path_to_file.substr(0, 4) == "/vod") {
        // if path == "/vod/500Seg2-Frag3"
        // get part of logging info
        info->valid = true;
        printf("miProxy send video chunk request to server\n\n");
        string new_request = change_request_bitrate(string(client_header), T_cur, info);
        if (send_message(server_socket, new_request.c_str(), new_request.length()) == -1) {
            perror("Error sending on stream socket");
            delete[] client_header;
            return -1;
        }
    }
    else {
        // else: directly forward
        printf("miProxy send other request to server\n\n");
        if (send_message(server_socket, client_header, header_len) == -1) {
            perror("Error sending on stream socket");
            delete[] client_header;
            return -1;
        }
    }
    delete[] client_header;
    return server_socket;
}


int main(int argc, char *argv[]) {
    // ./miProxy --nodns <listen-port> <www-ip> <alpha> <log>
    // ./miProxy --dns <listen-port> <dns-ip> <dns-port> <alpha> <log>
    PORT = atoi(argv[2]);
    struct in_addr server_ip;
    struct sockaddr_in serv_addr;
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVERPORT);
    bool use_dns = (argc == 7);

    if (strcmp(argv[1], "--nodns") == 0) {
        serv_addr.sin_addr.s_addr = inet_addr(argv[3]);
        printf("Server address: ip %s , port %d \n",
                inet_ntoa(serv_addr.sin_addr), ntohs(serv_addr.sin_port));
        alpha = atof(argv[4]);
        path_to_logging = string(argv[5]);
    }
    else {
        struct in_addr dns_ip;
        inet_aton(argv[3], &dns_ip);
        int dns_port = atoi(argv[4]);
        alpha = atof(argv[5]);
        path_to_logging = string(argv[6]);
    }
    
    int master_socket, addrlen, activity;
    std::array<int, MAXCLIENTS> client_sockets;
    client_sockets.fill(0);

    struct sockaddr_in address;
    master_socket = get_master_socket(&address);

    // accept the incoming connection
    addrlen = sizeof(address);
    puts("Waiting for connections ...");
    // set of socket descriptors
    fd_set readfds;
    while (true) {
        // clear the socket set
        FD_ZERO(&readfds);

        // add master socket to set
        FD_SET(master_socket, &readfds);
        for (const auto &client_sock : client_sockets) {
            if (client_sock != 0) {
                FD_SET(client_sock, &readfds);
            }
        }
        // wait for an activity on one of the sockets , timeout is NULL ,
        // so wait indefinitely
        activity = select(FD_SETSIZE, &readfds, NULL, NULL, NULL);
        if ((activity < 0) && (errno != EINTR)) {
            perror("select error");
        }

        // If something happened on the master socket ,
        // then its an incoming connection, call accept()
        if (FD_ISSET(master_socket, &readfds)) {
            int new_socket = accept(master_socket, (struct sockaddr *)&address,
                                    (socklen_t *)&addrlen);
            if (new_socket < 0) {
                perror("accept");
                exit(EXIT_FAILURE);
            }

            // inform user of socket number - used in send and receive commands
            printf("\n---New host connection from client---\n");
            printf("socket fd is %d , ip is : %s , port : %d \n", new_socket,
                    inet_ntoa(address.sin_addr), ntohs(address.sin_port));

            // add new socket to the array of sockets
            for (auto &client_sock : client_sockets) {
                // if position is empty
                if (client_sock == 0) {
                    client_sock = new_socket;
                    break;
                }
            }
        }

        // else it's some IO operation on a client socket
        for (auto &client_sock : client_sockets) {
            // Note: sd == 0 is our default here by fd 0 is actually stdin
            if (client_sock != 0 && FD_ISSET(client_sock, &readfds)) {
                struct logging_info info;
                info.valid = false;
                int server_sock = send_to_server(client_sock, &address, &serv_addr, &info);
                send_to_client(server_sock, path_to_logging, &info);
                logging(&info);
            }
        }
    }
    return 0;
}

