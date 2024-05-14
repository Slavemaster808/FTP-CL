#include <string>
#include <iostream>
#include <algorithm>
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#define BUFFER_SIZE 1024

namespace ftp
{

    class Client
    {
    private:
        bool pass_mode_ = false;
        int control_socket_;
        int data_socket_;

    public:
        Client(const char *server_ip, int port);
        ~Client();
        void connect_to_server(int _socket, const char *server_ip, int port);
        std::string execute_command(const std::string &command);
        std::string print_server_response(int _socket);

        int login();
        void password();

        void passive_mode();
        // void active_mode();

        void list();
        void pwd();
        void change_dir(const std::string &command);

        int upload_file(const std::string &command);
        int download_file(const std::string &command);

        void help();
        void quit();
    };
} // namespace ftp

namespace
{
    void parse_addr_port(std::string &response, std::string &ip, int &port)
    {
        size_t start = response.find('(') + 1;
        size_t end = response.find(')');
        response = response.substr(start, end - start);

        std::vector<std::string> tokens;
        start = end = 0;
        while ((start = response.find_first_not_of(',', end)) != std::string::npos)
        {
            end = response.find(',', start);
            tokens.push_back(response.substr(start, end - start));
        }
        ip += tokens[0] + '.' + tokens[1] + '.' + tokens[2] + '.' + tokens[3];
        port = std::stoi(tokens[4]) * 256 + std::stoi(tokens[5]);
    }

    int parse_file_size(std::string pars_str)
    {
        size_t start = pars_str.find('(') + 1;
        size_t end = pars_str.find(')');
        pars_str = pars_str.substr(start, end - start);

        return std::stoi(pars_str.substr(0, pars_str.find(' ')));
    }

    void parse_file_path(std::string &file_path)
    {
        size_t pos = file_path.find_last_of('/');
        if (pos != std::string::npos)
        {
            file_path = file_path.substr(pos + 1);
        }
    }
} // namespace

namespace ftp
{

    Client::Client(const char *server_ip, int port)
    {
        control_socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (control_socket_ < 0)
        {
            throw std::runtime_error("Error creating socket!");
        }
        connect_to_server(control_socket_, server_ip, port);
        print_server_response(control_socket_);
    }

    Client::~Client()
    {
        close(control_socket_);
        close(data_socket_);
    }

    void Client::connect_to_server(int _socket, const char *server_ip, int port)
    {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = inet_addr(server_ip);
        address.sin_port = htons(port);

        if (connect(
                _socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) <
            0)
        {
            throw std::runtime_error("Connection error!");
            close(_socket);
        }
        else
        {
            std::cout << "Connection complete!\n";
        }
    }

    std::string Client::execute_command(const std::string &command)
    {
        char message[BUFFER_SIZE];
        sprintf(message, "%s\r\n", command.c_str());
        send(control_socket_, message, strlen(message), 0);
        return print_server_response(control_socket_);
    }

    std::string Client::print_server_response(int _socket)
    {
        char buff[BUFFER_SIZE];
        memset(buff, 0, sizeof(buff));
        recv(_socket, &buff, BUFFER_SIZE, 0);
        std::cout << buff;
        return buff;
    }

    int Client::login()
    {
        std::string username;
        std::cout << "USERNAME > ";
        std::cin >> username;
        if (execute_command("USER " + username).substr(0, 3) == "530")
        {
            return -1;
        };
        return 0;
    }

    void Client::password()
    {
        std::string passw;
        std::cout << "PASS > ";
        std::cin >> passw;
        execute_command("PASS " + passw);
    }

    void Client::passive_mode()
    {
        if (!pass_mode_)
        {
            std::string reply = execute_command("PASV");
            if (reply.substr(0, 3) != "530")
            {
                std::string ip_addr;
                int port = 0;
                parse_addr_port(reply, ip_addr, port);

                data_socket_ = socket(AF_INET, SOCK_STREAM, 0);
                if (data_socket_ < 0)
                {
                    throw std::runtime_error("Error creating socket!");
                }

                connect_to_server(data_socket_, ip_addr.c_str(), port);
                pass_mode_ = true;
            }
        }
    }

    void Client::list()
    {
        execute_command("LIST");
        if (pass_mode_)
        {
            print_server_response(data_socket_);
            print_server_response(control_socket_);
            close(data_socket_);
            pass_mode_ = false;
        }
    }

    void Client::pwd() { execute_command("PWD"); }

    void Client::change_dir(const std::string &command)
    {
        execute_command(command);
    }

    int Client::upload_file(const std::string &command)
    {
        std::string file_path;
        size_t it = command.find_first_of(' ');
        if (it != std::string::npos)
        {
            file_path = command.substr(it + 1);
        }
        FILE *file = fopen(file_path.c_str(), "rb");
        if (!file)
        {
            std::cerr << "File opening error!\n";
            return -1;
        }
        parse_file_path(file_path);
        std::string reply = execute_command("STOR " + file_path);
        if (reply.substr(0, 3) == "150")
        {
            int count;
            char databuf[BUFFER_SIZE];
            while (!feof(file))
            {
                count = fread(databuf, 1, BUFFER_SIZE, file);
                send(data_socket_, databuf, count, 0);
            }
            send(data_socket_, "\r\n", strlen("\r\n"), 0);
            fclose(file);
            close(data_socket_);
            pass_mode_ = false;
            print_server_response(control_socket_);
        }
        else
        {
            return -1;
        }
        return 0;
    }

    int Client::download_file(const std::string &command)
    {
        std::string file_path;
        size_t it = command.find_first_of(' ');
        if (it != std::string::npos)
        {
            file_path = command.substr(it + 1);
        }
        std::string reply = execute_command("RETR " + file_path);
        if (reply.substr(0, 3) == "150")
        {
            int file_size = parse_file_size(reply);
            FILE *file = fopen(file_path.c_str(), "wb");
            int read = 0;
            while (read < file_size)
            {
                char databuff[BUFFER_SIZE];
                int readed = recv(data_socket_, databuff, sizeof(databuff), 0);
                fwrite(databuff, 1, readed, file);
                read += readed;
            }
            fclose(file);
            close(data_socket_);
            pass_mode_ = false;
            print_server_response(control_socket_);
        }
        else
        {
            return -1;
        }

        return 0;
    }

    void Client::help()
    {
        std::cout << "PASV - passive mode.\n"
                  << "LIST - catalog check.\n"
                  << "PWD  - current catalog path.\n"
                  << "CWD <path> - catalog switch.\n"
                  << "RETR <file path> - get file from server.\n"
                  << "STOR <file path> - send file to server.\n"
                  << "QUIT - quit.\n"
                  << "HELP - help.\n";
    }

    void Client::quit() { execute_command("QUIT"); }

} // namespace ftp

int main(int argc, char *argv[])
{

    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <server_ip> <server_port>\n";
        return -1;
    }
    try
    {
        const char *server_ip = argv[1];
        int server_port = atoi(argv[2]);

        ftp::Client client(server_ip, server_port);

        if (client.login() == 0)
        {
            client.password();
        }

        while (1)
        {
            std::string command;
            std::cout << "> ";
            std::getline(std::cin, command);

            if (command == "USER")
            {
                if (client.login() == 0)
                {
                    client.password();
                }
            }
            else if (command == "PASV")
            {
                client.passive_mode();
            }
            else if (command == "LIST")
            {
                client.list();
            }
            else if (command == "PWD")
            {
                client.pwd();
            }
            else if (command.substr(0, 3) == "CWD")
            {
                client.change_dir(command);
            }
            else if (command.substr(0, 4) == "STOR")
            {
                client.upload_file(command);
            }
            else if (command.substr(0, 4) == "RETR")
            {
                client.download_file(command);
            }
            else if (command == "HELP")
            {
                client.help();
            }
            else if (command == "QUIT")
            {
                client.quit();
                break;
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
    }
}