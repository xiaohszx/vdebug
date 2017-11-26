#include "BreakPoint.h"
#include "SyntaxDescHlpr.h"
#include "common.h"
#include "MainView.h"
#include "Command.h"
#include "ProcDbg.h"

CBreakPointMgr::CBreakPointMgr()
{}

CBreakPointMgr::~CBreakPointMgr()
{}

void CBreakPointMgr::Int3BpCallback()
{
    TITAN_ENGINE_CONTEXT_t context = GetCurrentDbgger()->GetCurrentContext();

    GetBreakPointMgr()->OnBreakPoint(context.cip);
    ((CProcDbgger *)GetCurrentDbgger())->Wait();
}

bool CBreakPointMgr::IsBpInCache(DWORD64 dwAddr) const
{
    for (vector<BreakPointInfo>::const_iterator it = m_vBreakPoints.begin() ; it != m_vBreakPoints.end() ; it++)
    {
        if (it->m_dwBpAddr == dwAddr)
        {
            return true;
        }
    }
    return false;
}

bool CBreakPointMgr::DeleteInCache(DWORD64 dwAddr)
{
    for (vector<BreakPointInfo>::iterator it = m_vBreakPoints.begin() ; it != m_vBreakPoints.end() ; it++)
    {
        if (it->m_dwBpAddr == dwAddr)
        {
            m_vBreakPoints.erase(it);
            return true;
        }
    }
    return false;
}

bool CBreakPointMgr::PushBreakPoint(const BreakPointInfo &info)
{
    m_vBreakPoints.push_back(info);
    return true;
}

BOOL CBreakPointMgr::SetBreakPoint(DWORD64 dwAddr, const CmdUserParam *pUserContxt)
{
    if (IsBpInCache(dwAddr))
    {
        return TRUE;
    }

    if (SetBPX((ULONG_PTR)dwAddr, UE_BREAKPOINT, Int3BpCallback))
    {
        BreakPointInfo point;
        point.m_dwBpAddr = dwAddr;
        point.m_wstrName = GetCurrentDbgger()->GetSymFromAddr(dwAddr);
        if (pUserContxt)
        {
            memcpy(&point.m_vUserContext, pUserContxt, sizeof(CmdUserContext));
        }
        PushBreakPoint(point);
        return TRUE;
    }
    return FALSE;
}

BOOL CBreakPointMgr::DeleteBp(DWORD64 dwAddr)
{
    if (!IsBpInCache(dwAddr))
    {
        return FALSE;
    }

    DeleteInCache(dwAddr);
    DeleteBPX((ULONG_PTR)dwAddr);
    return TRUE;
}

BOOL CBreakPointMgr::OnBreakPoint(DWORD64 dwAddr)
{
    for (vector<BreakPointInfo>::const_iterator it = m_vBreakPoints.begin() ; it != m_vBreakPoints.end() ; it++)
    {
        if (dwAddr == it->m_dwBpAddr)
        {
            CSyntaxDescHlpr hlpr;
            hlpr.FormatDesc(FormatW(L"�ϵ�%ls�Ѵ���", it->m_wstrName.c_str()), COLOUR_MSG);
            GetSyntaxView()->AppendSyntaxDesc(hlpr.GetResult());
            GetCurrentDbgger()->RunCommand("r");

            if (it->m_vUserContext.m_pfnCallback)
            {
                it->m_vUserContext.m_pfnCallback(it->m_vUserContext.m_pParam, NULL);
            }
            return TRUE;
        }
    }
    return TRUE;
}

CBreakPointMgr *GetBreakPointMgr()
{
    static CBreakPointMgr *s_ptr = NULL;

    if (!s_ptr)
    {
        s_ptr = new CBreakPointMgr;
    }
    return s_ptr;
}