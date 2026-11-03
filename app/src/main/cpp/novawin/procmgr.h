// NovaWin — gestão de processos
#pragma once
#include <sys/types.h>
#include <string>

namespace nw {
std::string procsList();
int procKill(pid_t pid);
int procSetAffinity(pid_t pid, int mask);
int procCpuCount();
} // namespace nw
