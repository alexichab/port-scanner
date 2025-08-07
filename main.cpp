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

// Функция сканирования портов в заданном диапазоне
std::vector<int> scan_ports(const std::string& ip, int start, int end) {
    std::vector<int> open_ports;

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
            // Соединение установлено сразу
            open_ports.push_back(port);
        } else if (errno == EINPROGRESS) {
            // Соединение в процессе, проверяем с помощью select
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
                        // Соединение установлено успешно
                        open_ports.push_back(port);
                    }
                    // Если error != 0, порт закрыт, но мы это не выводим
                }
            }
        }
        close(sock);
    }
    return open_ports;
}


int main(int argc, char* argv[]) {
    // Проверка аргументов командной строки
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " <ip> <start_port> <end_port> <num_threads>" << std::endl;
        return 1;
    }

    // Парсинг аргументов
    std::string ip = argv[1];
    int start_port = std::stoi(argv[2]);
    int end_port = std::stoi(argv[3]);
    int num_threads = std::stoi(argv[4]);

    // Валидация входных данных
    if (start_port > end_port || num_threads <= 0) {
        std::cerr << "Invalid arguments: start_port must be <= end_port, num_threads must be > 0" << std::endl;
        return 1;
    }

    // Вычисление размера поддиапазона для каждого потока
    int total_ports = end_port - start_port + 1;
    int ports_per_thread = total_ports / num_threads;
    int extra_ports = total_ports % num_threads;

    // Вектор для хранения асинхронных задач
    std::vector<std::future<std::vector<int>>> futures;

    // Запуск потоков
    for (int i = 0; i < num_threads; i++) {
        int thread_start = start_port + i * ports_per_thread;
        int thread_end = thread_start + ports_per_thread - 1;
        if (i == num_threads - 1) thread_end += extra_ports;  // Учет остатка портов

        auto fut = std::async(std::launch::async, scan_ports, ip, thread_start, thread_end);
        futures.push_back(std::move(fut));
    }

    // Сбор и вывод результатов
    for (auto& fut : futures) {
        auto open_ports = fut.get();
        for (int port : open_ports) {
            std::cout << "Port " << port << " is open" << std::endl;
        }
    }

    return 0;
}
