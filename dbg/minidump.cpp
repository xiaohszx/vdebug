#include <Windows.h>
#include <DbgHelp.h>
#include <time.h>
#include "minidump.h"
#include "symbol.h"
#include "DbgCommon.h"

CMiniDumpHlpr *CMiniDumpHlpr::GetInst() {
    static CMiniDumpHlpr *s_ptr = NULL;

    if (s_ptr == NULL)
    {
        s_ptr = new CMiniDumpHlpr();
    }
    return s_ptr;
}

CMiniDumpHlpr::CMiniDumpHlpr()
{}

CMiniDumpHlpr::~CMiniDumpHlpr()
{}

bool CMiniDumpHlpr::WriteDump(const mstring &strFilePath) const
{
    return true;
}

bool CMiniDumpHlpr::ResetStat() {
    CSymbolHlpr::GetInst()->UnloadAll();

    m_vModuleSet.clear();
    for (vector<DumpThreadInfo>::iterator it = m_vThreadSet.begin() ; it != m_vThreadSet.end() ; it++)
    {
        delete it->m_context.mFullContext;
    }
    m_vThreadSet.clear();
    m_vMemory.clear();
    mException = DumpException();
    mSystemInfo = DumpSystemInfo();
    mCurThread = DumpThreadInfo();
    return true;
}

bool CMiniDumpHlpr::LodeDump(const mstring &strFilePath)
{
    m_pDumpFile = MappingFileA(strFilePath.c_str(), FALSE, 0);

    if (!m_pDumpFile || !m_pDumpFile->lpView)
    {
        return false;
    }

    ResetStat();
    bool stat = true;
    stat &= LoadSystemInfo();
    stat &= LoadMemorySet();
    stat &= LoadModuleSet();
    stat &= LoadThreadSet();
    LoadException();
    LoadAllSymbol();
    return stat;
}

bool CMiniDumpHlpr::LoadSystemInfo() {
    DWORD dwStreamSize = 0;
    LPVOID pStream = GetSpecStream(SystemInfoStream, dwStreamSize);

    if (!pStream)
    {
        return false;
    }
    MINIDUMP_SYSTEM_INFO *pSysStream = (MINIDUMP_SYSTEM_INFO *)pStream;

    PMINIDUMP_STRING pWstr = (PMINIDUMP_STRING)((const char *)m_pDumpFile->lpView + pSysStream->CSDVersionRva);
    mSystemInfo.m_strSpStr.append(WtoA(ustring(pWstr->Buffer, pWstr->Length / sizeof(WCHAR))));
    mSystemInfo.m_dwBuildNum = pSysStream->BuildNumber;
    mSystemInfo.m_dwMajVer = pSysStream->MajorVersion;
    mSystemInfo.m_dwMinVer = pSysStream->MinorVersion;
    mSystemInfo.mProductType = pSysStream->ProductType;

    if (PROCESSOR_ARCHITECTURE_AMD64 == pSysStream->ProcessorArchitecture)
    {
        mSystemInfo.m_eCpuType = em_processor_amd64;
    } else if (PROCESSOR_ARCHITECTURE_INTEL == pSysStream->ProcessorArchitecture)
    {
        mSystemInfo.m_eCpuType = em_processor_x86;
    }
    return true;
}

mstring CMiniDumpHlpr::GetVersionStr(const VS_FIXEDFILEINFO &version) const {
    DWORD dwVerMS = version.dwProductVersionMS;  
    DWORD dwVerLS = version.dwProductVersionLS;
    return FormatA("%d.%d.%d.%d", (dwVerMS >> 16), (dwVerMS & 0xFFFF), (dwVerLS >> 16), (dwVerLS & 0xFFFF));
}

bool CMiniDumpHlpr::LoadModuleSet() {
    DWORD dwStreamSize = 0;
    LPVOID pStream = GetSpecStream(ModuleListStream, dwStreamSize);

    if (!pStream)
    {
        return false;
    }

    m_vModuleSet.clear();
    PMINIDUMP_MODULE_LIST pModuleList = (PMINIDUMP_MODULE_LIST)pStream;
    for (int i = 0 ; i < (int)pModuleList->NumberOfModules ; i++)
    {
        DumpModuleInfo info;
        PMINIDUMP_MODULE pModule = pModuleList->Modules + i;
        info.m_dwBaseAddr = pModule->BaseOfImage;
        info.m_dwModuleSize = pModule->SizeOfImage;
        PMINIDUMP_STRING pStr= (PMINIDUMP_STRING)((const char *)m_pDumpFile->lpView + pModule->ModuleNameRva);

        info.m_strModulePath.append(WtoA(ustring(pStr->Buffer, pStr->Length / sizeof(WCHAR))));
        info.m_dwTimeStamp = pModule->TimeDateStamp;
        info.m_strModuleName = PathFindFileNameA(info.m_strModulePath.c_str());
        info.mVersion = GetVersionStr(pModule->VersionInfo);
        info.m_dwTimeStamp = pModule->TimeDateStamp;

        time_t t2 = info.m_dwTimeStamp;
        tm m2 = {0};
        localtime_s(&m2, &t2);
        info.mTimeStr = FormatA(
            "%04d-%02d-%02d %02d:%02d:%02d",
            m2.tm_year + 1900,
            m2.tm_mon + 1,
            m2.tm_mday,
            m2.tm_hour,
            m2.tm_min,
            m2.tm_sec
            );
        info.mVersion = GetVersionStr(pModule->VersionInfo);
        m_vModuleSet.push_back(info);
    }
    return true;
}

bool CMiniDumpHlpr::LoadThreadSet() {
    DWORD dwStreamSize = 0;
    LPVOID pStream = GetSpecStream(ThreadListStream, dwStreamSize);

    if (!pStream)
    {
        return false;
    }

    LPVOID ptr = m_pDumpFile->lpView;
    int d = (int)ptr;

    int index = 0;
    PMINIDUMP_THREAD_LIST pThreadList = (PMINIDUMP_THREAD_LIST)pStream;
    for (int i = 0 ; i < (int)pThreadList->NumberOfThreads ; i++, index++)
    {
        MINIDUMP_THREAD *pThread = (pThreadList->Threads + i);
        CONTEXTx64 *pContext = (CONTEXTx64 *)((const char *)m_pDumpFile->lpView + pThread->ThreadContext.Rva);
        DumpMemoryInfo stack;
        stack.m_dwStartAddr = pThread->Stack.StartOfMemoryRange;
        stack.m_dwRva = pThread->Stack.Memory.Rva;
        stack.m_dwSize = pThread->Stack.Memory.DataSize;

        DumpThreadInfo info;
        info.mIndex = index;
        info.m_context.m_dwCbp = pContext->Rbp;
        info.m_context.m_dwCsi = pContext->Rsi;
        info.m_context.m_dwCsp = pContext->Rsp;
        info.m_context.m_dwCip = pContext->Rip;
        info.m_context.mFullContext = new CONTEXTx64();
        memcpy(info.m_context.mFullContext, pContext, sizeof(CONTEXTx64));
        info.m_stack = stack;
        info.m_dwThreadId = pThread->ThreadId;
        info.m_dwTeb = pThread->Teb;

        DumpModuleInfo module = GetModuleFromAddr(info.m_context.m_dwCip);
        info.mCipSymbol = CDbgCommon::GetSymFromAddr(info.m_context.m_dwCip, module.m_strModuleName, module.m_dwBaseAddr);
        m_vThreadSet.push_back(info);
    }
    mCurThread = *m_vThreadSet.begin();
    return true;
}

bool CMiniDumpHlpr::LoadMemorySet() {
    DWORD dwStreamSize = 0;
    LPVOID pStream = GetSpecStream(MemoryListStream , dwStreamSize);

    if (!pStream)
    {
        return false;
    }

    MINIDUMP_MEMORY_LIST *pMemList = (MINIDUMP_MEMORY_LIST *)pStream;
    for (int i = 0 ; i < (int)pMemList->NumberOfMemoryRanges ; i++)
    {
        DumpMemoryInfo info;
        MINIDUMP_MEMORY_DESCRIPTOR  *pMemDesc = pMemList->MemoryRanges + i;
        info.m_dwStartAddr = pMemDesc->StartOfMemoryRange;
        info.m_dwSize = pMemDesc->Memory.DataSize;
        info.m_dwRva = pMemDesc->Memory.Rva;
        m_vMemory.push_back(info);
    }
    return true;
}

bool CMiniDumpHlpr::LoadException() {
    DWORD dwStreamSize = 0;
    LPVOID pStream = GetSpecStream(ExceptionStream  , dwStreamSize);

    if (!pStream)
    {
        return false;
    }

    PMINIDUMP_EXCEPTION_STREAM pException = (PMINIDUMP_EXCEPTION_STREAM)pStream;

    mException.mThreadId = pException->ThreadId;
    mException.mExceptionCode = pException->ExceptionRecord.ExceptionCode;
    mException.mExceptionAddress = pException->ExceptionRecord.ExceptionAddress;
    mException.mExceptionFlags = pException->ExceptionRecord.ExceptionFlags;

    CONTEXTx64 *pContext = (CONTEXTx64 *)((const char *)m_pDumpFile->lpView + pException->ThreadContext.Rva);
    memcpy(&mException.mThreadContext, pContext, sizeof(CONTEXTx64));
    return true;
}

bool CMiniDumpHlpr::LoadSymbolFromImage(const mstring &image, DWORD64 baseAddr) const {
    CSymbolTaskHeader task;
    CTaskLoadSymbol loadInfo;
    loadInfo.m_dwBaseOfModule = baseAddr;
    loadInfo.m_hImgaeFile = CreateFileA(image.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    HandleAutoClose abc(loadInfo.m_hImgaeFile);

    if (loadInfo.m_hImgaeFile == NULL || INVALID_HANDLE_VALUE == loadInfo.m_hImgaeFile)
    {
        int dd = 123;
    }
    task.m_dwSize = sizeof(CSymbolTaskHeader) + sizeof(CTaskLoadSymbol);
    task.m_pParam = &loadInfo;
    task.m_eTaskType = em_task_loadsym;

    CSymbolHlpr::GetInst()->SendTask(&task);
    return (TRUE == task.m_bSucc);
}

bool CMiniDumpHlpr::LoadSymbol(DWORD64 baseAddr) {
    for (list<DumpModuleInfo>::iterator it = m_vModuleSet.begin() ; it != m_vModuleSet.end() ; it++)
    {
        if (it->m_dwBaseAddr == baseAddr)
        {
            DumpModuleInfo &module = *it;

            if (module.mLoadSymbol)
            {
                return true;
            }

            /*
            //在本地获取dump中的文件路径,可能会从微软服务器上下载,符号服务器上不存在将会导致加载符号失败
            CTaskFindFileForDump task;
            CSymbolTaskHeader header;
            header.m_dwSize = sizeof(CTaskFindFileForDump) + sizeof(CSymbolTaskHeader);
            header.m_eTaskType = em_task_findfile;

            task.m_SizeofImage = (PVOID)module.m_dwModuleSize;
            task.m_TimeStamp = (PVOID)module.m_dwTimeStamp;
            task.m_strModuleName = module.m_strModuleName;
            header.m_pParam = &task;

            if (CSymbolHlpr::GetInst()->SendTask(&header))
            {
                module.m_strModulePath = task.m_strFullPath;
                if (!module.m_strModulePath.empty())
                {
                    module.mLoadSymbol = TRUE;

                    //load symbol
                    LoadSymbolFromImage(module.m_strModulePath, module.m_dwBaseAddr);
                    loadSucc = true;
                }
            } else {
                module.mLoadFaild = TRUE;
            }
            */
            PVOID ptr = DisableWow64Red();
            it->mLoadSymbol = LoadSymbolFromImage(module.m_strModulePath, module.m_dwBaseAddr);
            RevertWow64Red(ptr);
            return (TRUE == it->mLoadSymbol);
        }
    }
    return false;
}

void CMiniDumpHlpr::LoadAllSymbol() {
    //此处效率极低，需要优化
    list<DWORD64> loadSet;
    for (list<DumpModuleInfo>::iterator it = m_vModuleSet.begin() ; it != m_vModuleSet.end() ; it++)
    {
        if (!it->mLoadSymbol && !it->mLoadFaild)
        {
            loadSet.push_back(it->m_dwBaseAddr);
        }
    }

    for (list<DWORD64>::const_iterator ij = loadSet.begin() ; ij != loadSet.end() ; ij++)
    {
        LoadSymbol(*ij);
    }
}

list<DumpModuleInfo> CMiniDumpHlpr::GetModuleSet() const
{
    return m_vModuleSet;
}

vector<DumpThreadInfo> CMiniDumpHlpr::GetThreadSet() const
{
    return m_vThreadSet;
}

DumpThreadInfo CMiniDumpHlpr::GetCurThread() const {
    return mCurThread;
}

DumpSystemInfo CMiniDumpHlpr::GetSystemInfo() const
{
    return mSystemInfo;
}

list<DumpMemoryInfo> CMiniDumpHlpr::GetMemoryInfo() const
{
    return m_vMemory;
}

DWORD64 CMiniDumpHlpr::GetModuelBaseFromAddr(HANDLE hProcess, DWORD64 dwAddr)
{
    for (list<DumpModuleInfo>::const_iterator it = GetInst()->m_vModuleSet.begin() ; it != GetInst()->m_vModuleSet.end() ; it++)
    {
        if (dwAddr >= it->m_dwBaseAddr && dwAddr <= (it->m_dwBaseAddr + it->m_dwModuleSize))
        {
            return it->m_dwBaseAddr;
        }
    }
    return 0;
}

DWORD64 CALLBACK StackTranslateAddressProc64(HANDLE hProcess, HANDLE hThread, LPADDRESS64 lpaddr)
{
    return 0;
}

BOOL CMiniDumpHlpr::ReadDumpMemoryProc64(HANDLE hProcess, DWORD64 lpBaseAddress, PVOID lpBuffer, DWORD nSize, LPDWORD lpNumberOfBytesRead)
{
    //将读取的内存地址转换为RVA地址并进行读取
    DumpMemoryInfo *pStack = (DumpMemoryInfo *)hProcess;
    GetInst()->m_vThreadSet;
    DWORD64 d = (DWORD64)pStack;

    for (list<DumpMemoryInfo>::const_iterator it = GetInst()->m_vMemory.begin() ; it != GetInst()->m_vMemory.end() ; it++)
    {
        if (lpBaseAddress >= it->m_dwStartAddr && lpBaseAddress <= it->m_dwStartAddr + it->m_dwSize)
        {
            DWORD64 dw =(it->m_dwRva + (DWORD64)GetInst()->m_pDumpFile->lpView);
            DWORD64 dwOffset = (lpBaseAddress - it->m_dwStartAddr);
            DWORD64 dwDataAddr = dw +dwOffset;
            memcpy(lpBuffer, (void *)dwDataAddr, nSize);
            lpNumberOfBytesRead[0] = nSize;
            return TRUE;
        }
    }

    //if (lpBaseAddress >= pStack->m_dwStartAddr && lpBaseAddress <= pStack->m_dwStartAddr + pStack->m_dwSize)
    //{
    //    return TRUE;
    //}
    return FALSE;
}

DumpModuleInfo CMiniDumpHlpr::GetModuleFromAddr(DWORD64 addr) const {
    for (list<DumpModuleInfo>::const_iterator it = m_vModuleSet.begin() ; it != m_vModuleSet.end() ; it++)
    {
        if (addr >= it->m_dwBaseAddr && addr <= (it->m_dwBaseAddr + it->m_dwModuleSize))
        {
            return *it;
        }
    }
    return DumpModuleInfo();
}

bool CMiniDumpHlpr::LoadSymbolByAddr(DWORD64 addr) {
    DumpModuleInfo module  = GetModuleFromAddr(addr);
    return LoadSymbol(module.m_dwBaseAddr);
}

DumpThreadInfo CMiniDumpHlpr::GetThreadByTid(int tid) const {
    for (vector<DumpThreadInfo>::const_iterator it = m_vThreadSet.begin() ; it != m_vThreadSet.end() ; it++)
    {
        if (tid == it->m_dwThreadId)
        {
            return *it;
        }
    }
    return DumpThreadInfo();
}

list<STACKFRAME64> CMiniDumpHlpr::GetStackFrame(int tid)
{
    LoadAllSymbol();
    DumpThreadInfo threadInfo = GetThreadByTid(tid);

    if (threadInfo.m_dwThreadId == 0)
    {
        return list<STACKFRAME64>();
    }

    STACKFRAME64 frame = {0};
    DWORD machineType = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = threadInfo.m_context.m_dwCip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = threadInfo.m_context.m_dwCbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = threadInfo.m_context.m_dwCsp;
    frame.AddrStack.Mode = AddrModeFlat;

    CTaskStackWalkInfo info;
    info.m_context = frame;
    info.m_dwMachineType = IMAGE_FILE_MACHINE_I386;
    info.m_pfnGetModuleBaseProc = GetModuelBaseFromAddr;
    info.m_pfnReadMemoryProc = ReadDumpMemoryProc64;

    CSymbolTaskHeader header;
    header.m_dwSize = sizeof(CTaskStackWalkInfo) + sizeof(CSymbolTaskHeader);
    header.m_eTaskType = em_task_stackwalk;
    header.m_pParam = &info;
    CSymbolHlpr::GetInst()->SendTask(&header);
    return info.m_FrameSet;
}

list<STACKFRAME64> CMiniDumpHlpr::GetCurrentStackFrame() {
    return GetStackFrame(mCurThread.m_dwThreadId);
}

DumpException CMiniDumpHlpr::GetException() {
    if (mException.mSymbol.empty())
    {
        DumpModuleInfo module = GetModuleFromAddr(mException.mExceptionAddress);
        mException.mSymbol = CDbgCommon::GetSymFromAddr(mException.mExceptionAddress, module.m_strModuleName, module.m_dwBaseAddr);
    }
    
    return mException;
}

bool CMiniDumpHlpr::SetCurThread(DWORD tid) {
    for (vector<DumpThreadInfo>::const_iterator it = m_vThreadSet.begin() ; it != m_vThreadSet.end() ; it++)
    {
        if (it->m_dwThreadId == tid)
        {
            mCurThread = *it;
            return true;
        }
    }
    return false;
}

LPVOID CMiniDumpHlpr::GetSpecStream(MINIDUMP_STREAM_TYPE eType, DWORD &dwStreamSize) const
{
    if (!m_pDumpFile || !m_pDumpFile->lpView)
    {
        return false;
    }

    MINIDUMP_DIRECTORY *pDumpDir = NULL;
    PVOID pStreamAddr = NULL;
    MiniDumpReadDumpStream(m_pDumpFile->lpView, eType, &pDumpDir, &pStreamAddr, &dwStreamSize);

    if (!dwStreamSize)
    {
        return NULL;
    }
    return pStreamAddr;
}
