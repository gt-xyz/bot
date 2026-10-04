#include "libbot/command.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <format>

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace bot {
namespace {

auto to_c_argv(std::vector<std::string> const& argv) -> std::vector<char*>
{
    auto raw = std::vector<char*>{};
    raw.reserve(argv.size() + 1);
    for (auto const& argument : argv) {
        raw.push_back(const_cast<char*>(argument.c_str()));
    }
    raw.push_back(nullptr);
    return raw;
}

auto drain(int descriptor) -> std::string
{
    auto output = std::string{};
    auto buffer = std::array<char, 4096>{};
    while (true) {
        auto const count = ::read(descriptor, buffer.data(), buffer.size());
        if (count > 0) {
            output.append(buffer.data(), static_cast<std::size_t>(count));
        } else if (count == 0 || errno != EINTR) {
            return output;
        }
    }
}

}

auto run(std::vector<std::string> const& argv,
         std::filesystem::path const& directory) -> std::expected<CommandResult, std::string>
{
    if (argv.empty()) {
        return std::unexpected("no command given");
    }

    auto pipeEnds = std::array<int, 2>{};
    if (::pipe(pipeEnds.data()) != 0) {
        return std::unexpected(std::format("pipe: {}", std::strerror(errno)));
    }

    auto actions = posix_spawn_file_actions_t{};
    posix_spawn_file_actions_init(&actions);
    if (!directory.empty()) {
        posix_spawn_file_actions_addchdir_np(&actions, directory.c_str());
    }
    posix_spawn_file_actions_addclose(&actions, pipeEnds[0]);
    posix_spawn_file_actions_adddup2(&actions, pipeEnds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipeEnds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipeEnds[1]);

    auto raw = to_c_argv(argv);
    auto child = pid_t{};
    auto const spawned = ::posix_spawnp(&child, raw[0], &actions, nullptr, raw.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(pipeEnds[1]);

    if (spawned != 0) {
        ::close(pipeEnds[0]);
        return std::unexpected(std::format("{}: {}", argv[0], std::strerror(spawned)));
    }

    // Drain before reaping: a child writing more than the pipe buffer blocks
    // forever if we wait first.
    auto output = drain(pipeEnds[0]);
    ::close(pipeEnds[0]);

    auto waitStatus = int{};
    while (::waitpid(child, &waitStatus, 0) < 0 && errno == EINTR) {
    }

    auto const status = WIFEXITED(waitStatus) ? WEXITSTATUS(waitStatus) : 128 + WTERMSIG(waitStatus);
    return CommandResult{status, std::move(output)};
}

auto run_visible(std::vector<std::string> const& argv) -> std::expected<int, std::string>
{
    auto raw = to_c_argv(argv);
    auto child = pid_t{};
    if (auto const spawned = ::posix_spawnp(&child, raw[0], nullptr, nullptr, raw.data(), environ); spawned != 0) {
        return std::unexpected(std::format("{}: {}", argv[0], std::strerror(spawned)));
    }
    auto waitStatus = int{};
    while (::waitpid(child, &waitStatus, 0) < 0 && errno == EINTR) {
    }
    return WIFEXITED(waitStatus) ? WEXITSTATUS(waitStatus) : 128 + WTERMSIG(waitStatus);
}

auto replace(std::vector<std::string> const& argv) -> std::string
{
    auto raw = to_c_argv(argv);
    ::execvp(raw[0], raw.data());
    return std::format("{}: {}", argv[0], std::strerror(errno));
}

auto run_succeeds(std::vector<std::string> const& argv,
                  std::filesystem::path const& directory) -> std::expected<std::string, std::string>
{
    auto result = run(argv, directory);
    if (!result) {
        return std::unexpected(result.error());
    }
    if (result->status != 0) {
        return std::unexpected(std::format("{} exited {}: {}", argv[0], result->status, result->output));
    }
    return std::move(result->output);
}

}
