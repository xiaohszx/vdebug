#include <Windows.h>
#include <DbgCtrl/DbgCtrl.h>
#include <DbgCtrl/DbgProtocol.h>
#include <mq/mq.h>
#include "ProcDbgProxy.h"
#include "ProcDbg.h"
#include "./DescParser/DescParser.h"
#include "./DescParser/DescPrinter.h"
#include "./DescParser/DescCache.h"

using namespace std;

ProcDbgProxy *ProcDbgProxy::GetInstance() {
    static ProcDbgProxy *s_ptr = NULL;
    if (s_ptr == NULL)
    {
        s_ptr = new ProcDbgProxy();
    }
    return s_ptr;
}

ProcDbgProxy::ProcDbgProxy() {
    m_init = false;
    m_pDbgClient = NULL;
    m_x64 = (sizeof((void *)0) == 8);
    m_pProcDbgger = NULL;
    m_hProcListener = NULL;
}

bool ProcDbgProxy::InitProcDbgProxy(const char *unique) {
    if (m_init)
    {
        return true;
    }

    m_init = true;
    m_pDbgClient = DbgClientBase::newInstance();
    if (m_x64) {
        m_pDbgClient->InitClient(em_dbg_proc64, unique);
    }
    else {
        m_pDbgClient->InitClient(em_dbg_proc86, unique);
    }
    m_pDbgClient->RegisterCtrlHandler(DBG_CTRL_EXEC, ExecProc, this);
    m_pDbgClient->RegisterCtrlHandler(DBG_CTRL_ATTACH, AttachProc, this);
    m_pDbgClient->RegisterCtrlHandler(DBG_CTRL_RUNCMD, RunCmd, this);
    m_pDbgClient->RegisterCtrlHandler(DBG_CTRL_GET_PROC, GetProcInfo, this);
    m_pDbgClient->RegisterCtrlHandler(DBG_CTRL_DETACH, DetachProc, this);
    m_pDbgClient->RegisterCtrlHandler(DBG_CTRL_BREAK, BreakDebugger, this);
    m_pDbgClient->RegisterCtrlHandler(DBG_CTRL_TEST_DESC, DescTest, this);
    m_pDbgClient->RegisterCtrlHandler(DBG_CTRL_INPUT_DESC, DescSave, this);
    m_pProcDbgger = CProcDbgger::GetInstance();
    m_pCmdRunner = CProcCmd::GetInst();
    m_pCmdRunner->InitProcCmd(m_pProcDbgger);
    return true;
}

ProcDbgProxy::~ProcDbgProxy() {
}

/*
{
    "cmd":"RunCmd",
        "content":{
            "cmd":"bp kernen32!CreateFileW"
    }
}
*/
CtrlReply ProcDbgProxy::RunCmd(const CtrlRequest &request, void *param) {
    mstring data = request.mContent["command"].asString();
    data.trim();

    CtrlReply reply;
    if (data.empty())
    {
        reply.mStatus = DBG_CMD_SYNTAX_ERR;
        reply.mShow = "命令语法错误";
        return reply;
    }

    return GetInstance()->m_pCmdRunner->RunCommand(data, HUserCtx());
}

/*
{
    "cmd":"exec",
    "content":{
        "path":"abcdef.exe",
        "param":"abcdefgh"
    }
}
*/
CtrlReply ProcDbgProxy::ExecProc(const CtrlRequest &request, void *param) {
    mstring path = request.mContent["path"].asString();
    mstring exeParam = request.mContent["param"].asString();

    DbgProcUserContext context;
    context.m_strCmd = exeParam;
    context.m_strPePath = path;
    BOOL ret = GetInstance()->m_pProcDbgger->Connect(path.c_str(), &context);

    CtrlReply reply;
    if (ret)
    {
        reply.mStatus = 0;
    } else {
        reply.mStatus = 1;
    }
    return reply;
}

CtrlReply ProcDbgProxy::AttachProc(const CtrlRequest &request, void *param) {
    DWORD pid = (DWORD)request.mContent["pid"].asInt();
    GetInstance()->m_pProcDbgger->Connect(pid);

    return CtrlReply();
}

CtrlReply ProcDbgProxy::DetachProc(const CtrlRequest &request, void *param) {
    if (GetInstance()->m_pProcDbgger->GetDbggerStatus() != em_dbg_status_init)
    {
        GetInstance()->m_pProcDbgger->DisConnect();
    }

    return CtrlReply();
}

CtrlReply ProcDbgProxy::BreakDebugger(const CtrlRequest &request, void *param) {
    if (GetInstance()->m_pProcDbgger->GetDbggerStatus() == em_dbg_status_busy)
    {
        return GetInstance()->m_pCmdRunner->RunCommand("bk", HUserCtx());
    } else {
    }
    return CtrlReply();
}


CtrlReply ProcDbgProxy::GetProcInfo(const CtrlRequest &request, void *param) {
    int start = GetIntFromJson(request.mContent, "start");
    if (1 == start)
    {
        if (GetInstance()->m_hProcListener != NULL)
        {
            ProcMonitor::GetInstance()->UnRegisterListener(GetInstance()->m_hProcListener);
            GetInstance()->m_hProcListener = NULL;
        }

        GetInstance()->m_hProcListener = ProcMonitor::GetInstance()->RegisterListener(GetInstance());
    } else {
        ProcMonitor::GetInstance()->UnRegisterListener(GetInstance()->m_hProcListener);
        GetInstance()->m_hProcListener = NULL;
    }
    return CtrlReply();
}

void ProcDbgProxy::OnProcChanged(HProcListener listener, const list<const ProcMonInfo *> &added, const list<DWORD> &killed) {
    ProcInfoSet procSet;
    for (list<const ProcMonInfo *>::const_iterator it = added.begin() ; it != added.end() ; it++)
    {
        procSet.mAddSet.push_back(**it);
    }
    procSet.mKillSet = killed;

    EventInfo eventInfo;
    eventInfo.mEvent = DBG_EVENT_PROC_CHANGED;
    Reader().parse(EncodeProcMon(procSet), eventInfo.mContent);
    m_pDbgClient->ReportDbgEvent(eventInfo);
}

CtrlReply ProcDbgProxy::DescTest(const CtrlRequest &request, void *param) {
    CtrlReply result;
    mstring dll = request.mContent["module"].asString();
    mstring str = request.mContent["descStr"].asString();

    list<StructDesc *> stSet;
    list<FunDesc *> funSet;

    if (!CDescParser::GetInst()->ParserModuleProc(dll, str, stSet, funSet))
    {
        result.mShow = FormatA("解析描述信息错误，%hs", CDescParser::GetInst()->GetErrorStr().c_str());
    } else {
        result.mShow = "解析数据类型成功\n";
        list<FunDesc *>::const_iterator ij;

        for (ij = funSet.begin() ; ij != funSet.end() ; ij++)
        {
            result.mShow += CDescPrinter::GetInst()->GetProcStrByDesc(*ij);
        }
    }

    result.mShow += "\n";
    for (list<StructDesc *>::const_iterator it = stSet.begin() ; it != stSet.end() ; it++)
    {
        if (CDescCache::GetInst()->IsStructInDb(*it))
        {
            StructDesc *p1 = *it;
            result.mShow += FormatA("调试器中已存在%hs结构的信息\n", p1->mTypeName.c_str());
        }
    }

    for (list<FunDesc *>::const_iterator ij = funSet.begin() ; ij != funSet.end() ; ij++)
    {
        if (CDescCache::GetInst()->IsFunctionInDb(*ij))
        {
            FunDesc *p2 = *ij;
            result.mShow += FormatA("调试器中已存在%hs模块%hs函数信息\n", p2->mDllName.c_str(), p2->mProcName.c_str());
        }
    }
    return result;
}

CtrlReply ProcDbgProxy::DescSave(const CtrlRequest &request, void *param) {
    CtrlReply result;
    mstring dll = request.mContent["module"].asString();
    mstring str = request.mContent["descStr"].asString();
    int cover = request.mContent["cover"].asInt();

    list<StructDesc *> stSet;
    list<FunDesc *> funSet;

    if (!CDescParser::GetInst()->ParserModuleProc(dll, str, stSet, funSet))
    {
        result.mShow = FormatA("解析描述信息错误，%hs", CDescParser::GetInst()->GetErrorStr().c_str());
    } else {
        result.mShow = "解析数据类型成功\n";
        list<StructDesc *>::const_iterator it;
        list<FunDesc *>::const_iterator ij;

        bool dataInDb = false;
        result.mShow += "\n";
        for (list<StructDesc *>::const_iterator it = stSet.begin() ; it != stSet.end() ; it++)
        {
            if (CDescCache::GetInst()->IsStructInDb(*it))
            {
                dataInDb = true;
                StructDesc *p1 = *it;
                result.mShow += FormatA("调试器中已存在%hs结构的信息\n", p1->mTypeName.c_str());
            }
        }

        for (list<FunDesc *>::const_iterator ij = funSet.begin() ; ij != funSet.end() ; ij++)
        {
            if (CDescCache::GetInst()->IsFunctionInDb(*ij))
            {
                dataInDb = true;
                FunDesc *p2 = *ij;
                result.mShow += FormatA("调试器中已存在%hs模块%hs函数信息\n", p2->mDllName.c_str(), p2->mProcName.c_str());
            }
        }

        if (dataInDb && cover)
        {
            result.mShow += "强制覆盖保存数据";
        } else if (dataInDb && !cover) {
            result.mShow += "数据库中已存在相关数据,本次保存失败";
            result.mResult["needCover"] = 1;
            return result;
        }

        for (it = stSet.begin() ; it != stSet.end() ; it++)
        {
            CDescCache::GetInst()->InsertStructToDb(*it);
        }

        for (ij = funSet.begin() ; ij != funSet.end() ; ij++)
        {
            CDescCache::GetInst()->InsertFunToDb(*ij);
            result.mShow += CDescPrinter::GetInst()->GetProcStrByDesc(*ij);
        }
    }
    return result;
}