#ifndef DEVBUILD_HPP
#define DEVBUILD_HPP

#include <iostream>
#include <string>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define SrcDir "./src"
#define TargetDir "./target"
#define ObjDir (string(TargetDir) + "/" + "obj")
#define EntryPoint (string(SrcDir) + "/" + "main.cpp")
#define ExeFileName "test"

#define Compiler "clang++"
#define ProjType ".cpp"

#define CxxFlags {}
#define LdFlags {}

#define WatchDefaultExts {ProjType, ".h", ".build"}

using namespace std;
namespace fs = filesystem;

#ifndef REBUILD_YOURSELF
#  if _WIN32
#    if defined(__GNUC__)
#       define REBUILD_YOURSELF(binary_path, source_path) "g++", "-std=c++17", "-o", binary_path, source_path
#    elif defined(__clang__)
#       define REBUILD_YOURSELF(binary_path, source_path) "clang++","-std=c++17", "-o", binary_path, source_path
#    endif
#  else
#    define REBUILD_YOURSELF(binary_path, source_path) "c++", "-std=c++17", "-o", binary_path, source_path
#  endif
#endif

#define RESET   "\033[0m"
#define RED     "\033[31m"
#define CYAN    "\033[36m"
#define YELLOW  "\033[33m"

using namespace std;

enum LogImp {
    LOGINFO,
    LOGWARNING,
    LOGERROR
};

struct Logger {
    void SendMessage(LogImp imp, const string& message) {
        string prefix;
        string color;

        if (imp == LOGINFO) {
            prefix = "[INFO] ";
            color = CYAN;
        } else if (imp == LOGWARNING) {
            prefix = "[WARNING] ";
            color = YELLOW;
        } else if (imp == LOGERROR) {
            prefix = "[ERROR] ";
            color = RED;
        }

        (imp == LOGERROR ? cerr : cout) << color << prefix << RESET << message << endl;
    }
};

inline int DoesExistAndIsDir(const string& path) {
    if (!fs::exists(path)) {
        return 0;
    } else if (!fs::is_directory(path)) {
        return -1;
    }
    return 1;
}

inline void ConfigSetup(Logger log) {
    if (!fs::exists(SrcDir) || !fs::is_directory(SrcDir)) {
        log.SendMessage(LOGERROR, "source directory: '" + string(SrcDir) + "' doesnt exist") ;
        exit(69);
    }

    int res = DoesExistAndIsDir(TargetDir);
    if (res == 0) {
        log.SendMessage(LOGINFO, "target directory: '" + string(TargetDir) + "' doesn't exist so creating...");
        fs::create_directories(TargetDir);
    } else if (res == -1) {
        log.SendMessage(LOGERROR, "target directory: '" + string(TargetDir) + "' is a file");
        exit(69);
    }

    res = DoesExistAndIsDir(ObjDir);
    if (res == 0) {
        log.SendMessage(LOGINFO, "object directory: '" + string(ObjDir) + "' doesn't exist so creating...");
        fs::create_directories(ObjDir);
    } else if (res == -1) {
        log.SendMessage(LOGERROR, "object directory: '" + string(ObjDir) + "' is a file");
        exit(69);
    }
}

struct Task {
    vector<string> cmd;
    string output = "";

    int run(Logger log, bool saveToFile = false) {
        if (cmd.empty()) {
            log.SendMessage(LOGERROR, "task failed command is empty");
            return -1;
        }

        string fullCmd;
        for (const auto& part : cmd) {
            fullCmd += part + " ";
        }
        
        if (!fullCmd.empty()) {
            fullCmd.pop_back();
        }

        log.SendMessage(LOGINFO, "starting task '"+ fullCmd + "'");

        #ifdef _WIN32
            STARTUPINFO si = { sizeof(STARTUPINFO) };
            PROCESS_INFORMATION pi;
        
            string commandStr = fullCmd;
            char* command = &commandStr[0];
        
            if (!CreateProcess(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                log.SendMessage(LOGERROR, "failed to start process: " + fullCmd);
                return -1;
            }
        
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD exitCode;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return exitCode;
        #else
            int pipefd[2];
            if (saveToFile) {
                if (pipe(pipefd) == -1) {
                    log.SendMessage(LOGERROR, "failed to create pipe");
                    return -1;
                }
            }
            pid_t pid = fork();
        
            if (pid == -1) {
                log.SendMessage(LOGERROR, "failed to fork process for task");
                return -1;
            } else if (pid == 0) {
                if (saveToFile) {
                    close(pipefd[0]);
                    dup2(pipefd[1], STDOUT_FILENO);
                    dup2(pipefd[1], STDERR_FILENO);
                    close(pipefd[1]);
                }

                vector<char*> args;
                for (auto& arg : cmd) args.push_back(strdup(arg.c_str()));
                args.push_back(nullptr);
        
                execvp(args[0], args.data());
                perror("execvp failed");
                exit(1);
            } else {
                if (saveToFile) {
                    close(pipefd[1]);
                    char buffer[512];
                    ssize_t bytesRead;

                    while ((bytesRead = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
                        buffer[bytesRead] = '\0';
                        output += buffer;
                    }

                    close(pipefd[0]);
                }

                int status;
                waitpid(pid, &status, 0);
                return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            }
        #endif
    }
};

struct CliCommand {
    string name;
    string description;
    void (*cb)(int, char**, Logger);
};

struct Cli {
    vector<CliCommand> cmds; 
    Logger log;

    void help() {
        cout << "help: ./dev" << endl;
        for (const auto& cmd : cmds) {
            cout << "\t" << cmd.name << " - " << cmd.description << endl;
        }
        cout << "\t" << "help - prints this message" << endl;
    }

    void go(int argc, char** argv) {
        if (argc <= 1) {
            log.SendMessage(LOGERROR, "no command provided");
            help();
            exit(69);
        }

        auto it = find_if(cmds.begin(), cmds.end(), [&](const CliCommand& cmd) {
                return cmd.name == argv[1];
                });

        if (it != cmds.end()) {
            it->cb(argc, argv, log);
        } else if (string(argv[1]) == "help") {
            help();
        } else {
            log.SendMessage(LOGERROR, "unknown command: " + string(argv[1]));
            help();
        }
    }

    Cli(Logger l) : log(l) {}
};

struct BuildOptions {
    Logger log;
    map<string, string*> vars;
    vector<string> vec;

    string build;
    string buildwindows;
    string outfolder;
    string outname;

    BuildOptions(Logger Log) : log(Log), build(""), buildwindows(""), outfolder(""), outname("") {
        vars = {
            {"build", &build},
            {"buildWindows", &buildwindows},
            {"outfolder", &outfolder},
            {"outname", &outname}
        };

        for (const auto& [name, _] : vars) {
            vec.push_back(name);
        }
    }

    int operator()(const string& var, const string& value) {
        auto it = vars.find(var);
        if (it != vars.end()) {
            *(it->second) = value;
            if (var == "outfolder") {
                string path = string(TargetDir) + "/" + value;
                int res = DoesExistAndIsDir(path);
                if (res == 0) {
                    log.SendMessage(LOGINFO, "creating .build module outfolder --> '" + value + "'"); 
                    fs::create_directories(path);
                } else if (res == -1) {
                    log.SendMessage(LOGERROR, "your outname in your .build module '" + value + "' is a file so this shit not gone working quiting..."); 
                    exit(69);
                }
            }
            return 1;
        }
        return 0;
    }
};

struct BuildExtensionLexer {
    ifstream file;
    Logger log;

    string toLower(string thing) {
        transform(thing.begin(), thing.end(), thing.begin(), [](unsigned char c){return tolower(c);});
        return thing;
    }

    int ParseLine(const string& line, BuildOptions opts) {
        if (line.rfind("--", 0) == 0) {
            // log.SendMessage(LOGINFO, "FOUND COMMENT '" + line + "'");
            return 0;
        }

        size_t pos = line.find(":");
        if (pos != string::npos) {
            // log.SendMessage(LOGINFO, "found token : at position " + to_string(pos) + " --> " + "'" + line + "'");

            string beforeColon = line.substr(0, pos);
            beforeColon.erase(beforeColon.find_last_not_of(" \t") + 1);

            // log.SendMessage(LOGINFO, "token before ':' is '" + beforeColon + "'");

            if (!beforeColon.empty()) {
                string key =toLower(beforeColon);
                int found = 0;


                for (string opt : opts.vec) {
                    if (key == opt) {
                        found = 1;
                        break;
                    }
                }

                if (found) {
                    // log.SendMessage(LOGINFO, "valid option found: '" + beforeColon + "'");
                    string value = line.substr(pos + 1);
                    value.erase(0, value.find_first_not_of(" \t"));

                    if (value.empty()) {
                        log.SendMessage(LOGWARNING, "option '" + beforeColon + "' has no value.");
                        return 0;
                    }

                    opts(key, value);
                    return 1;
                } else {
                    log.SendMessage(LOGWARNING, "ignoring invalid option '" + key + "'");
                }
            }
        }
        return 0;
    }

    BuildOptions Parse() {
        BuildOptions options(log);
        string line;
        while (getline(file, line)) {
            ParseLine(line, options);
        }

        // DEBUG
        if (!options.build.empty()) {
            log.SendMessage(LOGINFO, "build ext opt value --> '" + options.build + "'");
        }
        if (!options.buildwindows.empty()) {
            log.SendMessage(LOGINFO, "build ext windows opt value --> '" + options.buildwindows + "'");
        }

        /* if (!options.outfolder.empty()) {
            cout << "outfolder opt dedected " << options.outfolder << endl;
        } */

        return options;
    }

    BuildExtensionLexer(string path, Logger Log) : file(path), log(Log) {
        if (!file.is_open()) {
            log.SendMessage(LOGERROR, "failed to open build extension file -> '" + path + "'");
            throw runtime_error("failed to open file");
        }
    }
    ~BuildExtensionLexer() {
        file.close();
    }
};

inline void GenerateFunc(int argc, char** argv, Logger log) {
    ConfigSetup(log);
    // log.SendMessage(LOGINFO, "starting generation of ninja build script");

    string ninjaFile = "build.ninja";
    ofstream file(ninjaFile);

    if (!file.is_open()) {
        log.SendMessage(LOGERROR, "failed to create the ninja build file: " + ninjaFile);
        return;
    }

    file << "target=" << TargetDir << "\n";
    file << "objdir=" << ObjDir << "\n";
    vector<string> cxxflags = CxxFlags;
    string cxxflagsstr;
    for (const auto& part : cxxflags) {
        cxxflagsstr += part + " ";
    }
    vector<string> ldflags = LdFlags;
    string ldflagsstr;
    for (const auto& part : ldflags) {
        ldflagsstr += part + " ";
    }
    file << "cxxflags=" << cxxflagsstr << "\n";
    file << "ldflags=" << ldflagsstr << "\n\n";
    file << "rule cxx\n";
    file << "   command = " << Compiler << " $cxxflags -c $in -o $out\n";
    file << "rule link\n";
    file << "   command = " << Compiler << " $ldflags $in -o $out\n\n";

    vector<string> donottouch;
    vector<string> sources;
    for (const auto& entry : fs::recursive_directory_iterator(SrcDir)) {
        if (fs::is_directory(entry.path().string())) {
            for (const auto& otherEntry : fs::recursive_directory_iterator(entry.path())) {
                if (otherEntry.path().stem() == ".build") {
                    // cout << "found" << endl;
                    // cout << entry.path() << endl;
                    BuildExtensionLexer lexer(otherEntry.path(), log);
                    BuildOptions opts = lexer.Parse();

                    string overlycomplex = otherEntry.path().parent_path().string() + "/" + otherEntry.path().parent_path().stem().string() + ProjType;
                    if (!fs::exists(overlycomplex)) {
                        log.SendMessage(LOGERROR, "cannot build module '" + otherEntry.path().parent_path().string() + "' your .build module must contain " + string(ProjType) + " file with the name of your module this acts as an entry");
                        continue;
                    } else if (opts.outname.empty()) {
                        log.SendMessage(LOGERROR, "cannot build module '" + otherEntry.path().parent_path().string() + "' you must provide an outname for the .build module");
                        continue;
                    } else {
                        if (!opts.build.empty()) {
                            string ruleName = otherEntry.path().parent_path().stem().string();
                            file << "rule " << ruleName << "\n";
                            file << "   command = " << opts.build << " $in -o $out\n\n";

                            vector<string> buildExtSources;
                            string entry;
                            for (const auto& buildExtEntry : fs::recursive_directory_iterator(otherEntry.path().parent_path())) {
                                if (buildExtEntry.path().stem().string() == otherEntry.path().parent_path().stem().string() + ProjType) {
                                    entry = buildExtEntry.path().string();
                                    continue;
                                } else {
                                    if (buildExtEntry.path().extension() == ProjType) {
                                        buildExtSources.push_back(buildExtEntry.path().string());
                                    }
                                }
                            }

                            if (opts.outfolder.empty()) {
                                file << "build $target/" << opts.outname << ": " << ruleName << " " << entry << " ";
                                if (!buildExtSources.empty()) {
                                    for (const auto& src : buildExtSources) {
                                        file << " " << src;
                                    }
                                    file << "\n";
                                }
                            } else {
                                file << "build $target/" << opts.outfolder << "/" << opts.outname << ": " << ruleName << " " << entry;
                                if (!buildExtSources.empty()) {
                                    for (const auto& src : buildExtSources) {
                                        file << " " << src;
                                    }
                                    file << "\n";
                                }
                            }

                            donottouch.push_back(otherEntry.path().parent_path().stem().string());

                            // file << "build " << string(TargetDir) + "/" + opts.outfolder + "" << "\n";
                            // cout <<  << endl;
                        }
                    }
                }
            }
        } else {
            auto it = find(donottouch.begin(), donottouch.end(), entry.path().parent_path().stem().string());
            if (entry.path().extension() == ProjType && it == donottouch.end()) {
                sources.push_back(entry.path().string());
            }
        }
    }

    vector<string> objfiles;
    for (const auto& src : sources) {
        string stem = fs::path(src).stem().string() + ".o";
        objfiles.push_back(stem);
        file << "build $objdir/" << stem << ": cxx " << src << "\n";
    }

    file << "\nbuild $target/" << ExeFileName << ": link";
    for (const auto& obj: objfiles) {
        file << " $objdir/" << obj;
    }
    file << "\n";

    // file << "build $objdir/test.o: cxx src/test.cpp\n";
    // file << "build $objdir/main.o: cxx src/main.cpp\n";
    // file << "build $target/" << ExeFileName << ": link $objdir/main.o $objdir/test.o " << "\n";

    file.close();
    log.SendMessage(LOGINFO, "ninja build script generation finished outputted to -> " + ninjaFile);

    log.SendMessage(LOGINFO, "generating compile_commands.json");
    Task ninjaCompileCmds = {{"ninja", "-t", "compdb", "cxx", "cc"}};
    ninjaCompileCmds.run(log, true);
    ofstream coc("compile_commands.json");
    coc << ninjaCompileCmds.output;
    coc.close();
    log.SendMessage(LOGINFO, "compile_commands.json generated");
    // log.SendMessage(LOGINFO, "output of task --> '" + ninjaCompileCmds.output + "'");
}

inline void WatchFunc(int argc, char** argv, Logger log) {
    GenerateFunc(argc, argv, log);
    Task ninja = {{"ninja"}};
    ninja.run(log);

    vector<string> validExt = WatchDefaultExts;
    map<string, fs::file_time_type> fileMod;
    map<string, int> missingCount;
    const int removalThreshold = 3;

    auto populateFiles = [&]() -> vector<string> {
        vector<string> files;
        for (const auto& entry : fs::recursive_directory_iterator(SrcDir)) {
            for (const auto& ext : validExt) {
                if (entry.path().extension() == ext) {
                    string filePath = entry.path().string();
                    files.push_back(filePath);
                    if (fileMod.find(filePath) == fileMod.end()) {
                        try {
                            fileMod[filePath] = fs::last_write_time(entry.path());
                        } catch (const fs::filesystem_error& e) {
                            log.SendMessage(LOGWARNING, "error retrieving last write time for " + filePath + ": " + e.what());
                        }
                    }
                    break;
                }
            }
        }
        return files;
    };

    vector<string> files = populateFiles();
    log.SendMessage(LOGINFO, "starting to watch over '" + string(SrcDir) + "'");

    for (;;) {
        vector<string> newFiles = populateFiles();

        for (const auto& newFile : newFiles) {
            if (fileMod.find(newFile) == fileMod.end()) {
                log.SendMessage(LOGINFO, "new file added: '" + newFile + "'");
                try {
                    fileMod[newFile] = fs::last_write_time(newFile);
                } catch (const fs::filesystem_error& e) {
                    log.SendMessage(LOGWARNING, "error retrieving last write time for new file " + newFile + ": " + e.what());
                }
                missingCount[newFile] = 0;
                GenerateFunc(argc, argv, log);
                Task ninja = {{"ninja"}};
                ninja.run(log);
            }
        }

        for (auto it = fileMod.begin(); it != fileMod.end(); ) {
            if (find(newFiles.begin(), newFiles.end(), it->first) == newFiles.end()) {
                missingCount[it->first]++;
                if (missingCount[it->first] >= removalThreshold) {
                    log.SendMessage(LOGINFO, "file removed: '" + it->first + "'");
                    missingCount.erase(it->first);
                    it = fileMod.erase(it);
                    GenerateFunc(argc, argv, log);
                    Task ninja = {{"ninja"}};
                    ninja.run(log);
                } else {
                    ++it;
                }
            } else {
                missingCount[it->first] = 0;
                ++it;
            }
        }

        for (const auto& entry : fileMod) {
            try {
                auto currentModTime = fs::last_write_time(entry.first);
                if (currentModTime != entry.second) {
                    log.SendMessage(LOGINFO, "file modified: '" + entry.first + "' regenerating ninja script");
                    fileMod[entry.first] = currentModTime;
                    GenerateFunc(argc, argv, log);
                    Task ninja = {{"ninja"}};
                    ninja.run(log);
                }
            } catch (const fs::filesystem_error& e) {
                log.SendMessage(LOGWARNING, "failed to get last_write_time for " + entry.first + ": " + e.what());
            }
        }

        this_thread::sleep_for(chrono::milliseconds(500));
    }
}

inline void CleanFunc(int argc, char** argv, Logger log) {
    log.SendMessage(LOGINFO, "cleaning target directory -> '" + string(TargetDir) + "'") ;
    try {
        if (fs::exists(TargetDir) && fs::is_directory(TargetDir)) {
            for (const auto& entry : fs::directory_iterator(TargetDir)) {
                if (fs::is_directory(entry.path())) {
                    fs::remove_all(entry.path());
                } else {
                    fs::remove(entry.path());
                }
            }
            fs::remove_all(TargetDir);
            fs::remove("compile_commands.json");
            fs::remove("build.ninja");
            fs::remove(".ninja_log");
            log.SendMessage(LOGINFO, "successfully cleaned target directory");
        } else {
            log.SendMessage(LOGERROR, "target directory either doesn't exist or is a file");
        }
    } catch (const fs::filesystem_error& e) {
        log.SendMessage(LOGERROR, "error while cleaning target directory: " + string(e.what()));
    }
}

inline void GoRebuildYourself(int argc, char** argv, Logger log) {
    if (argc < 1 && !argv[0]) {
        log.SendMessage(LOGERROR, "invalid binary path");
    }

    const char* binaryPath = argv[0]; 
    const char* sourcePath = __FILE__;

    struct stat binaryStat, sourceStat;

    if (stat(binaryPath, &binaryStat) != 0) {
        log.SendMessage(LOGERROR, "failed to stat binary '" + string(binaryPath) + "'");
        return;
    }

    if (stat(sourcePath, &sourceStat) != 0) {
        log.SendMessage(LOGERROR, "failed to stat source '" + string(sourcePath) + "'");
        return;
    }

    if (sourceStat.st_mtime > binaryStat.st_mtime) {
        // log.SendMessage(LOGINFO, "the binary is outdated it needs rebuilding");
        string oldBinaryPath = string(binaryPath) + ".old";

        fs::rename(binaryPath, oldBinaryPath);

        Task rebuild = {{REBUILD_YOURSELF(binaryPath, sourcePath)}};
        rebuild.run(log);

        fs::remove(oldBinaryPath);

        vector<string> args(argv, argv + argc);
        Task run = {args};
        int res = run.run(log); 
        if (res < 1) {
            exit(69);
        } else {
            exit(0);
        }
    } else {
        // log.SendMessage(LOGINFO, "the binary is up to date");
    }
}
#endif // DEVBUILD_HPP
