#include <iostream>
#include <string>
#include <cstdint>
#include <fstream>
#include <unistd.h>
#include <sys/socket.h>
#include <cstdint>
#include <csignal>
#include <cstdio>
#include <netinet/in.h>
#include <cerrno>
#include <cstdlib>
#include <iterator>
#include <cstdio>
#include "helper.h"
using namespace std;

// ---- Constants ----

const int32_t MAX_FUNCS = 128;
const int32_t MAX_TOKENS = MAX_VARS_PER_FRAME + 2; // kW + func_name + upto 16 params/args
const int32_t MAX_PATCHES = MAX_FUNCS * 4;
const uint64_t MAX_SOURCE_BYTES = 15ULL * 1024 * 1024; // sanity cap on the declared file length
             
const int32_t SOCKET_TIMEOUT_SEC = 5;                      // TODO: apply as SO_RCVTIMEO so a deadclient can't hang the server forever

// ---- Custom data structures

// Stack: back the live Call Stack during execution
template <typename T>
class Stack
{
    struct Node
    {
        T data;
        Node *next;
    };
    Node *top;
    int32_t count;

public:
    // Implement these functions:
    Stack() 
    { // initialize the stack
        top = nullptr;
        count = 0;
    }
    void push(const T &val)
    {

        if (count >= MAX_STACK_DEPTH)
        {
            cout << "Stack is full..." << endl;
            return;
        }
        Node *newNode = new Node;
        newNode->data = val;

        newNode->next = top;
        top = newNode;
        count++;
        // pushes the value on the stack if max limit is not reached yet.
    }
    T pop()
    {

        if (isEmpty())
        {
          cout << "Stack is empty..." << endl;
           return T();
        }

        Node *temp = top;

 
        T val = temp->data;
        top = top->next;
        delete temp;
        count--;
        return val;


        // pop the top value on the stack
    }
    T &peek()
    {

        if (isEmpty())
        {
            cout << "Stack is empty.." << endl;
            static T temp;
            return temp;
        }
        return top->data;
        // returns the top value on the stack
    }
    bool isEmpty()
    {
        return count == 0;
    }
    int32_t depth()
    {
        return count;
    }
    int32_t snapshot_into(T out[], int32_t maxLen)
    {


        Node *curr = top;
        int32_t idx = 0;


        while (curr != nullptr && idx < maxLen)
        {
            out[idx] = curr->data; 
            curr = curr->next;     
            idx++;
        }

        return idx;
        // copies every frame, top to bottom in the array given as a parameter
        // this is what buildSnapshot() call, returns count written
    }
};



struct TimelineNode
{
    Snapshot *data;
    TimelineNode *next;
    TimelineNode *prev;
};
class Timeline
{
    TimelineNode *head, *tail;
    int32_t stepCount;

public:
    // Implement these functions
    Timeline()
    {
        head = nullptr;
        tail = nullptr;
        
        stepCount = 0;
    }
    void record(Snapshot *s)
    {
        TimelineNode * NewNode = new TimelineNode;
        NewNode->data = s;
        NewNode->next = nullptr;
        NewNode->prev = nullptr;


        if (head == nullptr)
        {
            head = NewNode;
            tail = NewNode;
        }
        else{
            NewNode->prev = tail;
            tail->next = NewNode;
            tail = NewNode;
        }


        stepCount++;
        // add record in the timeline
    }
    TimelineNode *begin()
    {
        return head;
    }
    int32_t getStepCount()
    {
        return stepCount;
    }
};



void writeHeader(FILE *f, const TTDBHeader &h)
{
    fwrite(h.magic, 1, 4, f);
    fwrite(&h.version, sizeof(int32_t), 1, f);

    // placeholder for other two data members
}





// PASS 0x0: READING source.bin + VALIDITY CHECK
bool readSourceLine(ifstream &in, string &out)
{
    string line;
    while (getline(in, line))
    {
        uint32_t start = 0;
        while (start < line.length() &&(line[start] == ' ' ||line[start] == '\t' ||line[start] == '\r'))
        {
            start++;

        }

        if (start < line.length() &&line[start] != '#')
        {
            out = line.substr(start);
            return true;
        }




    }

    return false;
    // reads the next nonblank line
}
string firstWord(const string &line)
{
    uint32_t start = 0;
    while (start < line.length() && (line[start] == ' ' ||line[start] == '\t' ||line[start] == '\r' || line[start] == '\n'))
    {
        start++;

    }

    uint32_t end = start;
    while (end < line.length() &&line[end] != ' ' &&line[end] != '\t' && line[end] != '\r' && line[end] != '\n')
    {
        end++;
    }

    return line.substr(start, end - start);

    
}
string secondWord(const string &line)
{
    uint32_t start = 0;

    while (start < line.length() &&(line[start] == ' ' ||line[start] == '\t' ||line[start] == '\r' || line[start] == '\n'))
    {
        start++;
    }

    while (start < line.length() && line[start] != ' ' &&line[start] != '\t' &&line[start] != '\r' && line[start] != '\n')
    {
        start++;
    }

    while (start < line.length() && (line[start] == ' ' ||line[start] == '\t' || line[start] == '\r' || line[start] == '\n'))
    {
        start++;

    }

    uint32_t end = start;

    while (end < line.length() && line[end] != ' ' && line[end] != '\t' &&line[end] != '\r' && line[end] != '\n')
    {
        end++;
    }

    return line.substr(start, end - start);
}
bool validateProgram(const char *sourcePath)
{

    ifstream fin(sourcePath);

    if (!fin)
    {
        cout << "File not found" << endl;
        return false;
    }



    string line;
    uint32_t hasMain = 0;
    uint32_t InsideFun = 0;
    while (readSourceLine(fin, line))
    {
        string first = firstWord(line);

        string second = secondWord(line);

        if (first == "func")
        {
            if (InsideFun == 1)
            {
                cout << "Nested functions not allowed." << endl;
                fin.close();
                return false;
            }

            InsideFun = 1;

            if (second == "main")
            {
                hasMain = 1;

            }

        }

        else if (first == "func_end")
        {
            if (InsideFun == 0)
            {
                cout << "unmatch func_end found," << endl;
                fin.close();
                return false;
            }
            InsideFun = 0;

        }


        else
        {
            if (InsideFun == 0)
            {
                cout << "Line out function" << endl;
                fin.close();
                return false;
            }


        }

    }

    fin.close();
    if (InsideFun == 1)
    {
        cout << "func_end is missing" << endl;
        return false;
    }
    if (hasMain == 0)
    {


       cout << "Main function is missing" << endl;

        return false;
    }

    return true;

    
}


// PASS 0x1: RESOLVE() -> resolve.bin
int64_t writeResolveRecord(FILE *f, int64_t offsetField, const string &text)
{
    int64_t start=ftell(f);

    fwrite(&offsetField, sizeof(int64_t), 1,f);

    uint32_t len = text.length();
    fwrite(&len ,sizeof(uint32_t) , 1 ,f);
    if(len>0){
        fwrite(&text[0] ,sizeof(char), len,f);
    }


    return start;

    // writes one [offset(8B)][size(4B)][string] record at the current file position
    // returns this record's own starting byte position
}
int64_t readResolveRecord(FILE *f, string &outText)
{
    int64_t offsetField = 0;
    uint32_t len = 0;
    if(!fread(&offsetField,sizeof(int64_t),1,f)){
        return -1;
    }
    if(!fread(&len,sizeof(uint32_t),1,f)){
        return -1;
    }
    outText.resize(len);
    if(len>0){
        if(fread(&outText[0],sizeof(char),len,f) !=len){
           return -1;
        }
    }

    return offsetField;
    // reads one record at the current position and advances past it, returns the offset field - the raw line text comes back untouched in outText.
}
int64_t resolveProgram(const char *sourcePath, const char *resolveBinPath)
{
    FuncEntry funcArray[MAX_FUNCS];
    int32_t funcCount = 0;
    PendingPatch patches[MAX_PATCHES];
    int32_t patchCount = 0;



    ifstream src(sourcePath);
    if (!src) {
        return -1;  
    }

    FILE *bin = fopen(resolveBinPath, "w+b");
    if (!bin) {
        src.close();
        return -1;
    }


    int64_t mainOffset=-1;
    string line;

    while(getline(src,line)){
        string temp = line + "\n";
        int64_t pos = ftell(bin);
        int64_t start = writeResolveRecord(bin, pos, temp);
       
    

        if (firstWord(temp) == "func")
        {
            if (funcCount >= MAX_FUNCS)
            {
               cout << "Too many functions" << endl;
                src.close();
               fclose(bin);
                return -1;
            }
            string funcName = secondWord(temp);

            funcArray[funcCount].funcName = funcName;
            funcArray[funcCount].byteOffsetInResolveBin = start;
            funcCount++;

            if (funcName == "main") {
                mainOffset = start;
            }
        }
        else if (firstWord(temp) == "call")
        {



            if (patchCount >= MAX_PATCHES)
            {
               cout << "Too many calls" << endl;
               src.close();
                fclose(bin);
                return -1;
           }
            
            patches[patchCount].byteOffsetOfOffsetField = start;
            patches[patchCount].targetFuncName = secondWord(temp);
            patchCount++;
        }
    }    


    src.close();


    if (mainOffset == -1) {
        fclose(bin);
        return -1;
    }



    for(int32_t i = 0 ; i <patchCount; i++){
        int64_t targetOffset = -1;

        for (int32_t j = 0; j < funcCount; j++) {
           if (funcArray[j].funcName == patches[i].targetFuncName) {
                targetOffset = funcArray[j].byteOffsetInResolveBin;
                break;
            }
        }    
        
        if (targetOffset == -1) {
            cout << "Call to undefined function: " << patches[i].targetFuncName << endl;
            fclose(bin);
            return -1;
        }

        fseek(bin, patches[i].byteOffsetOfOffsetField, SEEK_SET);
        fwrite(&targetOffset, sizeof(int64_t), 1, bin);


    }




    fclose(bin);


    return mainOffset;

      
    // Every source line becomes one record holding the raw line, as-is.
    // resolve() only PEEKS at the leading word(s) -- enough to spot FUNC
    // (remember its position) and CALL (remember which function it needs
    // and where its offset field sits).
    // Once the whole file is written, every CALL's offset field is patched
    // with its target's position. Patching happens after the full write
    // Returns the byte offset of main's FUNC header record.
    // if there is no main return the error 
}

// PASS 0x2: EXECUTION (tokenization happens here)
enum TokenType
{
    KEYWORD,
    IDENTIFIER,
    PARAM
};
struct Token
{
    TokenType type;
    string text;
};
int32_t tokenizeLine(const string &line, Token tokens[], int32_t maxTokens)
{




    int32_t Count = 0;
    size_t i = 0;
    size_t n = line.length();




    while (i < n &&Count < maxTokens)
    {
  
        while (i < n &&(line[i] == ' ' || line[i] == '\t' || line[i] == '\n' || line[i] == '\r')) {
            i++;
        }

        if (i >= n){
         break; 
        } 

        string word = "";
        while (i < n &&(line[i] != ' '&& line[i] != '\t' && line[i] != '\n' && line[i] != '\r')) {
            word +=line[i];
            i++;
        }

   
        if (word.length() >0) {
            Token t;
            t.text = word;



            if (Count == 0) {
                t.type = KEYWORD;
            } 
            else if (Count == 1) {
                t.type = IDENTIFIER;
            } 
            else {
                t.type = PARAM;
            }

            tokens[Count] = t;
            Count++;

        }
    }

    return Count;
    // first word is always a instruction keyword
    // instruction set = [func, func_end, call, set, add, sub, mul and div]
    // next word is identifier like name of a function, variable name
    // after identifier all are the params/arg, space separated
}
Snapshot *buildSnapshot(Stack<Frame> &callStack)
{
    Snapshot *snap = new Snapshot();
    snap->stackDepth = callStack.snapshot_into(snap->callStack, MAX_STACK_DEPTH);
    return snap;
    // build the snapshot based on the callStack given
}








int32_t findLocal(Frame *fr, const string &name)
{
    for (int32_t i = 0; i < fr->localCount; i++)
    {
        if (fr->locals[i].name == name)
        {
            return i;
        }
    }
    return -1;
}










void executeProgram(const char *resolveBinPath, int64_t mainOffset, Timeline &timeline)
{
    if (mainOffset == -1) {
        return;
    }
    FILE *file = fopen(resolveBinPath, "rb");

    if (!file) {
        cout << "File not found" << endl;
        return;
    }

    Stack<Frame> st;
    Frame fr;

    fr.func_name = "main";

    fr.returnLine = -1;  
    fr.localCount = 0;
    fr.argc = 0;

    st.push(fr);

    fseek(file, mainOffset, SEEK_SET);

    string line;
    Token tokens[MAX_TOKENS];

    while (!st.isEmpty())
    {
        int64_t recordOffset = readResolveRecord(file, line);
        if (recordOffset == -1) {
          break;
        }  
        int32_t tokenCount = tokenizeLine(line, tokens, MAX_TOKENS);
        if (tokenCount == 0) {
           continue;
        }

        

        Frame* curr = &st.peek();
        if (tokens[0].text == "func")
        {
            for (int32_t p = 2; p < tokenCount && (p - 2) < curr->argc; p++)
            {
                if (curr->localCount < MAX_VARS_PER_FRAME)
                {
                    curr->locals[curr->localCount].name = tokens[p].text;
                    curr->locals[curr->localCount].value = curr->argv[p - 2].value;
                    curr->localCount++;
                }
            }
        }

 
        else if (tokens[0].text == "set")
        {
            int32_t val = 0;
            if (tokenCount > 2) {
                val = stoi(tokens[2].text);
            }
            int32_t i = 0;

            for (i = 0; i < curr->localCount; i++)
            {
                if (curr->locals[i].name == tokens[1].text)
                {
                    curr->locals[i].value = val;
                    break;
                }
            }

            if (i == curr->localCount && curr->localCount < MAX_VARS_PER_FRAME)
            {
                curr->locals[i].name = tokens[1].text;
                curr->locals[i].value = val;
                curr->localCount++;
            }
        }

       
        else if (tokens[0].text == "call")
        {

            if (st.depth() >= MAX_STACK_DEPTH)
            {
               cout << "Stack overflow" << endl;
                break;
            }
            curr->returnLine = ftell(file);

            Frame newFr;
            newFr.func_name = tokens[1].text;
            newFr.argc = 0;
            newFr.localCount = 0;
            newFr.returnLine = -1;

            for (int32_t a = 2; a < tokenCount && newFr.argc < MAX_VARS_PER_FRAME; a++)
            {
                int32_t pos = findLocal(curr, tokens[a].text);
                int32_t val = 0;
                if (pos != -1)
                {
                    val = curr->locals[pos].value;
                }
                else
                {
                    val = stoi(tokens[a].text);
                }
                newFr.argv[newFr.argc].name = tokens[a].text;
                newFr.argv[newFr.argc].value = val;
                newFr.argc++;
            }

            fseek(file, recordOffset, SEEK_SET);

            st.push(newFr);
        }

    
        else if (tokens[0].text == "func_end")
        {
            Frame finished = st.pop();

            if (!st.isEmpty())
            {
                Frame *prevCurr = &st.peek();

                for (int32_t a = 0; a < finished.argc; a++)
                {
                    int32_t callerPos = findLocal(prevCurr, finished.argv[a].name);
                    if (callerPos != -1 && a < finished.localCount)
                    {
                        prevCurr->locals[callerPos].value = finished.locals[a].value;
                    }
                }

                if (prevCurr->returnLine != -1)
                {
                    fseek(file, prevCurr->returnLine, SEEK_SET);
                }
            }
        }


        else if (tokens[0].text == "add" || tokens[0].text == "sub" ||
            tokens[0].text == "mul" || tokens[0].text == "div")
        {
            int32_t dst = findLocal(curr, tokens[1].text);

            if (dst == -1 && curr->localCount < MAX_VARS_PER_FRAME)
            {
                dst = curr->localCount;
                curr->locals[dst].name = tokens[1].text;
                curr->locals[dst].value = 0;
                curr->localCount++;
            }

            int32_t operand = 0;
            if (tokenCount > 2)
            {
                int32_t src = findLocal(curr, tokens[2].text);
                if (src != -1)
                {
                    operand = curr->locals[src].value;
                }
                else
                {
                    operand = stoi(tokens[2].text);
                }
            }

            if (dst != -1)
            {
                if (tokens[0].text == "add")
                {
                    curr->locals[dst].value += operand;
                }
                else if (tokens[0].text == "sub")
                {
                    curr->locals[dst].value -= operand;
                }
                else if (tokens[0].text == "mul")
                {
                    curr->locals[dst].value *= operand;
                }
                else
                {
                    if (operand == 0)
                    {
                        cout << "Division by zero" << endl;
                    }
                    else
                    {
                        curr->locals[dst].value /= operand;
                    }
                }
            }
        }

        timeline.record(buildSnapshot(st));
    }    

    fclose(file);
}














void writeString(FILE *f, const string &s)
{
    uint32_t len = s.length();
    fwrite(&len, sizeof(uint32_t), 1, f);
    if (len > 0) {
        fwrite(&s[0], sizeof(char), len, f);
    }
}










// PASS 0x3: SERIALIZE TIMELINE
void writeTdbg(Timeline &timeline, const char *tdbgPath)
{


    if (timeline.getStepCount() == 0)
    {
        cout << "Nothing to write" << endl;
        return;
    }
    FILE *file = fopen(tdbgPath, "w+b");
    if (!file) {
        cout << "File not found "  << endl;
        return;
    }


    int32_t stepCount = timeline.getStepCount();



    TTDBHeader header;
    header.magic[0] = 'T';
    header.magic[1] = 'T';
    header.magic[2] = 'D';
    header.magic[3] = 'B';
    header.version = 1;
    header.stepCount = stepCount;
    header.indexOffset = 0;

    writeHeader(file, header);                       
    fwrite(&stepCount, sizeof(int32_t), 1, file);    
    int64_t placeholder = 0;
    fwrite(&placeholder, sizeof(int64_t), 1, file);  


   int64_t *indexArray =new int64_t[stepCount];


    TimelineNode *curr =timeline.begin();
    int32_t idx = 0;

    while (curr != nullptr && idx < stepCount)
    {

       indexArray[idx] = ftell(file);
       Snapshot *snap =curr->data;

  
       fwrite(&snap->stackDepth, sizeof(int32_t), 1, file);

        for (int32_t f = 0; f < snap->stackDepth; f++)
        {
            Frame &fr = snap->callStack[f];

            writeString(file, fr.func_name);

            fwrite(&fr.argc, sizeof(int32_t), 1, file);
            for (int32_t a = 0; a < fr.argc; a++)
            {
                writeString(file, fr.argv[a].name);
                fwrite(&fr.argv[a].value, sizeof(int32_t), 1, file);
            }

            fwrite(&fr.returnLine, sizeof(int32_t), 1, file);

            fwrite(&fr.localCount, sizeof(int32_t), 1, file);
            for (int32_t l = 0; l < fr.localCount; l++)
            {
                writeString(file, fr.locals[l].name);
                fwrite(&fr.locals[l].value, sizeof(int32_t), 1, file);
            }
        }

        curr =curr->next;
        idx++;
    }


    int64_t idxOffset = ftell(file);

   
    for (int32_t i = 0; i < stepCount; i++)
    {
        fwrite(&indexArray[i], sizeof(int64_t), 1, file);
    }

   
    delete[] indexArray;



    

  
    fseek(file, 12, SEEK_SET);                       
    fwrite(&idxOffset, sizeof(int64_t), 1, file);

    fclose(file);
    // placeholder for header
    // index array of the size of stepcount from the timeline
    // placing each snapshot in the file while maintaining the index(starting point of each nth snapshot)
    // after timeline add the index array i the file
    // update the header
}
bool receiveSource(int clientSock, string &error)
{
    TrailerChecker checker;
    string verifiedData;
    char buffer[IO_BUFFER_SIZE];
    bool tooLarge = false;

    while (true)
    {
        ssize_t n = recv(clientSock, buffer, sizeof(buffer), 0);

        if (n < 0)
        {
            if (errno == EINTR)
                continue;

            error = "failed to receive upload";
            return false;
        }

        if (n == 0)
            break;

        string safe;
        checker.feed(buffer, (size_t)n, safe);


        if (!tooLarge)
        {
            verifiedData.append(safe);

            if (verifiedData.size() > MAX_SOURCE_BYTES + 1)
            {
                tooLarge = true;
                verifiedData.clear();
            }
        }
    }

    if (tooLarge)
    {
        error = "source file is too large";
        return false;
    }

    if (!checker.verify())
    {
        error = "upload checksum mismatch";
        return false;
    }

    if (verifiedData.empty())
    {
        error = "empty upload";
        return false;
    }

    uint8_t status = (uint8_t)verifiedData[0];
    if (status != STATUS_OK)
    {
        error = "invalid upload status";
        return false;
    }

    ofstream source("server_input.tmp", ios::binary | ios::trunc);
    if (!source)
    {
        error = "cannot create source.bin";
        return false;
    }

    source.write(
        verifiedData.data() + 1,
        (streamsize)(verifiedData.size() - 1));

    if (!source)
    {
        error = "cannot write source.bin";
        return false;
    }

    return true;
}

bool runPipeline(string &error)
{
    if (!validateProgram("server_input.tmp"))
    {
        error = "source validation failed";
        return false;
    }

    int64_t mainOffset = resolveProgram("server_input.tmp", "resolve.bin");
    if (mainOffset < 0)
    {
        error = "function resolution failed";
        return false;
    }

    Timeline timeline;
    executeProgram("resolve.bin", mainOffset, timeline);

    // Purani file ko pehle hatao taake failure par stale result na bhejein.
    remove("session.tdbg");
    writeTdbg(timeline, "session.tdbg");

    ifstream result("session.tdbg", ios::binary);
    if (!result)
    {
        error = "could not create debug file";
        return false;
    }

    return true;
}


void handleClient(int clientSock)
{
    string error;

    if (!receiveSource(clientSock, error))
    {
        sendMessage(clientSock, STATUS_ERROR, error);
        close(clientSock);
        return;
    }

    remove("resolve.bin");
    remove("session.tdbg");

    if (!runPipeline(error))
    {
        sendMessage(clientSock, STATUS_ERROR, error);
        close(clientSock);
        return;
    }

    ifstream file("session.tdbg", ios::binary);
    if (!file)
    {
        sendMessage(clientSock, STATUS_ERROR, "could not open debug file");
        close(clientSock);
        return;
    }

    string tdbgData(
        (istreambuf_iterator<char>(file)),
        istreambuf_iterator<char>());

    if (!sendMessage(clientSock, STATUS_OK, tdbgData))
    {
        close(clientSock);
        return;
    }

    close(clientSock);
}


int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        cerr << "Usage: ./server <port>" << endl;
        return 1;
    }

    int port = atoi(argv[1]);
    if (port < 1 || port > 65535)
    {
        cerr << "Invalid port" << endl;
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    int listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSock < 0)
    {
        perror("socket");
        return 1;
    }

    int reuse = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)port);

    if (bind(listenSock, (sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind");
        close(listenSock);
        return 1;
    }

    if (listen(listenSock, 16) < 0)
    {
        perror("listen");
        close(listenSock);
        return 1;
    }

    cout << "Server listening on port " << port << endl;

    while (true)
    {
        int clientSock = accept(listenSock, nullptr, nullptr);

        if (clientSock < 0)
        {
            if (errno == EINTR)
                continue;

            perror("accept");
            continue;
        }

        handleClient(clientSock);
    }

    close(listenSock);
    return 0;
}