#include "client.h"
#include "ui.h"

map<int, int> Client::timeSpentPerQuestion;
map<int, int> Client::shuffledQuestionMap;
vector<vector<int>> Client::shuffledOptionMap;
vector<string> Client::shuffledQuestions;
vector<vector<string>> Client::shuffledOptions;
vector<ExamInfo> availableExams;

int lastIndex = 0;
auto lastTime = steady_clock::now();
bool Client::timeUp = false;
pthread_mutex_t Client::timerMutex = PTHREAD_MUTEX_INITIALIZER;

Client::Client(const string& server_ip, int server_port) {
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        cerr << "Error: Could not create socket\n";
        exit(EXIT_FAILURE);
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        cerr << "Error: Connection to server failed\n";
        exit(EXIT_FAILURE);
    }
}

void Client::xorEncryptDecrypt(const string& filePath, char key) {
    fstream file(filePath, ios::in | ios::out | ios::binary);
    if (!file) {
        cerr << "Error: Unable to open file for encryption: " << filePath << "\n";
        return;
    }

    vector<char> fileData((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());

    for (char &c : fileData) {
        c ^= key;
    }

    file.seekp(0);
    file.write(fileData.data(), fileData.size());
    file.close();
}

void Client::decryptAndPrepareExam(const string& filePath, char key) {
    // Read the encrypted file in binary mode
    ifstream infile(filePath, ios::binary);
    if (!infile) {
        cerr << "[-] Error: Could not open file " << filePath << endl;
        return;
    }

    string encryptedContent((istreambuf_iterator<char>(infile)), istreambuf_iterator<char>());
    infile.close();

    // Decrypt the content
    for (char &ch : encryptedContent) {
        ch ^= key;
    }

    vector<string> questions;
    vector<vector<string>> options;

    istringstream iss(encryptedContent);
    string line, currentQuestion, optionA, optionB, optionC, optionD;
    bool readingOptions = false;
    vector<string> tempOptions;

    while (getline(iss, line)) {
        if (line.empty()) {  
            if (!currentQuestion.empty() && tempOptions.size() == 4) {
                questions.push_back(currentQuestion);
                options.push_back(tempOptions);
            }
            currentQuestion = "";  
            tempOptions.clear();    
            readingOptions = false; 
        } 
        else if (line.rfind("A)", 0) == 0 || line.rfind("B)", 0) == 0 ||
                line.rfind("C)", 0) == 0 || line.rfind("D)", 0) == 0) {  
            tempOptions.push_back(line.substr(2));  
            readingOptions = true;
        } 
        else if (!readingOptions) {  
            currentQuestion = line;  
        }
    }

    if (!currentQuestion.empty() && tempOptions.size() == 4) {
        questions.push_back(currentQuestion);
        options.push_back(tempOptions);
    }

    if (questions.empty()) {
        cerr << "[-] Error: No valid questions found in decrypted content.\n";
        return;
    }

    // Shuffle Questions and Options
    int n = questions.size();
    vector<int> qIndices(n);
    iota(qIndices.begin(), qIndices.end(), 0);

    random_device rd;
    mt19937 g(rd());
    shuffle(qIndices.begin(), qIndices.end(), g);

    shuffledQuestionMap.clear();
    shuffledQuestions.clear();
    shuffledOptions.clear();
    shuffledOptionMap.clear();

    for (int i = 0; i < n; ++i) {
        int origIdx = qIndices[i];
        shuffledQuestionMap[i] = origIdx;
        shuffledQuestions.push_back(questions[origIdx]);

        vector<int> optIdx = {0, 1, 2, 3};
        shuffle(optIdx.begin(), optIdx.end(), g);

        vector<string> shuffledOpts(4);
        vector<int> optMapping(4);
        for (int j = 0; j < 4; ++j) {
            shuffledOpts[j] = options[origIdx][optIdx[j]];
            optMapping[j] = optIdx[j];
        }

        shuffledOptions.push_back(shuffledOpts);
        shuffledOptionMap.push_back(optMapping);
    }
}

void* examTimer(void* arg) {
    int duration = *(int*)arg;
    int total = duration;

    for (int i = 0; i < total; ++i) {
        sleep(1);

        // Progress bar
        int percent = (100 * i) / total;
        int barWidth = 50;
        int pos = (barWidth * i) / total;

        cout << "\033[s"; // Save cursor position
        cout << "\033[1;1H"; // Move cursor to top-left
        cout << "[";
        for (int j = 0; j < barWidth; ++j) {
            if (j < pos) cout << "=";
            else if (j == pos) cout << ">";
            else cout << " ";
        }
        cout << "] " << percent << "% " << (total - i) << "s left";
        cout << "\033[u"; // Restore cursor position
        cout.flush();

        // Check if user already submitted
        pthread_mutex_lock(&Client::timerMutex);
        if (Client::timeUp) {
            pthread_mutex_unlock(&Client::timerMutex);
            return nullptr; // Exit early if user submitted
        }
        pthread_mutex_unlock(&Client::timerMutex);
    }

    pthread_mutex_lock(&Client::timerMutex);
    Client::timeUp = true;
    pthread_mutex_unlock(&Client::timerMutex);

    cout << "\n[!] Time is up. Submitting the exam...\n";
    cout << "[!] Please enter 0...\n";
    return nullptr;
}

void Client::displayPreparedQuestion(int index) {
    cout << "\n\n--------------------------------QUESTION "<<index+1<<"-------------------------------\n";
    if (index < 0 || index >= shuffledQuestions.size()) {
        cout << "Invalid question index.\n";
        return;
    }

    cout << "Q" << (index + 1) << ": " << shuffledQuestions[index] << "\n";
    for (int i = 0; i < 4; ++i) {
        char label = 'A' + i;
        cout << label << ") " << shuffledOptions[index][i] << "\n";
    }
    cout << "-----------------------------QUESTION END--------------------------------\n";
}

void Client::manageExam(int durationMinutes, Client* client) {
    int durationSeconds = durationMinutes * 60;
    vector<int> studentAnswers(shuffledQuestions.size(), -1);
    vector<int> timeSpent(shuffledQuestions.size(), 0); // in seconds

    Client::timeSpentPerQuestion.clear();

    int currentIndex = 0;
    auto questionStartTime = chrono::steady_clock::now();

    // Start timer thread
    pthread_t timerThread;
    pthread_create(&timerThread, nullptr, examTimer, &durationSeconds);

    system("clear");
    cout << "\n📘 Exam started. Good luck!\n";

    while (true) {
        // Check for timeout
        pthread_mutex_lock(&timerMutex);
        if (timeUp) {
            pthread_mutex_unlock(&timerMutex);
            break;
        }
        pthread_mutex_unlock(&timerMutex);

        displayPreparedQuestion(currentIndex);
        UI_elements::displayExamOptions();

        int opt;
        while (true) {
            cout << "\n➡️  Enter your choice (1-6): ";
            cin >> opt;
            if (cin.fail()) {
                cin.clear();
                cin.ignore(numeric_limits<streamsize>::max(), '\n');
                cout << "[✖] Invalid input. Please enter a number between 1 and 6.\n";
            } else {
                cin.ignore(numeric_limits<streamsize>::max(), '\n');
                break;
            }
        }

        // Track time spent on current question
        auto now = chrono::steady_clock::now();
        timeSpent[currentIndex] += chrono::duration_cast<chrono::seconds>(now - questionStartTime).count();

        switch (opt) {
            case 1: // Next question
                if (currentIndex < shuffledQuestions.size() - 1) currentIndex++;
                else cout << "\n[!] You are on the last question.\n";
                break;

            case 2: // Previous question
                if (currentIndex > 0) currentIndex--;
                else cout << "\n[!] You are on the first question.\n";
                break;

            case 3: { // Attempt/Answer
                cout << "\n✏️  Enter your answer (A/B/C/D): ";
                char answer;
                cin >> answer;
                answer = toupper(answer);

                if (answer >= 'A' && answer <= 'D') {
                    int shuffledIndex = answer - 'A';
                    int originalOptionIndex = shuffledOptionMap[currentIndex][shuffledIndex];
                    studentAnswers[currentIndex] = originalOptionIndex;

                    cout << "📌 Selected option index: " << originalOptionIndex << "\n";
                    cout << "[✔] Answer recorded successfully.\n";

                    if (currentIndex < shuffledQuestions.size() - 1) currentIndex++;
                    else cout << "\n[!] You are on the last question.\n";
                } else {
                    cout << "[✖] Invalid choice. Please enter A/B/C/D.\n";
                }
                break;
            }

            case 4: // Clear answer
                studentAnswers[currentIndex] = -1;
                cout << "[✔] Answer cleared.\n";
                break;

            case 5: { // Jump to question
                int qno;
                cout << "\n🔢 Enter question number (1 to " << shuffledQuestions.size() << "): ";
                cin >> qno;
                if (qno >= 1 && qno <= shuffledQuestions.size()) {
                    currentIndex = qno - 1;
                } else {
                    cout << "[✖] Invalid question number.\n";
                }
                break;
            }

            case 6: // Submit exam
                cout << "\n📝 Submitting your exam...\n";
                pthread_mutex_lock(&timerMutex);
                timeUp = true;
                pthread_mutex_unlock(&timerMutex);
                break;

            default:
                cout << "[✖] Invalid choice. Try again.\n";
                break;
        }

        // Restart timer for next question
        questionStartTime = chrono::steady_clock::now();

        pthread_mutex_lock(&timerMutex);
        if (timeUp) {
            pthread_mutex_unlock(&timerMutex);
            break;
        }
        pthread_mutex_unlock(&timerMutex);
    }

    pthread_join(timerThread, nullptr);
    timeUp = false;

    cout << "\n✅ Exam session ended.\n";

    // Prepare exam submission
    ostringstream dataToSend;
    dataToSend << "ANSWERS\n";
    for (int i = 0; i < studentAnswers.size(); ++i) {
        int originalIndex = shuffledQuestionMap[i];
        dataToSend << originalIndex << "," << studentAnswers[i] << "," << timeSpent[i] << "\n";
    }

    // Send data to server
    string finalData = dataToSend.str();
    send(client->sock, finalData.c_str(), finalData.size(), 0);
}

void Client::dashboard(Client * client) {
    int sockfd = client->sock;
    char buffer[5120];

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        recv(sockfd, buffer, sizeof(buffer), 0);
        system("clear");
        usleep(200000);
        cout << buffer;

        int input;
        cin >> input;
        string examSelection = to_string(input);
        send(sockfd, examSelection.c_str(), examSelection.size(), 0);

        if (input == 0) break;
    
        // Wait for attempt list or exam details
        memset(buffer, 0, sizeof(buffer));
        recv(sockfd, buffer, sizeof(buffer), 0);
        cout << buffer;

        cin >> input;
        examSelection = to_string(input);
        send(sockfd, examSelection.c_str(), examSelection.size(), 0);

        if (input == 0) continue;

        memset(buffer, 0, sizeof(buffer));
        recv(sockfd, buffer, sizeof(buffer), 0);
        system("clear");
        usleep(200000);
        // here write logic for displaying exam paper which is stored in e
        cout << buffer;

        cin >> input;
        examSelection = to_string(input);
        send(sockfd, examSelection.c_str(), examSelection.size(), 0);

        if (input == 0) continue;

        memset(buffer, 0, sizeof(buffer));
        recv(sockfd, buffer, sizeof(buffer), 0);
        cout << buffer;
        cout <<"\npress any key...\n";
        cin.get();
        cin.ignore();
        break;
    }
}

void Client::parseAvailableExams(const string& examData) {
    availableExams.clear();  // Clear old data
    istringstream iss(examData);
    string line;

    while (getline(iss, line)) {
        if (line.empty()) continue;

        size_t pos1 = line.find("Exam Name:");
        size_t pos2 = line.find("| Duration (minutes):");
        size_t pos3 = line.find("| Total Questions:");
        size_t pos4 = line.find("| Instructor:");

        if (pos1 != string::npos && pos2 != string::npos && pos3 != string::npos && pos4 != string::npos) {
            string name = line.substr(pos1 + 10, pos2 - (pos1 + 10));
            int duration = stoi(line.substr(pos2 + 22, pos3 - (pos2 + 22)));
            int totalQ = stoi(line.substr(pos3 + 18, pos4 - (pos3 + 18)));
            string instructor = line.substr(pos4 + 13);
            availableExams.emplace_back(name, duration, totalQ, instructor);
        }
    }
}

void Client::handleExamSelection(Client* client, int& choice) {
    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    int bytes_read = recv(client->sock, buffer, sizeof(buffer), 0);

    if (bytes_read <= 0) {
        cerr << "[✖] Error: Failed to read data from server\n";
        close(client->sock);
        return;
    }

    string examData(buffer);

    if (examData == "No exams available.") {
        cout << "\n[!] No exams available at the moment.\n\n";
        return;
    }

    parseAvailableExams(examData);

    // Display exams in neat format
    cout << "\n================================== Available Exams =================================\n";
    for (size_t i = 0; i < availableExams.size(); ++i) {
        const ExamInfo& ex = availableExams[i];
        printf("%2zu. %-25s | Duration: %3d min | Questions: %2d | Instructor: %s\n",
               i + 1, ex.name.c_str(), ex.duration, ex.totalQuestions, ex.instructor.c_str());
    }
    cout << "------------------------------------------------------------------------------------\n";
    cout << "Select exam number to start the exam (press 0 to go back): ";
    cin >> choice;

    while (choice < 0 || choice > availableExams.size()) {
        cout << "[✖] Please enter a valid exam number: ";
        cin >> choice;
    }

    if (choice == 0) {
        string examSelection = to_string(choice);
        send(client->sock, examSelection.c_str(), examSelection.size(), 0);
        return;
    }

    const ExamInfo& selectedExam = availableExams[choice - 1];
    string filePath = "./exams/exam_" + to_string(choice) + ".txt";

    string fileName = "./exams/exam_" + to_string(choice) + ".txt";
    ifstream file(fileName);
    if(!file.good()){
        receiveAndStoreExamQuestions(client->sock, choice);
    }
    else{
        string examSelection = to_string(-1);
        send(client->sock, examSelection.c_str(), examSelection.size(), 0);
    }
    
    decryptAndPrepareExam(filePath, 'X');

    cout << "\n==================== Exam Details ====================\n";
    cout << "- Exam Name       : " << selectedExam.name << "\n";
    cout << "- Total Questions : " << selectedExam.totalQuestions << "\n";
    cout << "- Duration        : " << selectedExam.duration << " minutes\n";
    cout << "- Marking Scheme  : +4 for correct, -1 for incorrect\n";
    cout << "---------------------------------------------------------\n";
    cout << "Start the exam now? (y/n): ";

    char confirm;
    cin >> confirm;
    send(client->sock, &confirm, 1, 0);

    if (tolower(confirm) == 'y') {
        manageExam(selectedExam.duration, client);
    } else {
        cout << "Returning to student menu.\n";
    }
}

void* Client::studentHandler(void* arg) {
    Client* client = static_cast<Client*>(arg);
    char buffer[1024] = {0};
    int choice;

    while (true) {
        UI_elements::displayStudentMenu();
        cin >> choice;

        sprintf(buffer, "%d", choice);
        send(client->sock, buffer, strlen(buffer), 0);

        if (choice == 3) {
            cout << "Logging out...\n";
            close(client->sock);
            return nullptr;
        } else if(choice==1){
            handleExamSelection(client, choice);
        }
         else if(choice == 2){
            dashboard(client);
        }
        else {
            cout << "Invalid choice! Please select a valid option.\n";
        }
    }
    return nullptr;
}

void Client::receiveAndStoreExamQuestions(int sock, int examNumber) {
    char buffer[4096] = {0};

    string examSelection = to_string(examNumber);
    send(sock, examSelection.c_str(), examSelection.size(), 0);

    int bytesReceived = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived <= 0) {
        cerr << "Error: Failed to receive exam questions from server.\n";
        return;
    }

    string server_reply(buffer);

    if (server_reply == "Error: Invalid exam selection") {
        cout << "[+] " << server_reply << endl;
        return;
    }
    
    buffer[bytesReceived] = '\0';
    string fileName = "./exams/exam_" + to_string(examNumber) + ".txt";

    ofstream outFile(fileName);
    if (!outFile) {
        cerr << "Error: Unable to create file " << fileName << "\n";
        return;
    }
    outFile << buffer;
    outFile.close();
    memset(buffer,0,sizeof(buffer));
    // Encrypt the file
    xorEncryptDecrypt(fileName, 'X');  // 'X' is the XOR key

    cout << "[+] Question paper received successfully\n";
}

void* Client::instructorHandler(void* arg) {
    Client* client = static_cast<Client*>(arg);
    char buffer[1024] = {0};
    int choice;

    while (true) {
        UI_elements::displayInstructorMenu();
        cin >> choice;

        sprintf(buffer, "%d", choice);
        send(client->sock, buffer, strlen(buffer), 0);

        if (choice == 5) {
            cout << "Logging out...\n";
            close(client->sock);
            return nullptr;
        } else if (choice == 1) {
            while ((getchar()) != '\n');
            string examName, duration, fileName;
            cout << "\n\n=============Enter exam details=============\n\n";
            cout << "Enter Exam Name: ";
            getline(cin, examName);
            cout << "Enter Exam Duration (minutes): ";
            cin >> duration;
            cout << "Enter Exam File Name: ";
            cin >> fileName;
            cout << "-------------------------------------------------\n";

            examName += "|" + duration + "|" + fileName;
            send(client->sock, examName.c_str(), examName.size(), 0);

            memset(buffer, 0, sizeof(buffer));
            recv(client->sock, buffer, sizeof(buffer), 0);
            cout <<buffer << endl;
        } else if (choice >= 2 && choice <= 4) {
            memset(buffer, 0, sizeof(buffer));
            recv(client->sock, buffer, sizeof(buffer), 0);
            cout << "\n\n=====================================Your uploaded exams=====================================\n";
            cout << buffer;
            cout << "-----------------------------------------------------------------------------------------------\n";
        } else {
            cout << "Invalid choice! Please select a valid option.\n";
        }
    }
    return nullptr;
}

void Client::authenticate() {
    int choice;
    system("clear");
    usleep(200000);
    UI_elements::displayHeader("Welcome to the Exam System");

    while (true) {
        UI_elements::displayMenu();
        cin >> choice;

        if (choice == 3) {
            cout << "Exiting...\n";
            string request = "exit";
            send(sock, request.c_str(), request.length(), 0);
            close(sock);
            exit(0);
        }

        cout << "Enter role (S for Student, I for Instructor): ";
        cin >> role;
        transform(role.begin(), role.end(), role.begin(), ::tolower);

        if (role != "s" && role != "i") {
            cout <<"[✖]Invalid role! Please enter S or I."<<endl;
            continue;
        }

        cout << "Enter username: "; cin >> username;
        cout << "Enter password: "; cin >> password;

        string user_type = (role == "s") ? "student" : "instructor";
        string request = (choice == 1) ? "LOGIN " : "REGISTER ";
        request += user_type + " " + username + " " + password;
        send(sock, request.c_str(), request.length(), 0);

        char response[128] = {0};
        int bytes_read = recv(sock, response, sizeof(response), 0);
        if (bytes_read <= 0) {
            cout << "[✖] Error: Failed to read data from server."<<endl;
            close(sock);
            return;
        }
        string server_reply(response);
        if (server_reply == "AUTHENTICATION_SUCCESS" || server_reply == "REGISTER_SUCCESS"){
            cout <<"[✔] " <<server_reply <<endl;
            usleep(1200000);
            break;
        } 
        else cout << ((choice == 1) ? "[✖] Login failed! Try again." : "[✖] Registration failed! Username may already exist.") << endl;
    }
}

void Client::start() {
    authenticate();
    system("clear");
    usleep(200000);
    pthread_t thread;
    if (role == "s") {
        pthread_create(&thread, nullptr, studentHandler, this);
    } else {
        pthread_create(&thread, nullptr, instructorHandler, this);
    }
    pthread_join(thread, nullptr);
}
