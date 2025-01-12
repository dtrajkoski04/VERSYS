#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <cstring>
#include <cstdlib>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <mutex>
#include <chrono>
#include <ldap.h>
#include <unordered_map>
#include <arpa/inet.h>


namespace fs = std::__fs::filesystem;

#define BUFFER_SIZE 1024

class TwMailerServer {
private:
    int server_socket;
    sockaddr_in server_address;
    std::string mail_spool_directory;
    std::mutex blacklist_mutex;
    std::unordered_map<std::string, int> login_attempts;
    std::unordered_map<std::string, std::chrono::time_point<std::chrono::system_clock>> blacklist;

    void handle_client(int client_socket, const std::string &client_ip) {
        char buffer[BUFFER_SIZE];
        bool authenticated = false;
        std::string session_username;

        while (true) {
            memset(buffer, 0, BUFFER_SIZE);
            int bytes_received = read(client_socket, buffer, BUFFER_SIZE);
            if (bytes_received <= 0) {
                std::cout << "Client disconnected." << std::endl;
                break;
            }

            std::istringstream input(buffer);
            std::string command;
            input >> command;

            if (command == "LOGIN") {
                authenticated = handle_login(input, client_socket, client_ip, session_username);
            } else if (!authenticated) {
                send_response(client_socket, "ERR\nLOGIN required\n");
            } else if (command == "SEND") {
                handle_send(input, client_socket, session_username);
            } else if (command == "LIST") {
                handle_list(client_socket, session_username);
            } else if (command == "READ") {
                handle_read(input, client_socket, session_username);
            } else if (command == "DEL") {
                handle_delete(input, client_socket, session_username);
            } else if (command == "QUIT") {
                break;
            } else {
                send_response(client_socket, "ERR\nUnknown command\n");
            }
        }

        close(client_socket);
    }

    bool handle_login(std::istringstream &input, int client_socket, const std::string &client_ip, std::string &session_username) {
        std::string username, password;
        input.ignore();
        std::getline(input, username);
        std::getline(input, password);

        std::lock_guard<std::mutex> lock(blacklist_mutex);

        if (blacklist.count(client_ip) > 0) {
            auto now = std::chrono::system_clock::now();
            if (now < blacklist[client_ip]) {
                send_response(client_socket, "ERR\nIP blacklisted\n");
                return false;
            } else {
                blacklist.erase(client_ip);
            }
        }

        LDAP *ldap_handle;
        int rc = ldap_initialize(&ldap_handle, "ldap://ldap.technikum.wien.at:389");
        if (rc != LDAP_SUCCESS) {
            send_response(client_socket, "ERR\nLDAP connection failed\n");
            return false;
        }

        std::string user_dn = "uid=" + username + ",dc=technikum-wien,dc=at";
        rc = ldap_simple_bind_s(ldap_handle, user_dn.c_str(), password.c_str());

        if (rc == LDAP_SUCCESS) {
            session_username = username;
            login_attempts[client_ip] = 0;
            send_response(client_socket, "OK\n");
            ldap_unbind_ext_s(ldap_handle, nullptr, nullptr);
            return true;
        } else {
            login_attempts[client_ip]++;
            if (login_attempts[client_ip] >= 3) {
                blacklist[client_ip] = std::chrono::system_clock::now() + std::chrono::minutes(1);
                send_response(client_socket, "ERR\nToo many login attempts\n");
            } else {
                send_response(client_socket, "ERR\nInvalid credentials\n");
            }
            ldap_unbind_ext_s(ldap_handle, nullptr, nullptr);
            return false;
        }
    }

    void handle_send(std::istringstream &input, int client_socket, const std::string &username) {
        std::string receiver, subject, message, line;
        input.ignore();
        std::getline(input, receiver);
        std::getline(input, subject);

        std::ostringstream message_stream;
        while (std::getline(input, line) && line != ".") {
            message_stream << line << "\n";
        }
        message = message_stream.str();

        std::string user_dir = mail_spool_directory + "/" + receiver;
        if (!fs::exists(user_dir)) {
            fs::create_directory(user_dir);
        }

        std::ofstream mail_file(user_dir + "/" + std::to_string(time(nullptr)) + ".txt");
        mail_file << "From: " << username << "\n";
        mail_file << "Subject: " << subject << "\n";
        mail_file << "Message:\n" << message;
        mail_file.close();

        send_response(client_socket, "OK\n");
    }

    void handle_list(int client_socket, const std::string &username) {
        std::string user_dir = mail_spool_directory + "/" + username;
        if (!fs::exists(user_dir)) {
            send_response(client_socket, "0\n");
            return;
        }

        std::ostringstream response;
        int count = 0;
        for (const auto &entry : fs::directory_iterator(user_dir)) {
            count++;
            std::ifstream mail_file(entry.path());
            std::string line;
            std::getline(mail_file, line); // Skip the "From" line
            std::getline(mail_file, line); // Get the subject line
            response << line.substr(9) << "\n"; // Extract subject
        }
        response << count << "\n";
        send_response(client_socket, response.str());
    }

    void handle_read(std::istringstream &input, int client_socket, const std::string &username) {
        int message_number;
        input >> message_number;

        std::string user_dir = mail_spool_directory + "/" + username;
        if (!fs::exists(user_dir)) {
            send_response(client_socket, "ERR\n");
            return;
        }

        auto it = fs::directory_iterator(user_dir);
        std::advance(it, message_number - 1);
        if (it == fs::end(fs::directory_iterator(user_dir))) {
            send_response(client_socket, "ERR\n");
            return;
        }

        std::ifstream mail_file(it->path());
        std::ostringstream mail_content;
        mail_content << mail_file.rdbuf();
        send_response(client_socket, "OK\n" + mail_content.str());
    }

    void handle_delete(std::istringstream &input, int client_socket, const std::string &username) {
        int message_number;
        input >> message_number;

        std::string user_dir = mail_spool_directory + "/" + username;
        if (!fs::exists(user_dir)) {
            send_response(client_socket, "ERR\n");
            return;
        }

        auto it = fs::directory_iterator(user_dir);
        std::advance(it, message_number - 1);
        if (it == fs::end(fs::directory_iterator(user_dir))) {
            send_response(client_socket, "ERR\n");
            return;
        }

        fs::remove(it->path());
        send_response(client_socket, "OK\n");
    }

    void send_response(int client_socket, const std::string &response) {
        write(client_socket, response.c_str(), response.size());
    }

public:
    TwMailerServer(int port, const std::string &spool_directory) : mail_spool_directory(spool_directory) {
        if (!fs::exists(mail_spool_directory)) {
            fs::create_directory(mail_spool_directory);
        }

        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket < 0) {
            perror("Socket creation failed");
            throw std::runtime_error("Socket creation failed");
        }

        server_address.sin_family = AF_INET;
        server_address.sin_addr.s_addr = INADDR_ANY;
        server_address.sin_port = htons(port);

        if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
            perror("Bind failed");
            throw std::runtime_error("Bind failed");
        }

        if (listen(server_socket, 5) < 0) {
            perror("Listen failed");
            throw std::runtime_error("Listen failed");
        }

        std::cout << "Server listening on port " << port << std::endl;
    }

    void run() {
        while (true) {
            sockaddr_in client_address;
            socklen_t client_len = sizeof(client_address);
            int client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_len);
            if (client_socket < 0) {
                perror("Accept failed");
                continue;
            }

            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_address.sin_addr, client_ip, INET_ADDRSTRLEN);
            std::cout << "Client connected from " << client_ip << std::endl;

            std::thread(&TwMailerServer::handle_client, this, client_socket, std::string(client_ip)).detach();
        }
    }

    ~TwMailerServer() {
        close(server_socket);
    }
};

int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: ./twmailer-server <port> <mail-spool-directory>" << std::endl;
        return EXIT_FAILURE;
    }

    try {
        int port = std::stoi(argv[1]);
        std::string mail_spool_directory = argv[2];

        TwMailerServer server(port, mail_spool_directory);
        server.run();
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
