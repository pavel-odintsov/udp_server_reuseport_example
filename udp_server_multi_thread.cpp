#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <array>

#include <sys/socket.h>
#include <string.h>
#include <netdb.h>

std::array<uint64_t, 512> packets_per_thread;

bool create_and_bind_socket(std::size_t thread_id, const std::string& host, unsigned int port, uint32_t threads_per_port, int& sockfd) {
    std::cout << "Netflow plugin will listen on " << host << ":" << port << " udp port" << std::endl;

    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);

    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    
    // AI_PASSIVE to handle empty host as bind on all interfaces
    // AI_NUMERICHOST to allow only numerical host
    hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;

    addrinfo* servinfo = NULL;

    std::string port_as_string = std::to_string(port);

    int getaddrinfo_result = getaddrinfo(host.c_str(), port_as_string.c_str(), &hints, &servinfo);

    if (getaddrinfo_result != 0) {
        std::cout << "getaddrinfo function failed with code: " << getaddrinfo_result
               << " please check host syntax" << std::endl;
        return false;
    }

    sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);

    std::cout << "Setting reuse port" << std::endl;

    int reuse_port_optval = 1;

    auto set_reuse_port_res = setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &reuse_port_optval, sizeof(reuse_port_optval));

    if (set_reuse_port_res != 0) {
        std::cout << "Cannot enable reuse port mode"<< std::endl;
        return false;
    }

    int bind_result = bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen);

    if (bind_result) {
        std::cout << "Can't bind on port: " << port << " on host " << host
               << " errno:" << errno << " error: " << strerror(errno) << std::endl;

        return false;;
    }

    std::cout << "Successful bind" << std::endl;

    // Free up memory for server information structure
    freeaddrinfo(servinfo);

    return true;
}

void capture_traffic_from_socket(int sockfd, std::size_t thread_id) {
    std::cout << "Started capture" << std::endl;

    while (true) {
        const unsigned int udp_buffer_size = 65536;
        char udp_buffer[udp_buffer_size];

        int received_bytes = recv(sockfd, udp_buffer, udp_buffer_size, 0);

        if (received_bytes > 0) {
            packets_per_thread[thread_id]++;
        }
    }
}

void print_speed(uint32_t number_of_thread) {
    std::array<uint64_t, 512> packets_per_thread_previous = packets_per_thread;

    std::cout <<"Thread ID" << "\t" << "UDP packet / second" << std::endl; 

    while (true) {
	    std::this_thread::sleep_for(std::chrono::seconds(1));

	    for (uint32_t i = 0; i < number_of_thread; i++) {
            std::cout << "Thread " << i << "\t" << packets_per_thread[i] - packets_per_thread_previous[i] << std::endl;
	    }

	    packets_per_thread_previous = packets_per_thread;
    }
}

int main() {
    std::string host = "::";
    uint32_t port = 2055;

    uint32_t number_of_threads = 2;

    class worker_data_t {
        public:
            int socket_fd = 0;
            size_t thread_id = 0;
    };

    std::vector<worker_data_t> workers;;

    std::vector<std::thread> thread_group;

    for (size_t thread_id = 0; thread_id < number_of_threads; thread_id++) {
        int socket_fd = 0;

        bool result = create_and_bind_socket(thread_id, host, port, number_of_threads, socket_fd);

        if (!result) {
            std::cout << "Cannot create / bind socket" << std::endl;
            exit(1);
        }

        // Start traffic capture
        std::thread current_thread(capture_traffic_from_socket, socket_fd, thread_id);
        thread_group.push_back(std::move(current_thread));
    }

    // Add some delay to be sure that both threads started
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Start speed printer
    std::thread speed_printer(print_speed, number_of_threads);

    // Wait for all threads to finish
    for (auto& thread: thread_group) {
        thread.join();
    }
}
