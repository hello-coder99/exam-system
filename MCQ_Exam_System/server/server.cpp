#include "server.h"
#include <cctype>
#define INT_MIN -1000

static vector<string> exams;
map<int, string> Server::socketToUsername;

Server::Server(int port) {
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        cerr << "Error: Could not create server socket\n";
        exit(EXIT_FAILURE);
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        cerr << "Error: Could not bind to port\n";
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 5) == -1) {
        cerr << "Error: Could not listen for connections\n";
        exit(EXIT_FAILURE);
    }
    cout << "[+] Server started on port " << port << endl;
}

void Server::start() {
    AuthManager();
    ExamManager em;
    exams = em.load_exam_metadata("../data/exams/exam_list.txt");
    while (true) {
        int client_socket = accept(server_socket, nullptr, nullptr);
        pthread_t thread;
        pthread_create(&thread, nullptr, handle_client, &client_socket);
        pthread_detach(thread);
    }
}

void Server::receiveStudentAnswers(int sock, const string& examName) {
    char buffer[256] = {0};
    int bytesReceived = recv(sock, buffer, sizeof(buffer), 0);
    if (bytesReceived <= 0) {
        cerr << "Error: Failed to receive answers from client.\n";
        return;
    }
    string data(buffer);
    if (data.substr(0, 7) != "ANSWERS") {
        cerr << "Invalid data received format.\n";
        return;
    }

    string studentId = socketToUsername[sock];

    // Load correct answers
    string answerFile = "../data/exams/answers_" + examName + ".txt";
    vector<int> correctAnswers;
    ifstream answerIn(answerFile);
    string line;
    while (getline(answerIn, line)) {
        line.erase(remove_if(line.begin(), line.end(), ::isspace), line.end());
        if (!line.empty()) 
            correctAnswers.push_back(line[0] - 'A');
        
    }
    answerIn.close();

    istringstream dataStream(data.substr(8));
    string entry;
    int totalQuestions = correctAnswers.size();
    vector<int> perQuestionMarks(totalQuestions, 0);
    vector<int> perQuestionTime(totalQuestions, 0);
    vector<int> perQuestionAnswer(totalQuestions, -1);  // -1 means not attempted
    int totalMarks = 0, totalTimeSpent = 0;
    int attemptedCount = 0, wrongCount = 0;
    const int positiveMark = 4, negativeMark = -1;

while (getline(dataStream, entry)) {
    int qIdx, answer, timeSpent;
    char delim;
    istringstream entryStream(entry);
    entryStream >> qIdx >> delim >> answer >> delim >> timeSpent;
    int marks = 0;
    if (answer != -1) {
        attemptedCount++;
        if (answer == correctAnswers[qIdx]) {
            marks = positiveMark;
        } else {
            marks = negativeMark;
            wrongCount++;
        }
    }

    // Store at proper index
    perQuestionMarks[qIdx] = marks;
    perQuestionTime[qIdx] = timeSpent;
    perQuestionAnswer[qIdx] = answer;
    totalMarks += marks;
    totalTimeSpent += timeSpent;
}


    string currDateTime = getCurrentDateTime();
    // Leaderboard file
    string leaderboardFile = "../data/results/exam_" + examName + "_leaderboard.txt";
    ofstream leaderboardOut(leaderboardFile, ios::app);
    leaderboardOut << studentId << " " << totalMarks << " "
                   << attemptedCount << " " << wrongCount << " "
                   << totalTimeSpent << "\n";
    leaderboardOut.close();

    // Student performance file (append for multiple attempts)
    string perfFile = "../data/results/student_" + studentId + "_attempts.txt";
    ofstream perfOut(perfFile, ios::app);
    perfOut << examName << "|";
    perfOut << currDateTime << "|";
    perfOut << totalMarks << "|";
    perfOut << totalQuestions*4 << "|";
    perfOut << "../data/results/student_"+studentId+"_"+examName+"_performance.txt\n";
    perfOut.close();

    string scoreFile = "../data/results/student_"+studentId+"_"+examName+"_performance.txt";
    ofstream scoreOut(scoreFile, ios::app);
    scoreOut << "START\n";
    scoreOut << currDateTime << "|";
    scoreOut << examName + "|";
    scoreOut << totalMarks << "|" << totalQuestions*4 << "|";
    scoreOut << totalQuestions<< "|" <<attemptedCount << "|" << wrongCount <<"|";
    scoreOut << totalTimeSpent <<"\nEND\n";

    for (size_t i = 0; i < perQuestionMarks.size(); ++i) {
        scoreOut << "Q" << (i + 1) << "|";
        scoreOut << perQuestionMarks[i] << "|";
        if (perQuestionAnswer[i] != -1) {
            scoreOut  << static_cast<char>('A' + perQuestionAnswer[i]) << "|";
        }
        else{
            scoreOut << "NA|";
        }
        scoreOut << perQuestionTime[i] << "s\n";
    }
    scoreOut.close();

    // Student attempt history
    string attemptFile = "../data/results/exam_log.txt";
    ofstream attemptOut(attemptFile, ios::app);
    attemptOut <<studentId<<": " <<examName << ": " << getCurrentDateTime() << "\n";
    attemptOut.close();

    cout << "[✔] Evaluation complete for " << studentId << " on '" << examName << "'.\n";
}

string Server::getCurrentDateTime() {
    time_t now = time(nullptr);
    tm* localTime = localtime(&now);

    ostringstream oss;
    oss << put_time(localTime, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void Server::handleStudentExamRequest(int sock, ExamManager exam) {
    char buffer[1024] = {0};
    int bytesReceived = recv(sock, buffer, sizeof(buffer), 0);
    if (bytesReceived <= 0) {
        cerr << "Error: Failed to receive exam selection from client.\n";
        return;
    }

    int examNumber = atoi(buffer);

    if(examNumber==0) return;

    
    // Validate exam number
    if (examNumber < 1 || examNumber > exams.size()) {
        string errorMsg = "Error: Invalid exam selection";
        send(sock, errorMsg.c_str(), errorMsg.size(), 0);
        return;
    }

    // Extract exam name from the exams
    string selectedExamName;
    istringstream iss(exams[examNumber - 1]);
    string line;
    
    while (getline(iss, line)) {
        if (line.find("Exam Name:") != string::npos) {
            selectedExamName = line.substr(line.find(":") + 2);
            break;
        }
    }

    // Send questions for the selected exam
    if(examNumber!=-1){
        exam.sendExamQuestions(sock, selectedExamName);
        cout << "[+] question paper send successfully !\n";
    }
    else{
        cout << "[+] client already have question paper\n";
    }
    
    memset(buffer, 0, sizeof(buffer));
    recv(sock, buffer, sizeof(buffer), 0);
    string response(buffer);

    if (response == "y" || response == "Y") {
        cout << "[+] Student confirmed to start the exam.\n";
        sleep(3);
        receiveStudentAnswers(sock, selectedExamName);
    } else {
        cout << "[!] Student decided not to start the exam.\n";
    }
}

bool Server::handle_authentication(int sock, const string& command, const string& user_type, const string& username, const string& password) {
    if (command == "LOGIN") {
        if (AuthManager::authenticate_user(username, password, user_type)) {
            send(sock, "AUTHENTICATION_SUCCESS", strlen("AUTHENTICATION_SUCCESS"), 0);
            cout << username << " logged in successfully as " << user_type << endl;
            return true;
        } else {
            send(sock, "AUTHENTICATION_FAILED", strlen("AUTHENTICATION_FAILED"), 0);
            cerr << "Authentication failed for " << username << endl;
            return false;
        }
    } else if (command == "REGISTER") {
        if (AuthManager::register_user(username, password, user_type)) {
            send(sock, "REGISTER_SUCCESS", strlen("REGISTER_SUCCESS"), 0);
            cout << username << " registered successfully as " << user_type << endl;
            return true;
        } else {
            send(sock, "REGISTER_FAILED", strlen("REGISTER_FAILED"), 0);
            cerr << "Registration failed for " << username << endl;
            return false;
        }
        
    }
    return false;
}

void Server::handleViewPerformance(int clientSock, const string& studentId) {
    string filename = "../data/results/student_" + studentId + "_attempts.txt";
    ifstream file(filename);
    if (!file.is_open()) {
        string err = "[!] No exam data found for student.";
        err += "\n[0] Back to Main Menu\n--------------------------------------\n";
        err += "Select an exam to view performance: ";
        send(clientSock, err.c_str(), err.size() + 1, 0);
        return;
    }

    map<string, vector<tuple<string, string, string>>> examMap;
    string line;
    while (getline(file, line)) {
        stringstream ss(line);
        string examName, timestamp, marksObtained, totalMarks, perfPath;
        getline(ss, examName, '|');
        getline(ss, timestamp, '|');
        getline(ss, marksObtained, '|');
        getline(ss, totalMarks, '|');
        getline(ss, perfPath);

        examMap[examName].emplace_back(timestamp, marksObtained, totalMarks + "|" + perfPath);
    }
    file.close();

    while (true) {
        string dashboard = "\n========== Attempted Exams ==========\n\n";
        vector<string> examNames;
        int index = 1;
        for (auto& pair : examMap) {
            dashboard += "[" + to_string(index++) + "] " + pair.first + " (" + to_string(pair.second.size()) + " attempts)\n";
            examNames.push_back(pair.first);
        }
        dashboard += "\n[0] Back to Main Menu\n--------------------------------------\n";
        dashboard += "select from above: ";

        send(clientSock, dashboard.c_str(), dashboard.size() + 1, 0);

        char examChoiceBuf[10] = {0};
        int bytesReceived = recv(clientSock, examChoiceBuf, sizeof(examChoiceBuf), 0);
        if (bytesReceived <= 0) {
            cerr << "Error: Failed to receive exam selection from client.\n";
            return;
        }
        int examChoice = atoi(examChoiceBuf);
        cout << "exam choice: "<< examChoice<<endl;

        if (examChoice == 0) break;
        if (examChoice < 1 || examChoice > examNames.size()) break;

        string selectedExam = examNames[examChoice - 1];
        auto& attempts = examMap[selectedExam];

        string attemptList = "\n=============="+selectedExam+" attempts==============\n\n";
        // attemptList += "You attempted \"" + selectedExam + "\" " + to_string(attempts.size()) + " times:\n";
        for (int i = 0; i < attempts.size(); ++i) {
            string timestamp = get<0>(attempts[i]);
            string marksObtained = get<1>(attempts[i]);
            string totalMarks = get<2>(attempts[i]).substr(0, get<2>(attempts[i]).find('|'));

            attemptList += "[" + to_string(i + 1) + "] Attempt on: " + timestamp +
                            " Marks Obtained: " + marksObtained + " / " + totalMarks + "\n";
        }
        attemptList += "\n[0] Back to Exam List\n";
        attemptList += "--------------------------------------------------------\n";
        attemptList += "Select an attempt to view details: ";

        send(clientSock, attemptList.c_str(), attemptList.size() + 1, 0);
        
        char attemptChoiceBuf[10] = {0};
        recv(clientSock, attemptChoiceBuf, sizeof(attemptChoiceBuf), 0);
        int attemptChoice = atoi(attemptChoiceBuf);

        if (attemptChoice == 0) continue;
        if (attemptChoice < 1 || attemptChoice > attempts.size()) continue;

        string selectedTimestamp = get<0>(attempts[attemptChoice - 1]);
        string perfFilePath = get<2>(attempts[attemptChoice - 1]);
        perfFilePath = perfFilePath.substr(perfFilePath.find('|') + 1);

        ifstream perfFile(perfFilePath);
        if (!perfFile.is_open()) {
            string error = "Error: Performance file not found.\n";
            attemptList += "\n[0] Back to Exam List\n";
            error += "--------------------------------------------------------\n";
            error += "select from above: ";
            send(clientSock, error.c_str(), error.size() + 1, 0);
            continue;
        }

        string formatted, line;
        bool found = false;
        while (getline(perfFile, line)) {
            if (line == "START") {
                string summaryLine;
                if (!getline(perfFile, summaryLine)) break;

                stringstream ss(summaryLine);
                string timestamp, examName, marksObtained, totalMarks, totalQuestions, attempted, wrong, totalTime;
                getline(ss, timestamp, '|');
                cout << "time stamp: "<<timestamp<<endl;
                if (timestamp != selectedTimestamp) {
                    // skip this block
                    while (getline(perfFile, line) && line != "START");
                        // skip lines until next START or EOF
                    if (line == "START") {
                        perfFile.seekg(-line.length()-1, ios::cur); // rewind to let outer loop re-process START
                    }
                    continue;
                }

                // Matched
                found = true;
                getline(ss, examName, '|');
                getline(ss, marksObtained, '|');
                getline(ss, totalMarks, '|');
                getline(ss, totalQuestions, '|');
                getline(ss, attempted, '|');
                getline(ss, wrong, '|');
                getline(ss, totalTime, '|');

                formatted = "\n========== Attempt Details ==========\n\n";
                formatted += "Exam: " + examName + "\n";
                formatted += "Attempt Date: " + timestamp + "\n\n";
                formatted += "Total Marks Obtained   : " + marksObtained + " / " + totalMarks + "\n";
                formatted += "Total Questions        : " + totalQuestions + "\n";
                formatted += "Attempted Questions    : " + attempted + "\n";
                formatted += "Wrong Answers          : " + wrong + "\n";
                formatted += "Total Time Spent       : " + totalTime + "s\n\n";

                formatted += "Qno.    status     marks     answer     time\n";
                formatted += "--------------------------------------------\n";
                // Read until END
                while (getline(perfFile, line) && line != "END");

                // After END comes per-question
                int qNum = 1;
                while (getline(perfFile, line)) {
                    if (line == "START") break;
                
                    stringstream qss(line);
                    string questionStr, markStr, optStr, timeStr;
                    
                    getline(qss, questionStr, '|');
                    getline(qss, markStr, '|');
                    getline(qss, optStr, '|');
                    getline(qss, timeStr, 's');      
                
                    formatted += questionStr + ": ";
                    if (optStr == "NA") {
                        formatted += "not_attempted     -         -        "+ timeStr + "s\n";
                    } else {
                        int mark = stoi(markStr);  // Convert marks string to integer
                        string status = (mark == -1) ? "     wrong    " : "   attempted  ";
                        formatted += status;
                        formatted += (mark > 0 ? "   +" : "   ") + markStr + "         ";
                        formatted += optStr + "        " + timeStr + "s\n";
                    }
                }
                // here add code for adding exam paper along with stat
                string examFilePath = "../data/exams/questions_" + examName + ".txt";
                ifstream examFile(examFilePath);
                if (examFile.is_open()) {
                    formatted += "\n========== Exam Questions ==========\n";
                    string qLine;
                    int qNum = 1;
                    while (getline(examFile, qLine)) {
                        if (qLine.empty()) {
                            formatted += "\n";  // preserve spacing between questions
                            continue;
                        }

                        if (qLine[0] == ' ') {
                            // Likely a question line (starts with space), so prepend Q number
                            formatted += "Q" + to_string(qNum++) + "." + qLine + "\n";
                        } else {
                            // Likely an option line
                            formatted += qLine + "\n";
                        }
                    }
                    formatted += "===========================================\n";
                    examFile.close();
                } else {
                    formatted += "\n[Warning] Unable to load original exam paper: " + examFilePath + "\n";
                }
                formatted += "[1] View Leaderboard for this Exam\n";
                formatted += "[0] Back to Exam List\n";
                formatted += "-------------------------------------------\n";
                formatted +="Select from above option: ";
                break;
            }
        }

        perfFile.close();

        if (!found) {
            string err = "Error: Attempt not found.\n";
            err += "[0] Back to Exam List\n";
            err += "-------------------------------------------\n";
            err += "Select from above option: ";
            send(clientSock, err.c_str(), err.size() + 1, 0);
        } else {
            send(clientSock, formatted.c_str(), formatted.size() + 1, 0);
        }

        char leaderboardbuf[10] = {0};
        bytesReceived = recv(clientSock, leaderboardbuf, sizeof(leaderboardbuf), 0);
        if (bytesReceived <= 0) {
            cerr << "Error: Failed to receive exam selection from client.\n";
            return;
        }
        int leaderboard = atoi(leaderboardbuf);
        cout << "exam choice: "<< leaderboardbuf<<endl;

        if(leaderboard==0 || leaderboard!=1) continue;
        string leaderboardList = "../data/results/exam_" + selectedExam + "_leaderboard.txt";
        ifstream leaderboardFile(leaderboardList);  
        if (!leaderboardFile.is_open()) {
            formatted += "\n[✖] Could not open leaderboard file.\n";
        } else {
            vector<vector<string>> students;
    
            string line;
            while (getline(leaderboardFile, line)) {
                stringstream ss(line);
                string uname, marksStr, attStr, wrongStr, timeStr;
                ss >> uname >> marksStr >> attStr >> wrongStr >> timeStr;
    
                vector<string> entry = { uname, marksStr, attStr, wrongStr, timeStr };
                students.push_back(entry);
            }
            leaderboardFile.close();
    
            // Sort students based on: marks desc, wrong asc, time asc
            sort(students.begin(), students.end(), [](const vector<string>& a, const vector<string>& b) {
                int ma = stoi(a[1]), mb = stoi(b[1]);
                int wa = stoi(a[3]), wb = stoi(b[3]);
                int ta = stoi(a[4]), tb = stoi(b[4]);
    
                if (ma != mb) return ma > mb;
                if (wa != wb) return wa < wb;
                return ta < tb;
            });
    
            // Find rank of this user
            int yourRank = -1;
            for (int i = 0; i < students.size(); ++i) {
                if (students[i][0] == studentId) {
                    yourRank = i + 1;
                    break;
                }
            }
    
            formatted += "\n========= Leaderboard: "+ selectedExam+" =========\n\n";
            formatted += "Rank  Student ID     Marks   Time(s)\n";
            formatted += "---------------------------------------------------\n";
            for (int i = 0; i < min(3, (int)students.size()); ++i) {
                formatted += to_string(i + 1) + "     ";
                formatted += students[i][0];
                int spaceLen = 17 - students[i][0].length();
                formatted += string(spaceLen, ' ');
                formatted += students[i][1] + "      ";
                formatted += students[i][4] + "\n";
            }
            formatted += "---------------------------------------------------\n";
            if (yourRank != -1)
                formatted += "Your rank: " + to_string(yourRank) + "\n";
            else
                formatted += "[!] You didn't participate in this exam.\n";
        }
        send(clientSock, formatted.c_str(), formatted.size() + 1, 0);
        break;
    }
}

void* Server::handle_client(void* client_socket) {
    
    int sock = *(int*)client_socket;
    char buffer[1024] = {0};
    string command, user_type, username, password;
    int attempts=0;
    while(attempts < 5){
        memset(buffer, 0, sizeof(buffer));
        recv(sock, buffer, sizeof(buffer), 0);
        string request(buffer);
        if(request=="exit") break;
        istringstream iss(request);
        iss >> command >> user_type >> username >> password;
        if(handle_authentication(sock, command, user_type, username, password)){
            Server::socketToUsername[sock] = username;
            break;
        }
        cout<< "[!] Only " << 4 - attempts++ << "left\n\n";
    }

    ExamManager exam_manager;
    if (user_type == "student") {
        while (true){
            memset(buffer, 0, sizeof(buffer));
            int bytes_received = recv(sock, buffer, sizeof(buffer), 0);
            if (bytes_received <= 0) break;
            buffer[bytes_received] = '\0';
            string request(buffer);
            
            if (request == "1") {
                string all_exams;
                int qno = 1;
                for (auto& exam : exams) {
                    string formattedExam;
                    istringstream iss(exam);
                    string line;
                    
                    while (getline(iss, line)) 
                        formattedExam += line + " | ";
        
                    if (!formattedExam.empty() && formattedExam.back() == ' ')
                        formattedExam.pop_back();

                    all_exams += to_string(qno) + ". " + formattedExam + "\n";
                    qno++;
                }
            
                if (all_exams.empty())
                    all_exams = "No exams available.";
                
                send(sock, all_exams.c_str(), all_exams.size(), 0);
                if(all_exams!="No exams available.")
                    handleStudentExamRequest(sock, exam_manager);
            }
            
            else if (request == "2") {
                handleViewPerformance(sock, username);
            }
            else if(request == "3") break;
        }
    }
    else if (user_type == "instructor") {
        while (true){
            memset(buffer, 0, sizeof(buffer));
            int bytes_received = recv(sock, buffer, sizeof(buffer), 0);
            buffer[bytes_received] = '\0';
            string request(buffer);
            string response = "";

            if (request == "1") {
                memset(buffer, 0, sizeof(buffer));
                recv(sock, buffer, sizeof(buffer), 0);
                string examData(buffer);
            
                size_t pos1 = examData.find("|");
                size_t pos2 = examData.find("|", pos1 + 1);
            
                if (pos1 == string::npos || pos2 == string::npos) {
                    response = "Error: Invalid format. Use 'Exam Name | Duration | FileName'";
                } else {
                    string examName = examData.substr(0, pos1);  
                    int examDuration = atoi(examData.substr(pos1 + 1, pos2 - pos1 - 1).c_str());
                    string examFileName = "../data/exams/" + examData.substr(pos2 + 1);
                    
                    if (exam_manager.parse_exam(examFileName, examName, username, examDuration)) {
                        exams = exam_manager.load_exam_metadata("../data/exams/exam_list.txt");
                        response = "Exam successfully uploaded!"; 
                    } else {
                        response = "Error: Invalid exam format!";
                    }
                }
                send(sock,response.c_str(),response.size(),0);
            }
            else if(request == "2"){
                send(sock, "upload exam sheet...", strlen("upload exam sheet..."), 0);
            }
            else if (request == "3"){
                send(sock, "student performance...", strlen("student performance..."), 0);
            }
            else if (request == "4") {
                string all_exams;
                int qno = 1;  // Start numbering from 1
            
                for (const auto& exam : exams) {
                    istringstream iss(exam);
                    ostringstream filteredExam;
                    string line;
                    string instructor_name;
                    bool include_exam = false;
            
                    while (getline(iss, line)) {
                        if (line.find("Instructor:") != string::npos) {
                            instructor_name = line.substr(line.find(":") + 2);  // Extract instructor name
                            if (instructor_name == username) {
                                include_exam = true;  // Match found, include this exam
                            }
                        }
                        if (line.find("Instructor:") != std::string::npos)
                            continue;
                        filteredExam << line << " | ";
                    }
            
                    if (include_exam) {
                        all_exams += to_string(qno++) + ". " + filteredExam.str() + "\n";
                    }
                }
            
                if (all_exams.empty()) {
                    all_exams = "No exams available for this instructor.\n";
                }
            
                all_exams += "\0";
                send(sock, all_exams.c_str(), all_exams.size(), 0);
            }
            
            else if (response=="5") break;
        }
    }
    close(sock);
    cout << "[-] client[ "<<username<<" ] disconnected!"<<endl;
    return nullptr;
}
