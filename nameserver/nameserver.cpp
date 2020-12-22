#include "../starter_files/DNSHeader.h"
#include "../starter_files/DNSQuestion.h"
#include "../starter_files/DNSRecord.h"

int PORT = 0;

int main(int argc, char *argv[]) {
    // ./nameserver [--geo|--rr] <port> <servers> <log>
    PORT = atoi(argv[2]);
    return 0;
}