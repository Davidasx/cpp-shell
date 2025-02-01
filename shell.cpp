// IO
#include <iostream>
#include <fcntl.h>
// std::string
#include <string>
// std::vector
#include <vector>
// std::string 转 int
#include <sstream>
// PATH_MAX 等常量
#include <climits>
// POSIX API
#include <unistd.h>
// wait
#include <sys/wait.h>
// array
#include <array>
// signal
#include <signal.h>

static bool command_running = false;
static std::vector<pid_t> background_processes; // 添加后台进程保存

std::string trim(const std::string& s);
std::vector<std::string> split(std::string s, const std::string& delimiter);
void execute_command(const std::string& cmd);

volatile sig_atomic_t fg_pid = 0;

void sigint_handler(int signo) {
    if (fg_pid > 0) {
        kill(fg_pid, SIGINT);
        fg_pid = 0;
    }
    if (!command_running) {
        std::cout << "\n$ ";
    }
    std::cout.flush();
}

int main() {
    // 设置SIGINT信号处理
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    // 不同步 iostream 和 cstdio 的 buffer
    std::ios::sync_with_stdio(false);

    // 用来存储读入的一行命令
    std::string cmd;

    while (true) {
        // 打印提示符
        std::cout << "$ ";

        // 读入一行。std::getline 结果不包含换行符。
        std::getline(std::cin, cmd);

        // 按空格分割命令为单词
        std::vector<std::string> args = split(cmd, " ");

        // 没有可处理的命令
        if (args.empty()) {
            continue;
        }

        // 退出
        if (args[0] == "exit") {
            if (args.size() <= 1) {
                return 0;
            }

            // std::string 转 int
            std::stringstream code_stream(args[1]);
            int code = 0;
            code_stream >> code;

            // 转换失败
            if (!code_stream.eof() || code_stream.fail()) {
                std::cout << "Invalid exit code\n";
                continue;
            }

            return code;
        }

        if (args[0] == "pwd") {
            command_running = true;
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd)) != nullptr) {
                std::cout << cwd << std::endl;
            }
            else {
                std::cout << "获取当前目录失败" << std::endl;
            }
            command_running = false;
            continue;
        }

        if (args[0] == "cd") {
            command_running = true;
            if (args.size() < 2) {
                // 没有参数时切换到HOME目录
                const char* home = getenv("HOME");
                if (home == nullptr || chdir(home) != 0) {
                    std::cout << "Failed to change directory to " << std::string(home) << std::endl;
                }
            }
            else {
                if (chdir(args[1].c_str()) != 0) {
                    std::cout << "Failed to change directory to " << args[1] << std::endl;
                }
            }
            command_running = false;
            continue;
        }

        if (args[0] == "wait") {
            command_running = true;
            // 设置 SIGINT 为忽略
            struct sigaction sigIntAction, oldSigIntAction;
            sigIntAction.sa_handler = SIG_IGN;
            sigemptyset(&sigIntAction.sa_mask);
            sigIntAction.sa_flags = 0;
            sigaction(SIGINT, &sigIntAction, &oldSigIntAction);

            // 等待所有后台进程结束
            for (pid_t pid : background_processes) {
                std::cerr << "WAITING FOR PID: " << pid << std::endl;
                waitpid(pid, nullptr, 0);
            }
            background_processes.clear();

            // 恢复原来的 SIGINT 处理器
            sigaction(SIGINT, &oldSigIntAction, nullptr);
            command_running = false;
            continue;
        }

        if (args[0] == "fg") {
            command_running = true;
            pid_t pid;
            if (args.size() < 2) {
                // 使用最近的后台进程
                if (background_processes.empty()) {
                    std::cout << "fg: no current job" << std::endl;
                    command_running = false;
                    continue;
                }
                pid = background_processes.back();
            }
            else {
                // std::string 转 int
                std::stringstream pid_stream(args[1]);
                pid = 0;
                pid_stream >> pid;

                // 转换失败
                if (!pid_stream.eof() || pid_stream.fail()) {
                    std::cout << "Invalid PID" << std::endl;
                    command_running = false;
                    continue;
                }
            }

            // 检查PID是否在后台进程列表中
            bool pid_exists = false;
            for (int i = 0; i < (int)(background_processes.size()); i++) {
                if (background_processes[i] == pid) {
                    pid_exists = true;
                    background_processes.erase(background_processes.begin() + i);
                    break;
                }
            }

            if (!pid_exists) {
                std::cout << "fg: job not found" << std::endl;
                command_running = false;
                continue;
            }

            // 设置前台进程
            fg_pid = pid;

            // 等待进程结束
            waitpid(pid, nullptr, 0);

            fg_pid = 0;
            continue;
        }

        if (args[0] == "bg") {
            command_running = true;
            pid_t pid;
            if (args.size() < 2) {
                // 使用最近的后台进程
                if (background_processes.empty()) {
                    std::cout << "bg: no current job" << std::endl;
                    command_running = false;
                    continue;
                }
                pid = background_processes.back();
            }
            else {
                // std::string 转 int
                std::stringstream pid_stream(args[1]);
                pid = 0;
                pid_stream >> pid;

                // 转换失败
                if (!pid_stream.eof() || pid_stream.fail()) {
                    std::cout << "Invalid PID" << std::endl;
                    command_running = false;
                    continue;
                }
            }

            // 检查PID是否在后台进程列表中
            bool pid_exists = false;
            for (int i = 0; i < (int)(background_processes.size()); i++) {
                if (background_processes[i] == pid) {
                    pid_exists = true;
                    break;
                }
            }

            if (!pid_exists) {
                std::cout << "bg: job not found" << std::endl;
                command_running = false;
                continue;
            }

            // 向进程发送 SIGCONT 信号
            if (kill(pid, SIGCONT) == -1) {
                perror("kill");
            }

            std::cout << "[" << pid << "] Running in background" << std::endl;
            command_running = false;
            continue;
        }
        execute_command(cmd);
    }
}

// 移除字符串首尾的空白字符
std::string trim(const std::string& str) {
    const std::string whitespace = " \t\n\r\f\v";
    const auto start = str.find_first_not_of(whitespace);
    if (start == std::string::npos) return "";

    const auto end = str.find_last_not_of(whitespace);
    return str.substr(start, end - start + 1);
}

// 改进的 split 函数，自动处理空白字符
std::vector<std::string> split(std::string s, const std::string& delimiter) {
    std::vector<std::string> res;
    // 先处理输入字符串
    s = trim(s);

    size_t pos = 0;
    std::string token;
    while ((pos = s.find(delimiter)) != std::string::npos) {
        token = s.substr(0, pos);
        // 对每个子串进行 trim
        token = trim(token);
        if (!token.empty()) {  // 忽略空字符串
            res.push_back(token);
        }
        s = s.substr(pos + delimiter.length());
    }
    // 处理最后一个子串
    s = trim(s);
    if (!s.empty()) {
        res.push_back(s);
    }
    return res;
}


// 执行带有管道的命令
void execute_command(const std::string& cmd) {
    command_running = true;

    // 检查是否为后台运行命令
    bool background = false;
    std::string command = cmd;
    if (command.back() == '&') {
        background = true;
        command = trim(command.substr(0, command.length() - 1)); // 移除末尾的 &
    }

    // 分割管道中的各个命令
    std::vector<std::string> commands;
    size_t start = 0;
    size_t pos;
    while ((pos = command.find('|', start)) != std::string::npos) {
        std::string subcommand = command.substr(start, pos - start);
        commands.push_back(subcommand);
        start = pos + 1;
    }
    commands.push_back(command.substr(start));

    int n = commands.size();

    // 使用 std::array<int,2> 来存储每个管道的两个文件描述符
    std::vector<std::array<int, 2>> pipes(n - 1);
    std::vector<pid_t> pids(n);

    // 创建所需的管道
    for (int i = 0; i < n - 1; i++) {
        if (pipe(pipes[i].data()) == -1) {
            std::cerr << "管道创建失败" << std::endl;
            return;
        }
    }

    pid_t bg_pgid = 0;
    // 为每个命令创建子进程
    for (int i = 0; i < n; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            std::cerr << "fork 调用失败" << std::endl;
            return;
        }
        if (pids[i] == 0) {  // 子进程
            // 如果是后台任务，则设置进程组
            if (background) {
                if (i == 0) {
                    // 第一个子进程创建新的进程组
                    setpgid(0, 0);
                }
                else {
                    // 其他子进程加入第一个子进程所在的进程组
                    setpgid(0, bg_pgid);
                }
            }
            // 如果不是第一个命令，重定向标准输入到前一个管道的读端
            if (i > 0) {
                close(pipes[i - 1][1]);
                dup2(pipes[i - 1][0], STDIN_FILENO);
                close(pipes[i - 1][0]);
            }
            else {
                // 检查重定向
                int redirect_pos = commands[i].find('<');
                if (redirect_pos != std::string::npos) {
                    std::string input_source = trim(commands[i].substr(redirect_pos + 1));
                    commands[i] = trim(commands[i].substr(0, redirect_pos)); // 移除重定向部分

                    // 打开输入文件
                    int fd = open(input_source.c_str(), O_RDONLY);
                    if (fd == -1) {
                        perror("Error opening input file");
                        exit(1);
                    }

                    // 重定向标准输入
                    if (dup2(fd, STDIN_FILENO) == -1) {
                        perror("Error redirecting input");
                        exit(1);
                    }
                    close(fd);
                }
            }
            // 如果不是最后一个命令，重定向标准输出到当前管道的写端
            if (i < n - 1) {
                close(pipes[i][0]);
                dup2(pipes[i][1], STDOUT_FILENO);
                close(pipes[i][1]);
            }
            else {
                // 检查重定向
                int redirect_pos = commands[i].find('>');
                if (redirect_pos != std::string::npos) {
                    bool output_append = false;
                    std::string output_file;

                    // 检查是否为追加模式 >>
                    if (redirect_pos != (int)(commands[i].size()) - 1 && commands[i][redirect_pos + 1] == '>') {
                        output_append = true;
                        output_file = trim(commands[i].substr(redirect_pos + 2));
                    }
                    else {
                        output_file = trim(commands[i].substr(redirect_pos + 1));
                    }

                    // 移除命令中的重定向部分
                    commands[i] = trim(commands[i].substr(0, redirect_pos));

                    // 打开输出文件
                    int fd;
                    if (output_append) {
                        fd = open(output_file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
                    }
                    else {
                        fd = open(output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    }

                    if (fd < 0) {
                        perror("open failed");
                        exit(1);
                    }

                    // 重定向标准输出
                    if (dup2(fd, STDOUT_FILENO) < 0) {
                        perror("dup2 failed");
                        exit(1);
                    }
                    close(fd);
                }
            }
            // 关闭所有管道描述符，避免描述符泄漏
            for (auto& p : pipes) {
                close(p[0]);
                close(p[1]);
            }
            // 分割命令字符串为命令及其参数
            std::vector<std::string> args = split(commands[i], " ");
            if (args.empty()) {
                continue;
            }

            // 构造 execvp 需要的参数数组
            std::vector<char*> argv;
            for (auto& arg : args) {
                argv.push_back(&arg[0]);
            }
            argv.push_back(nullptr);

            // 执行命令
            execvp(argv[0], argv.data());
            // 如果 execvp 返回，则说明执行失败
            perror("");
            exit(1);
        }
        else {
            // 父进程中记录后台进程组的组长 PID
            if (background && i == 0) {
                bg_pgid = pids[i];
            }
        }
    }

    // 父进程关闭所有管道
    for (auto& p : pipes) {
        close(p[0]);
        close(p[1]);
    }

    if (!background) {
        // 前台运行：等待所有子进程结束
        for (int i = 0; i < n; i++) {
            waitpid(pids[i], nullptr, 0);
        }
    }
    else {
        // 后台运行：不等待子进程，直接返回
        std::cout << "[" << pids[0] << "] Running in background" << std::endl;
        background_processes.push_back(pids[0]); // 记录后台进程组组长 PID
    }

    command_running = false;
}

