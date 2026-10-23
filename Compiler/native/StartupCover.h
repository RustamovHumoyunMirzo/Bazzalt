#pragma once
#include "game_logo.h"
#include <algorithm>

namespace Bazzalt::Game::Startup {
inline HWND Cover=nullptr;
inline bool ShowBrand=true,AllowSkip=false,Skip=false,Ready=false;
inline int LogoSize=144;
inline void Position(HWND owner){
    if(!Cover||!IsWindow(Cover))return;RECT bounds{};GetClientRect(owner,&bounds);POINT origin{};ClientToScreen(owner,&origin);
    SetWindowPos(Cover,HWND_TOP,origin.x,origin.y,bounds.right,bounds.bottom,SWP_NOACTIVATE);InvalidateRect(Cover,nullptr,FALSE);
}
inline LRESULT CALLBACK Procedure(HWND window,UINT message,WPARAM w,LPARAM l){
    switch(message){
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_LBUTTONDOWN:if(AllowSkip)Skip=true;return 0;
    case WM_ERASEBKGND:{RECT bounds{};GetClientRect(window,&bounds);FillRect(reinterpret_cast<HDC>(w),&bounds,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));return 1;}
    case WM_PAINT:{
        PAINTSTRUCT paint;HDC dc=BeginPaint(window,&paint);RECT bounds{};GetClientRect(window,&bounds);FillRect(dc,&bounds,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        if(ShowBrand){
            const int height=std::min(LogoSize,std::max(32,(int(bounds.bottom)-80)/2)),width=height*GameLogoWidth/GameLogoHeight;
            const int top=(bounds.bottom-height-48)/2;
            BITMAPINFO bitmap{};bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bitmap.bmiHeader.biWidth=GameLogoWidth;bitmap.bmiHeader.biHeight=-GameLogoHeight;bitmap.bmiHeader.biPlanes=1;bitmap.bmiHeader.biBitCount=32;bitmap.bmiHeader.biCompression=BI_RGB;
            SetStretchBltMode(dc,HALFTONE);StretchDIBits(dc,(bounds.right-width)/2,top,width,height,0,0,GameLogoWidth,GameLogoHeight,GameLogoPixels,&bitmap,DIB_RGB_COLORS,SRCCOPY);
            HFONT font=CreateFontW(-20,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");auto previous=SelectObject(dc,font);
            SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));RECT text{8,top+height+20,bounds.right-8,top+height+52};DrawTextW(dc,L"Powered by Bazzalt",-1,&text,DT_CENTER|DT_SINGLELINE|DT_VCENTER);SelectObject(dc,previous);DeleteObject(font);
        }
        EndPaint(window,&paint);return 0;
    }
    case WM_DESTROY:if(Cover==window)Cover=nullptr;return 0;
    }
    return DefWindowProcW(window,message,w,l);
}
inline void Create(HWND owner,HINSTANCE instance,bool brand,int size,bool skip,bool visible){
    ShowBrand=brand;LogoSize=size;AllowSkip=skip;Skip=false;Ready=false;
    WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=Procedure;cls.lpszClassName=L"BazzaltStartupCover";cls.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    Require(RegisterClassW(&cls)!=0,"Could not register startup cover");
    Cover=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,cls.lpszClassName,L"",WS_POPUP,0,0,1,1,owner,nullptr,instance,nullptr);Require(Cover!=nullptr,"Could not create startup cover");Position(owner);
    if(visible){ShowWindow(Cover,SW_SHOWNOACTIVATE);UpdateWindow(Cover);}
}
inline void Finish(){Ready=true;if(Cover&&IsWindow(Cover))DestroyWindow(Cover);Cover=nullptr;}
}
