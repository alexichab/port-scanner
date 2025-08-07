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
#include <csignal>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <thread>
#include <chrono>

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

std::atomic<bool> interrupted{false};

void signal_handler(int) {
    interrupted = true;
}

void print_help(const char* progname) {
    std::cout <<
        "Usage: " << progname << " <ip> <start_port> <end_port> <num_threads> [--open-only] [--output <file>] [--help]\n"
        "  <ip>           - IP address to scan\n"
        "  <start_port>   - Start of port range\n"
        "  <end_port>     - End of port range\n"
        "  <num_threads>  - Number of threads\n"
        "Options:\n"
        "  --open-only    - Show only open ports\n"
        "  --output FILE  - Write results to FILE\n"
        "  --help         - Show this help message\n";
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);

    // Аргументы
    if (argc < 5) {
        print_help(argv[0]);
        return 1;
    }

    // Парсим опции
    bool open_only = false;
    std::string output_file;
    for (int i = 5; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--open-only") open_only = true;
        else if (arg == "--help") {
            print_help(argv[0]);
            return 0;
        }
        else if (arg == "--output" && i + 1 < argc) {
            output_file = argv[++i];
        }
        else {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_help(argv[0]);
            return 1;
        }
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
    if (num_threads > total_ports) num_threads = total_ports; // Оптимизация потоков

    int ports_per_thread = total_ports / num_threads;
    int extra_ports = total_ports % num_threads;
    std::vector<std::future<std::vector<PortResult>>> futures;

    std::atomic<int> scanned_ports{0};
    std::atomic<bool> progress_done{false};

    std::thread progress_thread([&]() {
        int last_percent = -1;
        while (!progress_done) {
            int percent = (100 * scanned_ports) / total_ports;
            if (percent != last_percent) {
                std::cout << "\rScanning: " << percent << "% (" << scanned_ports << "/" << total_ports << ")   " << std::flush;
                last_percent = percent;
            }
            if (scanned_ports >= total_ports) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::cout << "\rScanning: 100% (" << scanned_ports << "/" << total_ports << ")   " << std::endl;
    });

    auto scan_ports_progress = [&](const std::string& ip, int start, int end) {
        std::vector<PortResult> results;
        for (int port = start; port <= end; port++) {
            if (interrupted) break;
            auto res = scan_ports(ip, port, port);
            results.insert(results.end(), res.begin(), res.end());
            ++scanned_ports;
        }
        return results;
    };
    
    int current_port = start_port;
    for (int i = 0; i < num_threads; i++) {
        int thread_start = current_port;
        int thread_end = thread_start + ports_per_thread - 1;
        if (i < extra_ports) thread_end++;
        if (thread_end > end_port) thread_end = end_port;
        current_port = thread_end + 1;

        auto fut = std::async(std::launch::async, scan_ports_progress, ip, thread_start, thread_end);
        futures.push_back(std::move(fut));
    }

    std::map<int, PortStatus> all_results;
    for (auto& fut : futures) {
        auto port_results = fut.get();
        for (const auto& pr : port_results) {
            all_results[pr.port] = pr.status;
        }
    }
    progress_done = true;
    progress_thread.join();

    std::ostream* out = &std::cout;
    std::ofstream fout;
    if (!output_file.empty()) {
        fout.open(output_file);
        if (!fout) {
            std::cerr << "Failed to open output file: " << output_file << std::endl;
            return 1;
        }
        out = &fout;
    }

    for (const auto& [port, status] : all_results) {
        if (open_only && status != PortStatus::Open) continue;
        std::string status_str;
        switch (status) {
            case PortStatus::Open: status_str = "open"; break;
            case PortStatus::Closed: status_str = "closed"; break;
            case PortStatus::Filtered: status_str = "filtered"; break;
        }
        *out << "Port " << port << " is " << status_str << std::endl;
    }

    if (interrupted) {
        *out << "\nScan interrupted by user. Partial results shown above.\n";
    }

    return 0;
}
