#include <iostream>
#include <vector>
#include <future>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <map>

// Добавляем перечисление и структуру для хранения статуса порта
enum class PortStatus { Open, Closed, Filtered };

struct PortResult {
    int port;
    PortStatus status;
};

// Добавлена фильтрация портов
std::vector<PortResult> scan_ports(const std::string& ip, int start, int end) {
    std::vector<PortResult> results;

    for (int port = start; port <= end; port++) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) continue;

        fcntl(sock, F_SETFL, O_NONBLOCK);

        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

        int res = connect(sock, (sockaddr*)&addr, sizeof(addr));
        if (res == 0) {
            results.push_back({port, PortStatus::Open});
        } else if (errno == EINPROGRESS) {
            fd_set write_fds;
            FD_ZERO(&write_fds);
            FD_SET(sock, &write_fds);

            timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;

            res = select(sock + 1, NULL, &write_fds, NULL, &tv);
            if (res > 0 && FD_ISSET(sock, &write_fds)) {
                int error = 0;
                socklen_t len = sizeof(error);
                if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len) == 0) {
                    if (error == 0) {
                        results.push_back({port, PortStatus::Open});
                    } else if (error == ECONNREFUSED) {
                        results.push_back({port, PortStatus::Closed});
                    } else {
                        results.push_back({port, PortStatus::Filtered});
                    }
                } else {
                    results.push_back({port, PortStatus::Filtered});
                }
            } else {
                // select timeout или ошибка
                results.push_back({port, PortStatus::Filtered});
            }
        } else if (errno == ECONNREFUSED) {
            results.push_back({port, PortStatus::Closed});
        } else {
            results.push_back({port, PortStatus::Filtered});
        }
        close(sock);
    }
    return results;
}


int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " <ip> <start_port> <end_port> <num_threads>" << std::endl;
        return 1;
    }

    std::string ip = argv[1];
    int start_port = std::stoi(argv[2]);
    int end_port = std::stoi(argv[3]);
    int num_threads = std::stoi(argv[4]);

    if (start_port > end_port || num_threads <= 0) {
        std::cerr << "Invalid arguments: start_port must be <= end_port, num_threads must be > 0" << std::endl;
        return 1;
    }

    int total_ports = end_port - start_port + 1;
    int ports_per_thread = total_ports / num_threads;
    int extra_ports = total_ports % num_threads;
    std::vector<std::future<std::vector<PortResult>>> futures;

    for (int i = 0; i < num_threads; i++) {
        int thread_start = start_port + i * ports_per_thread;
        int thread_end = thread_start + ports_per_thread - 1;
        if (i == num_threads - 1) thread_end += extra_ports;  // Учет остатка портов

        auto fut = std::async(std::launch::async, scan_ports, ip, thread_start, thread_end);
        futures.push_back(std::move(fut));
    }

    std::map<int, PortStatus> all_results;
    for (auto& fut : futures) {
        auto port_results = fut.get();
        for (const auto& pr : port_results) {
            all_results[pr.port] = pr.status;
        }
    }
    for (const auto& [port, status] : all_results) {
        std::string status_str;
        switch (status) {
            case PortStatus::Open: status_str = "open"; break;
            case PortStatus::Closed: status_str = "closed"; break;
            case PortStatus::Filtered: status_str = "filtered"; break;
        }
        std::cout << "Port " << port << " is " << status_str << std::endl;
    }

    return 0;
}
