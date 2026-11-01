#pragma once
#include <windows.h>
#include <QProcess>
#include <vector>

// Closing the service's handle also terminates its helper processes after a
// service crash. No process is selected by executable name.
class OwnedProcessJob {
public:
    OwnedProcessJob()
    {
        m_handle = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (m_handle && !SetInformationJobObject(m_handle, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            CloseHandle(m_handle);
            m_handle = nullptr;
        }
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        m_attributes.resize(size);
        m_list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(m_attributes.data());
        if (!InitializeProcThreadAttributeList(m_list, 1, 0, &size)) {
            m_list = nullptr;
            return;
        }
        m_ready = m_handle && UpdateProcThreadAttribute(m_list, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
                                                       &m_handle, sizeof(m_handle), nullptr, nullptr);
    }
    ~OwnedProcessJob()
    {
        if (m_list) DeleteProcThreadAttributeList(m_list);
        if (m_handle) CloseHandle(m_handle);
    }
    OwnedProcessJob(const OwnedProcessJob &) = delete;
    OwnedProcessJob &operator=(const OwnedProcessJob &) = delete;
    bool ready() const { return m_ready; }
    void bind(QProcess &process)
    {
        process.setCreateProcessArgumentsModifier([this](QProcess::CreateProcessArguments *args) {
            m_startup.StartupInfo = *args->startupInfo;
            m_startup.StartupInfo.cb = sizeof(m_startup);
            m_startup.lpAttributeList = m_list;
            args->startupInfo = &m_startup.StartupInfo;
            args->flags |= EXTENDED_STARTUPINFO_PRESENT;
        });
    }
private:
    HANDLE m_handle = nullptr;
    std::vector<char> m_attributes;
    LPPROC_THREAD_ATTRIBUTE_LIST m_list = nullptr;
    STARTUPINFOEXW m_startup {};
    bool m_ready = false;
};
