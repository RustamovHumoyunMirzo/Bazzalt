#define BazzaltRunLinuxPlayer BazzaltRunMacImplementation
#include "PlayerLinux.cpp"
extern "C" __attribute__((visibility("default"))) int BazzaltRunMacPlayer(int argc,char** argv){@autoreleasepool{return BazzaltRunMacImplementation(argc,argv);}}
