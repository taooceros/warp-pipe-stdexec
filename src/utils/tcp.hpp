#ifndef TCP_H
#define TCP_H

#include <arpa/inet.h>
#include <array>
#include <asm-generic/socket.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <netinet/in.h>
#include <span>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <strings.h>
#include <sys/socket.h>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <vector>

class OrderedOutBandCommBase {
public:
  template <class T, size_t Extent = std::dynamic_extent>
  ssize_t read(std::span<T, Extent> buf)
    requires(std::is_trivially_copyable<T>::value)
  {
    ssize_t readed = 0;

    while (readed < buf.size_bytes()) {
      auto num_read =
          ::read(socketfd, buf.data() + readed, buf.size_bytes() - readed);
      if (num_read == -1) {
        perror("read failed");
        exit(EXIT_FAILURE);
      }

      readed += num_read;
    }

    return readed;
  }

  template <class T, size_t Extent = std::dynamic_extent>
  void write(std::span<T, Extent> buf)
    requires(std::is_trivially_copyable<T>::value)
  {
    auto num_send = ::send(socketfd, buf.data(), buf.size_bytes(), 0);

    if (num_send != buf.size_bytes()) {
      throw std::runtime_error("write failed");
    }

    printf("writed %zu bytes\n", num_send);
  }

  template <class T>
  void write(const T &data)
    requires(std::is_trivially_copyable<T>::value)
  {
    auto writed = ::send(socketfd, &data, sizeof(T), 0);

    if (writed != sizeof(T)) {
      throw std::runtime_error("write failed");
    }
  }

  template <typename T> void write_size(std::span<T> buf) {
    write(buf.size());
    write(buf);
  }

  template <typename T> std::vector<T> read_size() {
    auto read_size = read<size_t>();
    if (read_size == -1) {
      perror("read size failed");
      exit(EXIT_FAILURE);
    }

    std::vector<T> buf(read_size);
    read(std::span<T>{buf});
    return buf;
  }

  template <class T, size_t Extent = std::dynamic_extent>
  T read()
    requires(std::is_trivially_copyable<T>::value)
  {
    std::array<std::byte, sizeof(T)> buf;
    auto read = ::read(socketfd, static_cast<void *>(buf.data()), sizeof(T));
    if (read != sizeof(T)) {
      printf("read failed with readed %zu but supposed to read %zu when "
             "supposed with errorno %d: %s\n",
             read, sizeof(T), errno, strerror(errno));

      exit(EXIT_FAILURE);
    }
    return *reinterpret_cast<T *>(buf.data());
  }

  virtual ~OrderedOutBandCommBase() = default;

protected:
  int socketfd = 0;
};

class TcpServer : public OrderedOutBandCommBase {
public:
  TcpServer(TcpServer &&) = delete;
  TcpServer &operator=(TcpServer &&) = delete;
  // delete copy constructor
  TcpServer(const TcpServer &) = delete;

  // delete copy assignment
  TcpServer &operator=(const TcpServer &) = delete;

  int server_socket;
  struct sockaddr_in address {};
  int opt = 1;
  socklen_t addrlen = sizeof(address);

  explicit TcpServer(uint16_t port);

  ~TcpServer() override;
};

class TcpClient : public OrderedOutBandCommBase {
public:
  struct sockaddr_in serv_addr;

  TcpClient(TcpClient &&) = delete;
  TcpClient &operator=(TcpClient &&) = delete;
  // delete copy constructor
  TcpClient(const TcpClient &) = delete;

  // delete copy assignment
  TcpClient &operator=(const TcpClient &) = delete;

  int clientfd;

  TcpClient(const std::string &server_ip, uint16_t port);

  ~TcpClient() override;
};

#endif