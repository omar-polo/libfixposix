/*******************************************************************************/
/* Permission is hereby granted, free of charge, to any person or organization */
/* obtaining a copy of the software and accompanying documentation covered by  */
/* this license (the "Software") to use, reproduce, display, distribute,       */
/* execute, and transmit the Software, and to prepare derivative works of the  */
/* Software, and to permit third-parties to whom the Software is furnished to  */
/* do so, all subject to the following:                                        */
/*                                                                             */
/* The copyright notices in the Software and this entire statement, including  */
/* the above license grant, this restriction and the following disclaimer,     */
/* must be included in all copies of the Software, in whole or in part, and    */
/* all derivative works of the Software, unless such copies or derivative      */
/* works are solely in the form of machine-executable object code generated by */
/* a source language processor.                                                */
/*                                                                             */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR  */
/* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,    */
/* FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT   */
/* SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE   */
/* FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE, */
/* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER */
/* DEALINGS IN THE SOFTWARE.                                                   */
/*******************************************************************************/

#include <lfp/spawn.h>
#include <lfp/stdlib.h>
#include <lfp/errno.h>
#include <lfp/unistd.h>
#include <lfp/wait.h>

#include "spawn.h"

typedef int (*execfun)(const char*, char *const[], char *const[]);

static void
child_exit(int pipefd, int child_errno)
{
    int noctets = write(pipefd, &child_errno, sizeof(int));
    if (noctets == sizeof(int))
        _exit(255);
    else
        _exit(254);
}

static int
_lfp_spawn_apply_default_attributes(const lfp_spawnattr_t *attr)
{
    if (attr == NULL || ! (attr->flags & LFP_SPAWN_SETSIGMASK)) {
        sigset_t set;
        sigemptyset(&set);
        SYSGUARD(sigprocmask(SIG_SETMASK, &set, NULL));
    }
    return 0;
}

static void
handle_child(execfun execfn,
             const char *path,
             char *const argv[],
             char *const envp[],
             const lfp_spawn_file_actions_t *file_actions,
             const lfp_spawnattr_t *attr,
             int pipes[2])
{
    int child_errno;
    if ((child_errno = _lfp_spawn_apply_default_attributes(attr))  != 0 || \
        (child_errno = lfp_spawn_apply_attributes(attr))           != 0 || \
        (child_errno = lfp_spawn_apply_file_actions(file_actions)) != 0) {
        child_exit(pipes[1], child_errno);
    }
    execfn(path, argv, envp);
    child_exit(pipes[1], lfp_errno());
}

static int
handle_parent(pid_t child_pid,
              int pipes[2])
{
    close(pipes[1]);
    int status, child_errno;
    int noctets = read(pipes[0], &child_errno, sizeof(int));
    int read_errno = lfp_errno();
    close(pipes[0]);
    switch (noctets) {
    case -1:
        SYSERR(read_errno);
    case 0:
        return 0;
    case 4:
        waitpid(child_pid, &status, WNOHANG);
        SYSERR(child_errno);
    default:
        // This is not suppose to happen because all 4 octets
        // of the child's errno should get here with one write
        SYSERR(EBUG);
    }
}

static int
_lfp_spawn(execfun execfn,
           pid_t *restrict pid,
           const char *restrict path,
           char *const argv[restrict],
           char *const envp[restrict],
           const lfp_spawn_file_actions_t *restrict file_actions,
           const lfp_spawnattr_t *restrict attr)
{
    int pipes[2];

    // Used for passing the error code from child to parent in case
    // some of the syscalls executed in the child fail
    if (lfp_pipe(pipes, O_CLOEXEC) < 0)
        return -1;

    *pid = fork();

    switch (*pid) {
    case -1:
        return -1;
    case 0:
        handle_child(execfn, path, argv, envp, file_actions, attr, pipes);
        // Flow reaches this point only if child_exit() mysteriously fails
        SYSERR(EBUG);
    default:
        return handle_parent(*pid, pipes);
    }
}

DSO_PUBLIC int
lfp_spawn(pid_t *restrict pid,
          const char *restrict path,
          char *const argv[restrict],
          char *const envp[restrict],
          const lfp_spawn_file_actions_t *restrict file_actions,
          const lfp_spawnattr_t *restrict attr)
{
    SYSCHECK(EINVAL, pid == NULL);

    return _lfp_spawn(&lfp_execve, pid, path, argv, envp, file_actions, attr);
}

DSO_PUBLIC int
lfp_spawnp(pid_t *restrict pid,
           const char *restrict file,
           char *const argv[restrict],
           char *const envp[restrict],
           const lfp_spawn_file_actions_t *restrict file_actions,
           const lfp_spawnattr_t *restrict attr)
{
    SYSCHECK(EINVAL, pid == NULL);

    return _lfp_spawn(&lfp_execvpe, pid, file, argv, envp, file_actions, attr);
}
