#pragma once
#include <linux/capability.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <unistd.h>

// Root CI children must observe ordinary file read permissions, just like an
// installed desktop service. Never changes the parent, accounts or ownership.
inline bool ordinaryReadPermissions()
{
    if (geteuid() != 0) return true;
    __user_cap_header_struct header{_LINUX_CAPABILITY_VERSION_3, 0};
    __user_cap_data_struct data[2]{};
    if (syscall(SYS_capget, &header, data)) return false;
    if (prctl(PR_CAPBSET_DROP, CAP_DAC_OVERRIDE) || prctl(PR_CAPBSET_DROP, CAP_DAC_READ_SEARCH)) return false;
    const auto mask = ~((1U << CAP_DAC_OVERRIDE) | (1U << CAP_DAC_READ_SEARCH));
    data[0].effective &= mask; data[0].permitted &= mask; data[0].inheritable &= mask;
    return syscall(SYS_capset, &header, data) == 0;
}
