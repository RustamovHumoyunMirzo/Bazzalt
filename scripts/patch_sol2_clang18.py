"""Work around Clang 18's dependent-noexcept callback regression (Sol2 #1581)."""
import argparse
from pathlib import Path

OLD='static int call(lua_State* L) noexcept(std::is_nothrow_copy_assignable_v<T>) {'
NEW='''static int call(lua_State* L)
#if !defined(__clang__) || __clang_major__ != 18
            noexcept(std::is_nothrow_copy_assignable_v<T>)
#endif
        {'''

def Patch(source):
    path=Path(source)/'include/sol/function_types_stateless.hpp';text=path.read_text(encoding='utf-8')
    if text.count(NEW)==2 and OLD not in text:return False
    if text.count(OLD)!=2 or NEW in text:raise ValueError('Pinned Sol2 callback patch context changed; review upstream')
    path.write_text(text.replace(OLD,NEW),encoding='utf-8',newline='\n');return True

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('source',type=Path);args=parser.parse_args();Patch(args.source)
