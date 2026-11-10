// ======================= TIME-TRAVEL DEBUGGER - CLIENT TEMPLATE =======================

// The client never talks to the server directly. It only talks to the load balancer.
// The load balancer wakes the server up if it is down, so the client does not care.
// The first "start" may just take a bit longer.
//
// The network is used ONLY by "start". After the .tdbg file is downloaded,
// next / prv / jmp read that file on the local disk. They never contact the load balancer.
//
// How the pieces fit together:
//   main loop -> readCommand -> runCommand
//   start -> cmdStart()  -> requestDebugFile() (Part A) -> openTdbg() (Part B) -> shows step 1
//   next, prv, jmp -> moveBy() (Part D) -> readSnapshotAt() (Part B) -> printSnapshot() (Part E)

#include <iostream>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <vector>
#include <sys/socket.h>
#include "helper.h"
#include <cerrno>
#include <cctype>
#include <fcntl.h>
#include <sstream>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
using namespace std;

// ---- Constants ----
const char *PROGRAM_NAME = "ttdb";
const char *PROMPT = "ttdb> ";

const char *TDBG_FILE = "ttdb_last.tdbg"; // where the downloaded .tdbg is saved
const int MAX_JUMP = 1000000000;          // biggest n accepted by jmp
string LB_HOST = "127.0.0.1";
int LB_PORT = 5000;

const char *TDBG_TMP_FILE = "ttdb_last.tdbg.part";
const size_t MAX_ERROR_BYTES = 64 * 1024;
const uint32_t MAX_NAME_LEN = 4096;





// ---- Structs only the client needs ----
struct TdbgReader
{
    ssize_t file_index;
    TTDBHeader header;
    vector<int64_t> index;
};
struct DebugSession
{
    int active_flag;     // false until a "start" has worked
    TdbgReader reader;   // the .tdbg file stays open for the whole session
    int64_t currentStep; 
};

// =============================================================================================
// PART A: TALKING TO THE LOAD BALANCER
// =============================================================================================


int connectToHost(const char *host, int port)
{

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    if(sock <0){
        return -1;
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;               
    addr.sin_port = htons((uint16_t)port);


    if(inet_pton(AF_INET,host,&addr.sin_addr) != 1){
        close(sock);
        return -1;
    }

    if (connect(sock, (sockaddr *)&addr, sizeof(addr)) < 0)
    { 
      close(sock);
      return -1;
    }


    return sock;


    // Create a TCP socket and connect it to host:port.
    // Return the socket, or -1 if the connection fails.
}
bool uploadSource(int sock, const char *sourcePath)
{

    FILE *f = fopen(sourcePath , "rb");
    if(!f){
        return false;
    }

   
    Crc32 crc;
    uint8_t status = STATUS_OK;  
    crc.update(&status, 1);
    bool ok = sendAll(sock, (const char*)&status, 1); 



    char buf[IO_BUFFER_SIZE];
    while(ok){
        size_t n = fread(buf , 1 ,sizeof(buf) ,f);
        if(n==0){
            break;
        }
        crc.update(buf,n);
        ok = sendAll(sock , buf ,n);
    }

    fclose(f);

    if(ok){
        char trailer[4];

        crcToBytes(crc.value() , trailer);
        ok = sendAll(sock,trailer,4);

    }

    shutdown(sock,SHUT_WR);

    return ok;

    // have to do checksum and then send
    // When the file is finished, call shutdown(sock, SHUT_WR). This is how the server knows the string is complete.
    // Return false if the file cannot be opened or any send fails.
}
bool receiveResponse(int sock, const char *outTdbgPath, string &outError)
{
    outError.clear();

    TrailerChecker checker;
    string safe;
    string errText;
    char buf[IO_BUFFER_SIZE];

    FILE *out = nullptr;
    bool fileCreated = false;
    bool statusKnown = false;
    uint8_t status = 0;
    bool netFail = false;
    bool writeFail = false;

    while (true)
    {
        ssize_t n = recv(sock, buf, sizeof(buf), 0);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            netFail = true;
            break;
        }
        if (n == 0)
        {
            break;
        }

        safe.clear();
        checker.feed(buf, (size_t)n, safe);

        size_t pos = 0;
        if (!statusKnown && !safe.empty())
        {
            status = (uint8_t)safe[0];
            statusKnown = true;
            pos = 1;

            if (status == STATUS_OK)
            {
                out = fopen(outTdbgPath, "wb");
                if (!out)
                {
                    writeFail = true;
                    break;
                }
                fileCreated = true;
            }
        }

        if (statusKnown && pos < safe.size())
        {
            size_t len = safe.size() - pos;
            if (status == STATUS_OK)
            {
                if (fwrite(safe.data() + pos, 1, len, out) != len)
                {
                    writeFail = true;
                    break;
                }
            }
            else if (status == STATUS_ERROR)
            {
                if (errText.size() < MAX_ERROR_BYTES)
                {
                    errText.append(safe, pos, string::npos);
                }
            }
        }
    }

    if (out)
    {
        fclose(out);
    }

    if (writeFail)
    {
        if (fileCreated)
        {
            remove(outTdbgPath);
        }
        outError = "cannot write the downloaded file to disk";
        return false;
    }

    if (netFail)
    {
        if (fileCreated)
        {
            remove(outTdbgPath);
        }
        outError = "connection to load balancer lost (or timed out waiting for the reply)";
        return false;
    }

    if (!statusKnown)
    {
        outError = "no reply from load balancer (queue timeout or connection closed)";
        return false;
    }

    if (!checker.verify())
    {
        if (fileCreated)
        {
            remove(outTdbgPath);
        }
        outError = "checksum mismatch, response corrupted";
        return false;
    }

    if (status == STATUS_ERROR)
    {
        if (errText.empty())
        {
            outError = "server reported an error";
        }
        else
        {
            outError = errText;
        }
        return false;
    }

    if (status != STATUS_OK)
    {
        if (fileCreated)
        {
            remove(outTdbgPath);
        }
        outError = "unknown reply status";
        return false;
    }

    return true;
}

// =============================================================================================
// PART B: READING THE .tdbg FILE
// File layout (written by Phase 1):
//   header | snapshot 0 | snapshot 1 | ... | index (one 8 byte offset per snapshot)
// The index lets us jump straight to any step without reading the steps before it.
// =============================================================================================





static bool readExact(int fd, void *buf, size_t n)
{
    char *p = (char *)buf;
    size_t got = 0;

    while (got < n){

        ssize_t r = ::read(fd, p + got, n - got);
        if (r < 0){

            if (errno == EINTR){

                continue;
            }
            return false;
        }
        if (r == 0){

            return false;
        }
        got += (size_t)r;
    }
    return true;
}
















static bool closeAndFail(int fd, TdbgReader &r)
{
    close(fd);
    r.index.clear();
    return false;
}


bool openTdbg(const char *path, TdbgReader &r)
{
    r.file_index = -1;
    r.index.clear();

    int fd = open(path, O_RDONLY);
    if (fd < 0){
        return false;

    }

    TTDBHeader h;
    memset(&h, 0, sizeof(h));
    if (!readExact(fd, h.magic, 4) ||
        !readExact(fd, &h.version, sizeof(int32_t)) ||
        !readExact(fd, &h.stepCount, sizeof(int32_t)) ||
        !readExact(fd, &h.indexOffset, sizeof(int64_t)))
    {
        return closeAndFail(fd, r);
    }



    if (memcmp(h.magic, "TTDB", 4) != 0 || h.stepCount <= 0){

        return closeAndFail(fd, r);
    }

    off_t size = lseek(fd, 0, SEEK_END);

    if (size < 0 || h.indexOffset < TTDB_HEADER_BYTES || h.indexOffset > (int64_t)size){

        return closeAndFail(fd, r);
    }
    if ((int64_t)size != h.indexOffset + (int64_t)h.stepCount * 8){

        return closeAndFail(fd, r);
    }


        if (lseek(fd, (off_t)h.indexOffset,SEEK_SET) < 0)
    {
        return closeAndFail(fd, r);
    }

    r.index.resize((size_t)h.stepCount);
    if (!readExact(fd, r.index.data(), (size_t)h.stepCount * sizeof(int64_t)))
    {
        return closeAndFail(fd, r);
    }

    for (int32_t i = 0; i < h.stepCount; i++)
    {
        if (r.index[i]< TTDB_HEADER_BYTES || r.index[i] >=h.indexOffset)
        {
            return closeAndFail(fd, r);
        }
    }


    r.header = h;
    r.file_index = fd;
    return true;







}























void closeTdbg(TdbgReader &r)
{

    vector<int64_t>().swap(r.index);
    if (r.file_index >= 0){

        close((int)r.file_index);
    }
    r.file_index = -1;
    // Free r.index and close the file.
}












static bool readString(int fd, string &s)
{
    uint32_t len = 0;
    if (!readExact(fd, &len, sizeof(uint32_t)) || len > MAX_NAME_LEN)
    {
        return false;
    }

    s.resize(len);
    if (len == 0)
    {
        return true;
    }
    return readExact(fd, &s[0], len);
}








static bool readVariables(int fd, Variable arr[], int32_t count)
{
    for (int32_t i = 0; i < count; i++)
    {
        if (!readString(fd, arr[i].name))
        {
            return false;
        }
        if (!readExact(fd, &arr[i].value, sizeof(int32_t)))
        {
            return false;
        }
    }
    return true;
}




static bool readFrame(int fd, Frame &fr)
{
    if (!readString(fd, fr.func_name))
    {
        return false;
    }

    if (!readExact(fd, &fr.argc, sizeof(int32_t)) ||
        fr.argc < 0 || fr.argc > MAX_VARS_PER_FRAME)
    {
        return false;
    }
    if (!readVariables(fd, fr.argv, fr.argc))
    {
        return false;
    }

    if (!readExact(fd, &fr.returnLine, sizeof(int32_t)))
    {
        return false;
    }

    if (!readExact(fd, &fr.localCount, sizeof(int32_t)) ||
        fr.localCount < 0 || fr.localCount > MAX_VARS_PER_FRAME)
    {
        return false;
    }
    return readVariables(fd, fr.locals, fr.localCount);
}











Snapshot *readSnapshotAt(TdbgReader &r, int32_t step)
{

    if (r.file_index < 0 || step < 0 || step >= r.header.stepCount)
    {
        return nullptr;
    }



    int fd = (int)r.file_index;
    if (lseek(fd, (off_t)r.index[step], SEEK_SET) < 0)
    {
        return nullptr;
    }



    Snapshot *s = new Snapshot();
    int32_t depth = 0;
    if (!readExact(fd, &depth, sizeof(int32_t)) || depth < 0 || depth > MAX_STACK_DEPTH)
    {
        delete s;
        return nullptr;
    }
    s->stackDepth = depth;



    for (int32_t i = 0; i < depth; i++)
    {
        if (!readFrame(fd, s->callStack[i]))
        {
            delete s;
            return nullptr;
        }
    }

    return s;



}




// =============================================================================================
// PART C: THE SESSION IN MEMORY
// There is one DebugSession variable, created in main() and passed to every command by reference.
// =============================================================================================

void initSession(DebugSession &s)
{
    s.active_flag = 0;
    s.reader.file_index = -1;
    memset(&s.reader.header, 0, sizeof(s.reader.header));
    s.reader.index.clear();
    s.currentStep = 0;
   
}
void closeSession(DebugSession &s)
{

    if (s.active_flag)
    {
        closeTdbg(s.reader);
    }
    s.active_flag = 0;


    // If active, call closeTdbg(s.reader). Then set active = false.
    // Called when the user types quit and when the input ends.
}

// =============================================================================================
// PART D: THE COMMANDS
// =============================================================================================

void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s); // defined in Part E

bool showCurrent(DebugSession &s)
{
    Snapshot *snap = readSnapshotAt(s.reader, (int32_t)s.currentStep);
    if (snap == nullptr)
    {
        cout << "damaged .tdbg file" << endl;
        return false;
    }

    printSnapshot((int32_t)s.currentStep + 1, s.reader.header.stepCount, *snap);
    delete snap;
    return true;




    // Read the snapshot at s.currentStep, call printSnapshot, delete the snapshot.
    // If readSnapshotAt returns nullptr, print "damaged .tdbg file" and return false.
}
bool moveBy(DebugSession &s, int32_t delta)
{

    if (!s.active_flag)
    {
        cout << "no active session, run: start <source file>" << endl;
        return false;
    }

    
    int64_t total = s.reader.header.stepCount;
    int64_t target = s.currentStep + (int64_t)delta;

    // Hissa 3: range check
    if (target >= total)
    {
        cout << "cannot go forward " << delta << " step(s): only "
             << (total - 1 - s.currentStep) << " step(s) remain" << endl;
        return false;
    }
    if (target < 0)
    {
        cout << "cannot go back " << -(int64_t)delta << " step(s): only "
             << s.currentStep << " step(s) behind" << endl;
        return false;
    }


    int64_t old = s.currentStep;
    s.currentStep = target;
    if (!showCurrent(s))
    {
        s.currentStep = old;
        return false;
    }
    return true;
}














bool requestDebugFile(const char *host, int port, const char *sourcePath,
                      const char *outPath, string &outError)
{
  
    FILE *probe = fopen(sourcePath, "rb");
    if (!probe)
    {
        outError = string("cannot open source file: ") + sourcePath;
        return false;
    }
    fclose(probe);


    int sock = connectToHost(host, port);
    if (sock < 0)
    {
        outError = string("cannot connect to load balancer at ") + host + ":" + to_string(port);
        return false;
    }

    bool uploaded = uploadSource(sock, sourcePath);
    bool got = receiveResponse(sock, outPath, outError);
    close(sock);

    if (!got && !uploaded &&
        (outError.compare(0, 8, "no reply") == 0 || outError.compare(0, 10, "connection") == 0))
    {
        outError = "upload to the load balancer failed";
    }
    return got;
}








bool cmdStart(DebugSession &s, const string &sourcePath)
{

    string err;
    if (!requestDebugFile(LB_HOST.c_str(), LB_PORT, sourcePath.c_str(), TDBG_TMP_FILE, err))
    {
        cout << "error: " << err << endl;
        return false;
    }

    TdbgReader fresh;

    fresh.file_index = -1;
    memset(&fresh.header, 0, sizeof(fresh.header));

    if (!openTdbg(TDBG_TMP_FILE, fresh))
    {
        cout << "error: server sent a damaged .tdbg file" << endl;
        remove(TDBG_TMP_FILE);
        return false;
    }



    rename(TDBG_TMP_FILE, TDBG_FILE);
    if (s.active_flag)
    {
        closeTdbg(s.reader);
    }
    s.reader = fresh;
    s.currentStep = 0;
    s.active_flag = 1;

    return showCurrent(s);


    // 1. requestDebugFile with host and port, saving to TDBG_FILE.
    // 2. If it fails, print the error text and return false. Keep the old session as it was.
    // 3. Open the new file with openTdbg into a NEW local TdbgReader.
    //    If that fails, print "server sent a damaged .tdbg file", keep the old session, return false.
    // 4. Only now close the old reader (if s.active), then put the new reader into s,
    //    set s.currentStep = 0 and s.active = true.
    // 5. Show step 1 with showCurrent(s).
}
bool cmdNext(DebugSession &s)
{
    return moveBy(s, 1);
}

bool cmdPrv(DebugSession &s)
{
    return moveBy(s, -1);
}

bool cmdJmpNext(DebugSession &s, int32_t n)
{
    return moveBy(s, n);
}

bool cmdJmpPrv(DebugSession &s, int32_t n)
{
    return moveBy(s, -n);
}

// =============================================================================================
// PART E: PRINTING
// =============================================================================================



static void printVars(const char *label, const Variable arr[], int32_t count)
{
    cout << "      " << label;
    if (count == 0)
    {
        cout << " (none)" << endl;
        return;
    }

    for (int32_t i = 0; i < count; i++)
    {
        if (i == 0)
        {
            cout << " ";
        }
        else
        {
            cout << ", ";
        }
        cout << arr[i].name << "=" << arr[i].value;
    }
    cout << endl;
}





void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s)
{
    cout << "\n=== Step " << step << " / " << totalSteps << " ===" << endl;

    if (s.stackDepth == 0)
    {
        cout << "(program finished, call stack is empty)" << endl;
        return;
    }

    cout << "Call stack (top first):" << endl;
    for (int32_t i = 0; i < s.stackDepth; i++)
    {
        const Frame &fr = s.callStack[i];

        cout << "  [" << i << "] " << fr.func_name;
        if (i == 0)
        {
            cout << "   <- running";
        }
        cout << endl;

        if (fr.argc > 0)
        {
            printVars("args:  ", fr.argv, fr.argc);
        }
        printVars("locals:", fr.locals, fr.localCount);
    }
}


void printHelp()
{

    cout << "Commands:\n"
        << "  start <source file>     send the program to the server and begin debugging\n"
        << "  next                    go one step forward\n"
        << "  prv                     go one step back\n"
        << "  jmp next <n>            go n steps forward\n"
        << "  jmp prv <n>             go n steps back\n"
        << "  help                    show this text\n"
        << "  quit | exit             leave the debugger\n"
        << "(you may also write 'ttdb' before a command)" << endl;
    // Print the commands
}

// =============================================================================================
// PART F: THE INFINITE LOOP
// =============================================================================================

static bool parsePositive(const string &t, int32_t &out)
{
    if (t.empty() || t.size() > 10)
    {
        return false;
    }

    for (size_t i = 0; i < t.size(); i++)
    {
        if (!isdigit((unsigned char)t[i]))
        {
            return false;
        }
    }

    long long v = stoll(t);
    if (v <= 0 || v > MAX_JUMP)
    {
        return false;
    }

    out = (int32_t)v;
    return true;
}














bool runCommand(DebugSession &s, const string &line, bool &quitRequested)
{

    vector<string> w;
    istringstream iss(line);
    string tok;
    while (iss >> tok)
    {
        w.push_back(tok);
    }

    if (w.empty())
    {
        return true;
    }

    
    if (w[0] == PROGRAM_NAME)
    {
        w.erase(w.begin());
    }
    if (w.empty())
    {
        printHelp();
        return true;
    }


    const string &cmd = w[0];

    if (cmd == "start" && w.size() == 2)
    {
        return cmdStart(s, w[1]);
    }
    if (cmd == "next" && w.size() == 1)
    {
        return cmdNext(s);
    }
    if (cmd == "prv" && w.size() == 1)
    {
        return cmdPrv(s);
    }
    if (cmd == "jmp" && w.size() == 3 && (w[1] == "next" || w[1] == "prv"))
    {
        int32_t n = 0;
        if (!parsePositive(w[2], n))
        {
            cout << "jmp needs a positive number (1 to " << MAX_JUMP << ")" << endl;
            return false;
        }
        if (w[1] == "next")
        {
            return cmdJmpNext(s, n);
        }
        return cmdJmpPrv(s, n);
    }
    if (cmd == "help" && w.size() == 1)
    {
        printHelp();
        return true;
    }
    if ((cmd == "quit" || cmd == "exit") && w.size() == 1)
    {
        quitRequested = true;
        return true;
    }

   
    cout << "unknown command" << endl;
    printHelp();
    return false;
    // 1. Split the line into words.
    // 2. If the first word is "ttdb", drop it.
    // 3. Match the first word:
    //      start      -> needs a path after it, then cmdStart
    //      next       -> no more words, then cmdNext
    //      prv        -> no more words, then cmdPrv
    //      jmp        -> second word "next" or "prv", third word a positive number ,
    //                    then cmdJmpNext or cmdJmpPrv
    //      help       -> printHelp
    //      quit, exit -> set quitRequested = true
    // 4. For anything else, or a wrong number of words, print "unknown command" and the help text, and return false.
}
int main(int argc, char *argv[])
{

    if (argc != 3)
    {
        cerr << "usage: " << argv[0] << " <lb_ip> <lb_port>" << endl;
        return 1;
    }

    LB_HOST = argv[1];

    char *end = nullptr;
    long p = strtol(argv[2], &end, 10);
    if (*argv[2] == '\0' || *end != '\0' || p < 1 || p > 65535)
    {
        cerr << "invalid port: " << argv[2] << endl;
        return 1;
    }
    LB_PORT = (int)p;

    
    signal(SIGPIPE, SIG_IGN);

  
    DebugSession session;
    initSession(session);

    cout << "Time-Travel Debugger client (load balancer " << LB_HOST << ":" << LB_PORT << ")" << endl;
    printHelp();

 
    bool quitRequested = false;
    string line;
    while (true)
    {
        if (quitRequested)
        {
            break;
        }

        cout << PROMPT << flush;
        if (!getline(cin, line))
        {
            break;
        }

        runCommand(session, line, quitRequested);
    }

    
    closeSession(session);
    return 0;
    // you get string LB_HOST and int LB_PORT = 0 in arguments of this main function. 

    // DebugSession session; initSession(session);
    // Print a short welcome and the help text.
    //
    // Infinite loop:
    //   if quitRequested, break
    //
    // After the loop: closeSession(session) and return 0.
    // Never exit the loop because of an error. Only quit or the end of the input leaves it.

}