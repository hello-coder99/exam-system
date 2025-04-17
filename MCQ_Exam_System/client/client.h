#ifndef CLIENT_H
#define CLIENT_H

#include <iostream>
#include <string>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include <map>
#include <unordered_map>
#include <random>
#include <algorithm>
#include <iomanip>
#include <fstream>
#include <chrono>

using namespace std;
using namespace std::chrono;

class ExamInfo {
    public:
        string name;
        int duration;
        int totalQuestions;
        string instructor;
        ExamInfo(string name, int duration, int totalQ, string instructor): name(name), duration(duration), totalQuestions(totalQ), instructor(instructor) {}
};

class Client {
private:
    int sock;
    string role, username, password;

    static map<int, int> shuffledQuestionMap; 
    static vector<vector<int>> shuffledOptionMap; 
    static vector<string> shuffledQuestions;
    static vector<vector<string>> shuffledOptions;
    static map<int, int> timeSpentPerQuestion;

    static void* studentHandler(void* arg);
    static void* instructorHandler(void* arg);

    static void manageExam(int duration, Client* client);
    static void decryptAndPrepareExam(const string& filePath, char key);
    static void xorEncryptDecrypt(const string& filePath, char key);
    static void receiveAndStoreExamQuestions(int sock, int examNumber); 
    static void dashboard(Client * client);
    static void displayPreparedQuestion(int index);
    static void handleExamSelection(Client* client, int& choice);
    static void parseAvailableExams(const string& examData);

    void authenticate();

public:
    static bool timeUp;
    static pthread_mutex_t timerMutex;
    Client(const string& ip, int port);
    void start();
};

#endif
