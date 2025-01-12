#include <iostream>
#include <string>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024

class TwMailerClient {
private:
    int client_socket;
    sockaddr_in server_address;

    void send_command(const std::string &command) {
        char buffer[BUFFER_SIZE];
        write(client_socket, command.c_str(), command.size());

        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = read(client_socket, buffer, BUFFER_SIZE);
        if (bytes_received > 0) {
            std::cout << "Server response:\n" << buffer;
        } else {
            std::cerr << "Error reading server response or connection closed." << std::endl;
        }
    }

    void print_menu(bool authenticated) {
        std::cout << "\n--- TW-Mailer Client Menu ---\n";
        if (!authenticated) {
            std::cout << "1. LOGIN\n";
        } else {
            std::cout << "2. SEND Message\n";
            std::cout << "3. LIST Messages\n";
            std::cout << "4. READ Message\n";
            std::cout << "5. DELETE Message\n";
            std::cout << "6. QUIT\n";
        }
        std::cout << "Enter your choice: ";
    }

public:
    TwMailerClient(const std::string &server_ip, int server_port) {
        client_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (client_socket < 0) {
            perror("Socket creation failed");
            throw std::runtime_error("Socket creation failed");
        }

        server_address.sin_family = AF_INET;
        server_address.sin_port = htons(server_port);

        if (inet_pton(AF_INET, server_ip.c_str(), &server_address.sin_addr) <= 0) {
            perror("Invalid address/ Address not supported");
            throw std::runtime_error("Invalid server address");
        }

        if (connect(client_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
            perror("Connection failed");
            throw std::runtime_error("Connection to server failed");
        }

        std::cout << "Connected to server at " << server_ip << ":" << server_port << std::endl;
    }

    void run() {
        bool running = true;
        bool authenticated = false;

        while (running) {
            print_menu(authenticated);
            int choice;
            std::cin >> choice;
            std::cin.ignore();

            if (!authenticated && choice == 1) { // LOGIN
                std::string username, password;
                std::cout << "Enter username: ";
                std::getline(std::cin, username);
                std::cout << "Enter password: ";
                std::getline(std::cin, password);

                std::ostringstream command;
                command << "LOGIN\n" << username << "\n" << password << "\n";
                send_command(command.str());

                std::cout << "Attempting login..." << std::endl;
                authenticated = true; // Assume success; handle response on server

            } else if (authenticated) {
                switch (choice) {
                    case 2: { // SEND
                        std::string receiver, subject, message, line;
                        std::cout << "Enter receiver username: ";
                        std::getline(std::cin, receiver);
                        std::cout << "Enter subject: ";
                        std::getline(std::cin, subject);
                        std::cout << "Enter message (end with a single dot '.'): " << std::endl;
                        std::ostringstream message_stream;
                        while (std::getline(std::cin, line) && line != ".") {
                            message_stream << line << "\n";
                        }
                        message = message_stream.str();

                        std::ostringstream command;
                        command << "SEND\n" << receiver << "\n" << subject << "\n" << message << ".\n";
                        send_command(command.str());
                        break;
                    }
                    case 3: { // LIST
                        send_command("LIST\n");
                        break;
                    }
                    case 4: { // READ
                        int message_number;
                        std::cout << "Enter message number: ";
                        std::cin >> message_number;
                        std::cin.ignore();

                        std::ostringstream command;
                        command << "READ\n" << message_number << "\n";
                        send_command(command.str());
                        break;
                    }
                    case 5: { // DELETE
                        int message_number;
                        std::cout << "Enter message number: ";
                        std::cin >> message_number;
                        std::cin.ignore();

                        std::ostringstream command;
                        command << "DEL\n" << message_number << "\n";
                        send_command(command.str());
                        break;
                    }
                    case 6: { // QUIT
                        send_command("QUIT\n");
                        running = false;
                        break;
                    }
                    default:
                        std::cout << "Invalid choice. Please try again." << std::endl;
                }
            } else {
                std::cout << "Please login first." << std::endl;
            }
        }
    }

    ~TwMailerClient() {
        close(client_socket);
    }
};

int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: ./twmailer-client <server-ip> <server-port>" << std::endl;
        return EXIT_FAILURE;
    }

    try {
        std::string server_ip = argv[1];
        int server_port = std::stoi(argv[2]);

        TwMailerClient client(server_ip, server_port);
        client.run();
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}