#include "Bazzalt/Console.h"
#include "Runtime/ConsoleStore.h"
#include <cassert>
#include <thread>
using namespace Bazzalt;
int main(){
    assert(!Console::IsAvailable());Console::Info("ignored");Console::Clear();Console::ClearAt(0);assert(!Console::GetCount()&&!Console::GetMessages()&&!Console::GetMessageAt(0));
    Runtime::ConsoleAccess::Acquire();assert(Console::IsAvailable());assert(Console::GetCount().value()==0);
    Console::Info("first","Editor");Console::Warning("注意 warning","Physics",false);Console::Error("third","Physics");
    assert(Console::GetCount().value()==3);auto first=Console::GetMessageAt(0);assert(first&&first->Timestamp>0);
    ConsoleFilter filter;filter.Levels=static_cast<unsigned>(ConsoleLevel::Warning);filter.Source="Physics";filter.Contains="注意";
    assert(Console::GetCount(filter).value()==1);assert(!Console::GetMessages(filter)->front().ShowIcon);
    Console::Clear(filter);assert(Console::GetCount().value()==2);assert(Console::FindMessage(first->Id));
    Console::ClearAt(99);assert(!Console::GetMessageAt(99));Console::ClearAt(0);assert(!Console::FindMessage(first->Id));
    Console::Clear();std::thread worker([]{for(int i=0;i<100;++i)Console::Info("thread","Worker");});worker.join();assert(Console::GetCount().value()==100);
    auto snapshot=Runtime::ConsoleAccess::Snapshot(0);assert(snapshot&&snapshot->Messages.size()==100);assert(!Runtime::ConsoleAccess::Snapshot(snapshot->Revision));
    Console::ClearMessage(snapshot->Messages[0].Id);assert(Console::GetCount().value()==99);
    Console::Clear();for(int i=0;i<10001;++i)Console::Info("bounded");assert(Console::GetCount().value()==10000);
    Console::Clear();const std::string payload(65536,'x');for(int i=0;i<300;++i)Console::Info(payload);assert(Console::GetCount().value()<257);
    const auto retained=Console::GetCount().value();Console::Info(std::string(65537,'x'));assert(Console::GetCount().value()==retained);
    Runtime::ConsoleAccess::Release();assert(!Console::GetMessages());Console::Log("ignored");Runtime::ConsoleAccess::Acquire();assert(Console::GetCount().value()==0);Runtime::ConsoleAccess::Release();
}
