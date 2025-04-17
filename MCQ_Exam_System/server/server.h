#ifndef SERVER_H
#define SERVER_H

#include <iostream>
#include <pthread.h>
#include <unordered_map>
#include <map>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <unordered_set>

#include "auth.h"
#include "exam_manager.h"

using namespace std;

class Server {
public:
    Server(int port);
    void start();
    static map<int, string> socketToUsername;
    
private:
    int server_socket;
    static void receiveStudentAnswers(int sock, const string& examName);
    static bool handle_authentication(int sock, const string& command, const string& user_type, const string& username, const string& password);    
    static void* handle_client(void* client_socket);
    static void handleStudentExamRequest(int sock, ExamManager exam);
    static string getCurrentDateTime();
    static void handleViewPerformance(int sock, const string& username);
    static void handleViewAttemptDetail(int sock, const string& username, const string& examName, const string& timestamp);
    static void sendAttemptTimestamps(int sock, const string& studentId, const string& selectedExam);
    static void sendAttemptDetails(int clientSock, const string& username);

};

#endif
