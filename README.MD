# Многопоточный сканер портов

## Описание

Утилита для сканирования диапазона TCP-портов на заданном IP-адресе с использованием многопоточности (std::async).  
Позволяет различать открытые, закрытые и фильтруемые (firewall) порты.

## Сборка

```bash
make
```

## Использование

```bash
./scanner <ip> <start_port> <end_port> <num_threads>
```

- `<ip>` — IP-адрес для сканирования (127.0.0.1)
- `<start_port>` — начальный порт диапазона
- `<end_port>` — конечный порт диапазона 
- `<num_threads>` — количество потоков (например, 2)

**Пример:**
```bash
./scanner 127.0.0.1 8079 8094 2
```

## Пример вывода

```bash
./scanner 127.0.0.1 8079 8094 2
Port 8079 is closed
Port 8080 is open
Port 8081 is closed
Port 8082 is closed
Port 8083 is closed
Port 8084 is closed
Port 8085 is filtered
Port 8086 is closed
Port 8087 is closed
Port 8088 is closed
Port 8089 is closed
Port 8090 is closed
Port 8091 is closed
Port 8092 is closed
Port 8093 is closed
Port 8094 is closed

```
