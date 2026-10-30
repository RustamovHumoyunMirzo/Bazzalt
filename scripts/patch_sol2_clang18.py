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

def PatchOptionalReferences(source):
    path=Path(source)/'include/sol/optional_implementation.hpp';text=path.read_text(encoding='utf-8')
    start=text.index('class optional<T&>');prefix=text[:start];body=text[start:]
    old="""			*this = nullopt;
			this->construct(std::forward<Args>(args)...);"""
    new="""			// Reference optionals rebind; they never construct a referent.
			*this = optional(std::forward<Args>(args)...);
			return **this;"""
    if new in body and old not in body:return False
    if body.count(old)!=1:raise ValueError('Pinned Sol2 reference optional context changed')
    path.write_text(prefix+body.replace(old,new),encoding='utf-8',newline='\n');return True

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('source',type=Path);args=parser.parse_args();Patch(args.source);PatchOptionalReferences(args.source)
