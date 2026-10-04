#include "libbot/session.hpp"

#include "libbot/command.hpp"
#include "libbot/project.hpp"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <format>
#include <fstream>
#include <map>
#include <sstream>

namespace bot {
namespace {

constexpr auto sessionImage = "bot-session:latest";
constexpr auto proxyImage = "bot-proxy:latest";
constexpr auto egressNetwork = "bot-egress";
constexpr auto stampLength = std::size_t{15};

auto container(std::string const& id) -> std::string { return "bot-" + id; }
auto proxy(std::string const& id) -> std::string { return "bot-" + id + "-proxy"; }

// YYYYMMDD-HHMMSS. An id is the project and this, so when a session started
// is read from its name rather than written down anywhere.
auto stamp() -> std::string
{
    auto const now = std::time(nullptr);
    auto local = std::tm{};
    ::localtime_r(&now, &local);
    auto text = std::string(stampLength + 1, '\0');
    text.resize(std::strftime(text.data(), text.size(), "%Y%m%d-%H%M%S", &local));
    return text;
}

auto trimmed(std::string text) -> std::string
{
    text.erase(text.find_last_not_of(" \t\r\n") + 1);
    return text;
}

auto lines(std::string const& text) -> std::vector<std::string>
{
    auto stream = std::istringstream{text};
    auto result = std::vector<std::string>{};
    for (auto line = std::string{}; std::getline(stream, line);) {
        if (!line.empty()) {
            result.push_back(line);
        }
    }
    return result;
}

auto runtime(Config const& config, std::vector<std::string> arguments) -> std::expected<std::string, std::string>
{
    arguments.insert(arguments.begin(), config.runtime);
    return run_succeeds(arguments);
}

auto runtime_says_yes(Config const& config, std::vector<std::string> arguments) -> bool
{
    arguments.insert(arguments.begin(), config.runtime);
    auto const result = run(arguments);
    return result && result->status == 0;
}

auto ensure_image(Config const& config, std::string const& image, std::string const& definition)
    -> std::expected<void, std::string>
{
    if (runtime_says_yes(config, {"image", "exists", image})) {
        return {};
    }
    auto const built = run_visible({config.runtime, "build", "-t", image, (config.images / definition).string()});
    if (!built) {
        return std::unexpected(built.error());
    }
    if (*built != 0) {
        return std::unexpected(std::format("building {} failed", image));
    }
    return {};
}

// The proxy is the only way out of a session's network, and this file is the
// whole of what it will connect to: the allowed names, anchored, and nothing
// that is an address.
auto write_proxy_files(Config const& config, std::filesystem::path const& directory) -> std::expected<void, std::string>
{
    auto settings = std::ofstream{directory / "tinyproxy.conf"};
    settings << "Port 8888\nTimeout 600\nMaxClients 64\nConnectPort 443\n"
                "FilterDefaultDeny Yes\nFilter \"/etc/bot-proxy/filter\"\n";

    auto filter = std::ofstream{directory / "filter"};
    for (auto const& name : config.allow) {
        filter << '^';
        for (auto const character : name) {
            filter << (character == '.' ? "\\." : std::string{character});
        }
        filter << "$\n";
    }
    if (!settings || !filter) {
        return std::unexpected(std::format("cannot write the proxy's files in {}", directory.string()));
    }
    return {};
}

// Everything a session is given is on this one command line. Its own network,
// its own clone, its own home; no capability, no way to gain one, a root
// filesystem it cannot write. `bot check` starts its probe through here too,
// so what is proven is what runs.
auto session_arguments(Config const& config, std::string const& id, std::string const& proxyAddress, bool const terminal,
                       std::vector<std::string> const& command) -> std::vector<std::string>
{
    auto const directory = session_directory(config, id);
    auto const proxyUrl = std::format("http://{}:8888", proxyAddress);
    auto arguments = std::vector<std::string>{
        "run", "--detach", "--name", container(id),
        "--network=" + container(id),
        "--userns=keep-id", "--read-only", "--cap-drop=all", "--security-opt=no-new-privileges",
        "--pids-limit=1024", "--memory=8g",
        "--log-driver=k8s-file", "--log-opt=path=" + (directory / "log").string(),
        "--volume", (directory / "tree").string() + ":/work:Z",
        "--volume", (directory / "home").string() + ":/home/agent:Z",
        "--workdir", "/work",
        "--env", "HOME=/home/agent",
        "--env", "HTTPS_PROXY=" + proxyUrl, "--env", "HTTP_PROXY=" + proxyUrl,
        "--env", "https_proxy=" + proxyUrl, "--env", "http_proxy=" + proxyUrl,
    };
    if (terminal) {
        arguments.insert(arguments.end(), {"--interactive", "--tty"});
    }
    if (!config.agentEnv.empty()) {
        arguments.insert(arguments.end(), {"--env-file", config.agentEnv.string()});
    }
    arguments.push_back(sessionImage);
    arguments.insert(arguments.end(), command.begin(), command.end());
    return arguments;
}

auto start(Config const& config, std::string const& id, bool const terminal, std::vector<std::string> const& command)
    -> std::expected<void, std::string>
{
    auto const directory = session_directory(config, id);
    auto error = std::error_code{};
    for (auto const* part : {"tree", "home", "proxy"}) {
        std::filesystem::create_directories(directory / part, error);
    }
    if (error) {
        return std::unexpected(std::format("cannot create {}: {}", directory.string(), error.message()));
    }
    if (auto written = write_proxy_files(config, directory / "proxy"); !written) {
        return written;
    }
    if (auto built = ensure_image(config, proxyImage, "proxy"); !built) {
        return built;
    }
    if (auto built = ensure_image(config, sessionImage, "session"); !built) {
        return built;
    }

    // Two networks. The session's is internal, with no gateway and no
    // resolver, and holds the session and its proxy alone. The proxy is also
    // on a second one that leads out.
    if (!runtime_says_yes(config, {"network", "exists", egressNetwork})) {
        if (auto made = runtime(config, {"network", "create", egressNetwork}); !made) {
            return std::unexpected(made.error());
        }
    }
    if (auto made = runtime(config, {"network", "create", "--internal", "--disable-dns", container(id)}); !made) {
        return std::unexpected(made.error());
    }
    if (auto ran = runtime(config, {"run", "--detach", "--name", proxy(id),
                                    "--network=" + container(id) + "," + egressNetwork,
                                    "--read-only", "--cap-drop=all", "--security-opt=no-new-privileges",
                                    "--volume", (directory / "proxy").string() + ":/etc/bot-proxy:ro,Z",
                                    proxyImage, "tinyproxy", "-d", "-c", "/etc/bot-proxy/tinyproxy.conf"});
        !ran) {
        return std::unexpected(ran.error());
    }

    // The session has no resolver, so it is told the proxy's address, not its name.
    auto const address = runtime(config, {"inspect", "--format",
                                          "{{(index .NetworkSettings.Networks \"" + container(id) + "\").IPAddress}}",
                                          proxy(id)});
    if (!address || trimmed(*address).empty()) {
        return std::unexpected("the proxy has no address on the session's network");
    }
    if (auto ran = runtime(config, session_arguments(config, id, trimmed(*address), terminal, command)); !ran) {
        return std::unexpected(ran.error());
    }
    return {};
}

auto discard(Config const& config, std::string const& id) -> std::expected<void, std::string>
{
    (void)run({config.runtime, "rm", "--force", container(id)});
    (void)run({config.runtime, "rm", "--force", proxy(id)});
    (void)run({config.runtime, "network", "rm", container(id)});
    auto error = std::error_code{};
    std::filesystem::remove_all(session_directory(config, id), error);
    if (error) {
        return std::unexpected(std::format("cannot remove {}: {}", session_directory(config, id).string(), error.message()));
    }
    return {};
}

// The session never holds a credential for the remote and has no route to it.
// What it committed is fetched out of its clone into a repository it never
// saw, and pushed from there. Git is not run inside the session's tree: that
// tree's configuration and hooks were writable by the session.
auto publish(Config const& config, std::string const& id, std::string const& project)
    -> std::expected<std::string, std::string>
{
    auto const directory = session_directory(config, id);
    auto const repository = directory / "tree" / ".git";
    if (!std::filesystem::is_directory(repository) || std::filesystem::is_symlink(repository)) {
        return std::unexpected(std::format("{} is not the clone this session was given; nothing published", repository.string()));
    }

    auto const outbox = (directory / "outbox.git").string();
    auto const branch = "refs/heads/" + id;
    auto ignored = std::error_code{};
    std::filesystem::remove_all(outbox, ignored);
    if (auto made = run_succeeds({"git", "init", "-q", "--bare", outbox}); !made) {
        return std::unexpected(made.error());
    }
    if (auto fetched = run_succeeds({"git", "--git-dir", outbox, "-c", "transfer.fsckObjects=true", "fetch", "-q",
                                     "file://" + (directory / "tree").string(), "HEAD:" + branch});
        !fetched) {
        return std::unexpected(fetched.error());
    }

    auto const url = config.remote_url(project);
    auto const tip = run_succeeds({"git", "--git-dir", outbox, "rev-parse", branch});
    auto const main = run_succeeds({"git", "ls-remote", url, "refs/heads/main"});
    if (tip && main && main->starts_with(trimmed(*tip))) {
        return std::string{"nothing committed beyond main; nothing published"};
    }
    if (auto pushed = run_succeeds({"git", "--git-dir", outbox, "push", "-q", url, "+" + branch + ":" + branch}); !pushed) {
        return std::unexpected(pushed.error());
    }
    return std::format("published branch {} to {}", id, url);
}

auto quoted(std::string const& text) -> std::string
{
    auto result = std::string{"'"};
    for (auto const character : text) {
        result += character == '\'' ? std::string{"'\\''"} : std::string{character};
    }
    return result + "'";
}

auto is_address(std::string_view candidate) -> bool
{
    return !candidate.empty() && candidate.find_first_not_of("0123456789abcdefABCDEF.:") == std::string_view::npos;
}

}

auto session_directory(Config const& config, std::string const& id) -> std::filesystem::path
{
    return config.sessions / id;
}

auto is_session(Config const& config, std::string const& id) -> bool
{
    return id.size() > stampLength + 1 && id[id.size() - stampLength - 1] == '-' && is_valid_name(id)
        && std::filesystem::is_directory(session_directory(config, id));
}

auto sessions(Config const& config) -> std::vector<Session>
{
    auto states = std::map<std::string, std::string>{};
    if (auto const listed = runtime(config, {"ps", "--all", "--format", "{{.Names}} {{.State}}"}); listed) {
        for (auto const& line : lines(*listed)) {
            auto const space = line.find(' ');
            states[line.substr(0, space)] = space == std::string::npos ? std::string{} : line.substr(space + 1);
        }
    }

    auto result = std::vector<Session>{};
    auto error = std::error_code{};
    for (auto const& entry : std::filesystem::directory_iterator{config.sessions, error}) {
        auto const id = entry.path().filename().string();
        if (!is_session(config, id)) {
            continue;
        }
        auto const at = id.substr(id.size() - stampLength);
        auto const state = states.find(container(id));
        result.push_back(Session{
            id,
            id.substr(0, id.size() - stampLength - 1),
            std::format("{}-{}-{} {}:{}", at.substr(0, 4), at.substr(4, 2), at.substr(6, 2), at.substr(9, 2), at.substr(11, 2)),
            state != states.end() ? state->second : "gone",
        });
    }
    std::ranges::sort(result, {}, &Session::id);
    return result;
}

auto up(Config const& config, std::string const& project) -> std::expected<std::string, std::string>
{
    if (!is_valid_name(project)) {
        return std::unexpected(std::format("'{}' is not a usable project name", project));
    }
    if (config.agent.empty()) {
        return std::unexpected("'agent' is not set in the config: bot carries the choice of agent, it does not make it");
    }

    auto const id = project + "-" + stamp();
    auto const directory = session_directory(config, id);
    auto const tree = directory / "tree";
    auto error = std::error_code{};
    if (!std::filesystem::create_directories(directory, error)) {
        return std::unexpected(std::format("cannot create {}", directory.string()));
    }

    // A fresh clone on its own branch, cut off from where it came from. The
    // commits are made as whoever this machine's git says its owner is.
    auto prepared = run_succeeds({"git", "clone", "-q", config.remote_url(project), tree.string()});
    for (auto const& argv : std::initializer_list<std::vector<std::string>>{
             {"git", "switch", "-q", "-c", id},
             {"git", "remote", "remove", "origin"},
         }) {
        prepared = prepared ? run_succeeds(argv, tree) : prepared;
    }
    for (auto const* key : {"user.name", "user.email"}) {
        if (auto const value = run_succeeds({"git", "config", "--global", "--get", key}); prepared && value) {
            prepared = run_succeeds({"git", "config", key, trimmed(*value)}, tree);
        }
    }

    // What the agent should find in its home on a first start, such as its
    // settings. Copied, never mounted: a session cannot write back to it.
    if (prepared && !config.agentHome.empty()) {
        std::filesystem::copy(config.agentHome, directory / "home", std::filesystem::copy_options::recursive, error);
        if (error) {
            prepared = std::unexpected(std::format("cannot copy {}: {}", config.agentHome.string(), error.message()));
        }
    }

    auto const started = prepared ? start(config, id, true, config.agent)
                                  : std::expected<void, std::string>{std::unexpected(prepared.error())};
    if (!started) {
        (void)discard(config, id);
        return std::unexpected(started.error());
    }
    return id;
}

auto attach_command(Config const& config, std::string const& id) -> std::vector<std::string>
{
    return {config.runtime, "attach", container(id)};
}

auto stop(Config const& config, std::string const& id) -> std::expected<std::string, std::string>
{
    if (!is_session(config, id)) {
        return std::unexpected(std::format("no session named {}", id));
    }
    (void)run({config.runtime, "stop", container(id)});
    (void)run({config.runtime, "rm", "--force", proxy(id)});
    return publish(config, id, id.substr(0, id.size() - stampLength - 1));
}

auto remove(Config const& config, std::string const& id) -> std::expected<std::string, std::string>
{
    auto published = stop(config, id);
    if (!published) {
        return published;
    }
    if (auto gone = discard(config, id); !gone) {
        return std::unexpected(gone.error());
    }
    return published;
}

auto check(Config const& config, std::vector<std::string> const& addresses, std::ostream& report) -> bool
{
    auto held = true;
    auto const say = [&](bool const good, std::string const& what) {
        report << (good ? "ok    " : "FAIL  ") << what << std::endl;
        held = held && good;
    };

    // The addresses that must be out of reach: every one this machine has,
    // which covers its tailnet and local-network faces, and any others given.
    auto unreachable = addresses;
    if (auto const own = run_succeeds({"hostname", "-I"}); own) {
        auto stream = std::istringstream{*own};
        for (auto address = std::string{}; stream >> address;) {
            unreachable.push_back(address);
        }
    }
    if (!std::ranges::all_of(unreachable, is_address)) {
        report << "FAIL  an address to probe is not an address" << std::endl;
        return false;
    }
    say(!unreachable.empty(), "this machine has addresses to probe");

    auto const rootless = runtime(config, {"info", "--format", "{{.Host.Security.Rootless}}"});
    say(rootless && trimmed(*rootless) == "true", "the container runtime is rootless");

    auto const id = "check-" + stamp();
    auto const sentinel = config.sessions / (".sentinel-" + id);
    auto error = std::error_code{};
    std::filesystem::create_directories(session_directory(config, id), error);
    std::ofstream{sentinel} << "a session must not be able to read this\n";

    if (auto const started = start(config, id, false, {"sleep", "600"}); !started) {
        say(false, "a session starts: " + started.error());
        (void)discard(config, id);
        std::filesystem::remove(sentinel, error);
        return false;
    }

    // What every session container on this machine was actually given, the
    // probe included, as the runtime reports it.
    if (auto const listed = runtime(config, {"ps", "--all", "--format", "{{.Names}}"}); listed) {
        for (auto const& name : lines(*listed)) {
            if (!name.starts_with("bot-")) {
                continue;
            }
            auto owner = name.substr(4);
            if (owner.ends_with("-proxy")) {
                owner.erase(owner.size() - 6);
            }
            auto const prefix = session_directory(config, owner).string() + "/";
            auto const mounts = runtime(config, {"inspect", "--format", "{{range .Mounts}}{{.Source}}{{\"\\n\"}}{{end}}", name});
            // A source that is not a path is scratch memory, not something of the host's.
            say(mounts && std::ranges::all_of(lines(*mounts), [&](std::string const& source) {
                    return !source.starts_with('/') || source.starts_with(prefix);
                }),
                name + " mounts nothing outside its own directory");
        }
    } else {
        say(false, "the runtime lists its containers");
    }

    // Each script exits zero when the boundary held.
    auto const inside = [&](std::string const& script) {
        return runtime_says_yes(config, {"exec", container(id), "sh", "-c", script});
    };
    auto const hidden = [&](std::filesystem::path const& path) { return inside("! test -e " + quoted(path.string())); };
    auto const relay = [&](std::string const& host, std::string_view const expected) {
        return inside(std::format("test \"$(curl -s -o /dev/null -m 10 -w '%{{http_connect}}' https://{}/)\" = {}", host, expected));
    };

    say(inside("test \"$(id -u)\" != 0"), "a session is not root");
    say(inside("grep -Eq '^CapEff:[[:space:]]*0+$' /proc/self/status"), "a session holds no capabilities");
    say(inside("grep -Eq '^[^ ]+ / [^ ]+ ro[, ]' /proc/mounts"), "a session's root filesystem is read-only");
    say(hidden(sentinel), "a session cannot see the directory that holds the other sessions");
    say(hidden(default_config_path()), "a session cannot see this tool's configuration");
    if (!config.agentEnv.empty()) {
        say(hidden(config.agentEnv), "a session cannot see the file its agent's credential came from");
    }
    if (auto const* home = std::getenv("HOME"); home != nullptr) {
        say(inside("! ls -A " + quoted(home) + " 2>/dev/null | grep -q ."), "a session cannot see the owner's home directory");
    }
    say(inside("! ls /run/podman /run/docker.sock /var/run/docker.sock /run/user/*/podman 2>/dev/null | grep -q ."),
        "a session cannot see a container runtime's socket");

    say(inside("! grep -Eq '^[^[:space:]]+[[:space:]]+00000000[[:space:]]' /proc/net/route"), "a session has no default route");
    say(inside("! timeout 8 getent hosts example.org"), "a session cannot resolve a name itself");
    for (auto const& address : unreachable) {
        say(inside(std::format("out=$(timeout 4 bash -c ': </dev/tcp/{}/22' 2>&1); test $? -ne 0 && ! printf %s \"$out\" | grep -qi refused",
                               address)),
            "a session cannot connect to " + address);
        say(relay(address.contains(':') ? "[" + address + "]" : address, "403"), "the proxy refuses to relay to " + address);
    }
    if (!std::ranges::contains(config.allow, "example.org")) {
        say(relay("example.org", "403"), "the proxy refuses a name that is not allowed");
    }
    for (auto const& name : config.allow) {
        say(relay(name, "200"), "the proxy relays to " + name + ", which is allowed");
    }

    say(discard(config, id).has_value(), "the probe session is removed");
    std::filesystem::remove(sentinel, error);
    return held;
}

}
