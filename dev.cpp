#include <vector>
#include "dev.h"

int main(int argc, char** argv) {
    Logger log;
    GoRebuildYourself(argc, argv, log);
    Cli brick(log);
    brick.cmds.push_back({"gen", "generate a ninja build script", GenerateFunc});
    brick.cmds.push_back({"watch", "watch over files in src dir", WatchFunc});
    brick.cmds.push_back({"clean", "cleans the target directory", CleanFunc});
    brick.go(argc, argv);
    return 0;
}
