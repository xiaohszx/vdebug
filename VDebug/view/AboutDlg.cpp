#include "AboutDlg.h"
#include "../../ComLib/ComUtil.h"
#include "resource.h"
#include <CommCtrl.h>

using namespace std;

CAboutDlg::CAboutDlg() {
}

CAboutDlg::~CAboutDlg() {
}

INT_PTR CAboutDlg::OnInitDialog(WPARAM wp, LPARAM lp) {
    CentreWindow(mHwnd, NULL);

    extern HINSTANCE g_hInstance;
    HICON ico = LoadIconA(g_hInstance, MAKEINTRESOURCEA(IDI_MAIN));
    SendMessageA(GetDlgItem(mHwnd, IDC_ABOUT_ICO), STM_SETICON, (WPARAM)ico, 0);

    HWND hEditVersion = GetDlgItem(mHwnd, IDC_ABOUT_EDT_VERSION);

    char buff[512];
    char version[128];
    GetModuleFileName(NULL, buff, sizeof(buff));
    GetPeVersionA(buff, version, sizeof(version));

    SetWindowTextA(hEditVersion, FormatA("vdebug windows系统开源调试器 版本:%hs", version).c_str());

    mBmp1 = (HBITMAP)LoadBitmapA(g_hInstance, MAKEINTRESOURCEA(IDB_BITMAP1));
    SendMessageA(GetDlgItem(mHwnd, IDC_STATIC1), STM_SETIMAGE, (WPARAM)IMAGE_BITMAP, (LPARAM)mBmp1);

    mBmp2 = (HBITMAP)LoadBitmapA(g_hInstance, MAKEINTRESOURCEA(IDB_BITMAP2));
    SendMessageA(GetDlgItem(mHwnd, IDC_STATIC2), STM_SETIMAGE, (WPARAM)IMAGE_BITMAP, (LPARAM)mBmp2);
    SetFocus(GetDlgItem(mHwnd, IDC_ABOUT_ICO));

    mLinkVdebug = GetDlgItem(mHwnd, IDC_LINK_VDEBUG);
    mLinkHomePage = GetDlgItem(mHwnd, IDC_LINK_HOMEPAGE);

    string desc = 
        "vdebug是一个windows平台的调试器，主要特点(部分正在开发)如下：\r\n"
        "1.绿色，小巧，不依赖额外的库\r\n"
        "2.支持32/64位程序调试\r\n"
        "3.支持分析dump内存影像文件\r\n"
        "4.内置简洁易用的调试脚本,可以方便的写调试脚本\r\n"
        "5.可以自行导入C语言格式的函数类型解析函数参数\r\n"
        "6.漂亮的语法高亮控件\r\n"
        "\r\n"
        "该调试器中用到的库有:\r\n"
        "Scintilla 强大开源的语法高亮控件\r\n"
        "capstone 反汇编引擎\r\n"
        "sqlite3 文件数据库\r\n"
        "jsoncpp json解析库\r\n"
        "TitanEngine 调试脚本引擎\r\n"
        "deelx 正则表达式引擎\r\n"
        "感谢这些开源组件的作者!";

    SetWindowTextA(GetDlgItem(mHwnd, IDC_ABOUT_EDT_DESC), desc.c_str());
    return 0;
}

INT_PTR CAboutDlg::MessageProc(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG)
    {
        OnInitDialog(wp, lp);
    } else if (msg == WM_COMMAND)
    {
    } else if (msg == WM_NOTIFY)
    {
        NMHDR *ptr = (NMHDR *)lp;

        if (ptr->code == NM_CLICK)
        {
            PNMLINK pNMLink = (PNMLINK)lp;

            if (pNMLink->hdr.hwndFrom == mLinkVdebug)
            {
                ShellExecuteA(mHwnd, "open", "https://gitee.com/lougd/vdebug", NULL, NULL, SW_SHOWNORMAL);
            } else if (pNMLink->hdr.hwndFrom == mLinkHomePage)
            {
                ShellExecuteA(mHwnd, "open", "https://gitee.com/lougd", NULL, NULL, SW_SHOWNORMAL);
            }
        }
    }
    else if (msg == WM_CLOSE)
    {
        DeleteObject((HGDIOBJ)mBmp1);
        DeleteObject((HGDIOBJ)mBmp2);
        EndDialog(mHwnd, 0);
    }
    return 0;
}