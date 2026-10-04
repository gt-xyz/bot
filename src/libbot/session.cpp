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
    } else {
        // Messages arrive as files its owner writes. The session reads them and cannot write there.
        arguments.insert(arguments.end(), {"--volume", (directory / "inbox").string() + ":/inbox:ro,Z"});
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
    for (auto const* part : {"tree", "home", "proxy", terminal ? "proxy" : "inbox"}) {
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

auto teardown(Config const& config, std::string const& id) -> void
{
    (void)run({config.runtime, "rm", "--force", "--time", "0", container(id), proxy(id)});
    (void)run({config.runtime, "network", "rm", container(id)});
}

auto discard(Config const& config, std::string const& id) -> std::expected<void, std::string>
{
    teardown(config, id);
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

auto project_of(std::string const& id) -> std::string
{
    return id.substr(0, id.size() - stampLength - 1);
}

// A title is the start of the first message, cut where a character ends.
auto shortened(std::string const& text) -> std::string
{
    auto end = std::size_t{72};
    if (text.size() <= end) {
        return text;
    }
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
        --end;
    }
    return text.substr(0, end) + "…";
}

auto ago(std::string const& at) -> std::string
{
    auto const part = [&at](std::size_t from, std::size_t length) { return std::atoi(at.substr(from, length).c_str()); };
    auto when = std::tm{};
    when.tm_year = part(0, 4) - 1900;
    when.tm_mon = part(4, 2) - 1;
    when.tm_mday = part(6, 2);
    when.tm_hour = part(9, 2);
    when.tm_min = part(11, 2);
    when.tm_isdst = -1;
    auto const minutes = static_cast<long>(std::difftime(std::time(nullptr), std::mktime(&when)) / 60);
    return minutes < 1 ? "just now"
         : minutes < 90 ? std::format("{} min ago", minutes)
         : minutes < 36 * 60 ? std::format("{} h ago", minutes / 60)
                             : std::format("{} days ago", minutes / 1440);
}

// The first words a session hears when its owner gave it none.
constexpr auto nextWork = "Read AGENTS.md, then do the piece of work marked next. Commit what you do.";

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
        auto const state = states.find(container(id));
        auto const running = state != states.end() && state->second == "running";
        auto const conversation = takes_messages(config, id);
        auto first = std::string{};
        std::getline(std::ifstream{entry.path() / "inbox" / "0001"}, first);
        result.push_back(Session{
            id,
            project_of(id),
            conversation ? shortened(first) : "In a terminal",
            running ? (!conversation ? "terminal" : transcript(config, id).working ? "working" : "waiting")
                    // Opened and not yet started: it has messages, and the runtime has never heard of it.
                    : conversation && state == states.end() && !std::filesystem::exists(entry.path() / "log") ? "starting" : "stopped",
            ago(id.substr(id.size() - stampLength)),
        });
    }
    std::ranges::sort(result, {}, &Session::id);
    return result;
}

namespace {

// A fresh clone on its own branch, cut off from where it came from, and a
// home. The commits are made as whoever this machine's git says its owner is.
auto prepare(Config const& config, std::string const& project) -> std::expected<std::string, std::string>
{
    if (!is_valid_name(project)) {
        return std::unexpected(std::format("'{}' is not a usable project name", project));
    }
    auto const id = project + "-" + stamp();
    auto const directory = session_directory(config, id);
    auto const tree = directory / "tree";
    auto error = std::error_code{};
    if (!std::filesystem::create_directories(directory, error)) {
        return std::unexpected(std::format("cannot create {}", directory.string()));
    }

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
    if (!prepared) {
        (void)discard(config, id);
        return std::unexpected(prepared.error());
    }
    return id;
}

auto begin(Config const& config, std::string const& id, bool const terminal, std::vector<std::string> const& command)
    -> std::expected<std::string, std::string>
{
    if (auto const started = start(config, id, terminal, command); !started) {
        (void)discard(config, id);
        return std::unexpected(started.error());
    }
    return id;
}

}

auto up(Config const& config, std::string const& project) -> std::expected<std::string, std::string>
{
    if (config.agent.empty()) {
        return std::unexpected("'agent' is not set in the config: bot carries the choice of agent, it does not make it");
    }
    auto const id = prepare(config, project);
    return id ? begin(config, *id, true, config.agent) : id;
}

auto open_session(Config const& config, std::string const& project, std::string const& message)
    -> std::expected<std::string, std::string>
{
    auto const id = prepare(config, project);
    if (!id) {
        return id;
    }
    std::filesystem::create_directories(session_directory(config, *id) / "inbox");
    if (auto const said = say(config, *id, message.empty() ? nextWork : message); !said) {
        (void)discard(config, *id);
        return std::unexpected(said.error());
    }
    return id;
}

auto launch(Config const& config, std::string const& id) -> std::expected<void, std::string>
{
    auto report = std::ostringstream{};
    auto started = std::expected<void, std::string>{};
    if (!check(config, {}, report)) {
        auto failed = std::string{"a session is not contained on this machine, so none was started"};
        for (auto const& line : lines(report.str())) {
            failed += line.starts_with("FAIL") ? "\n" + line : std::string{};
        }
        started = std::unexpected(failed);
    } else {
        // The image's adapter: one turn of its agent for each message in the inbox.
        started = start(config, id, false, {"agent-loop"});
    }
    if (!started) {
        // Why it did not start is the first and last thing in its log.
        teardown(config, id);
        auto log = std::ofstream{session_directory(config, id) / "log"};
        for (auto const& line : lines("\x1e" "error\n" + started.error())) {
            log << "- stdout F " << line << '\n';
        }
    }
    return started;
}

auto run_session(Config const& config, std::string const& project, std::string const& message)
    -> std::expected<std::string, std::string>
{
    auto const id = open_session(config, project, message);
    if (!id) {
        return id;
    }
    if (auto const started = launch(config, *id); !started) {
        (void)discard(config, *id);
        return std::unexpected(started.error());
    }
    return id;
}

auto resolve(Config const& config, std::string const& name) -> std::expected<std::string, std::string>
{
    if (is_session(config, name)) {
        return name;
    }
    auto newest = std::string{};
    for (auto const& session : sessions(config)) {
        newest = session.project == name ? session.id : newest;
    }
    if (newest.empty()) {
        return std::unexpected(std::format("no session named {}, and no session on a project named {}", name, name));
    }
    return newest;
}

auto takes_messages(Config const& config, std::string const& id) -> bool
{
    return is_session(config, id) && std::filesystem::is_directory(session_directory(config, id) / "inbox");
}

namespace {

// Messages are files named 0001, 0002 and so on. The count of them is how
// many have been sent, and nothing else records it.
auto sent(std::filesystem::path const& inbox) -> int
{
    auto count = 0;
    auto error = std::error_code{};
    for (auto const& entry : std::filesystem::directory_iterator{inbox, error}) {
        count += entry.path().filename().string().size() == 4 ? 1 : 0;
    }
    return count;
}

}

auto say(Config const& config, std::string const& id, std::string const& message) -> std::expected<void, std::string>
{
    if (!takes_messages(config, id)) {
        return std::unexpected(std::format("{} is attended in a terminal and takes no messages; `bot attach {}`", id, id));
    }
    if (trimmed(message).empty()) {
        return std::unexpected("there is nothing in that message");
    }
    auto const inbox = session_directory(config, id) / "inbox";
    auto const draft = inbox / "draft";
    std::ofstream{draft} << trimmed(message) << '\n';
    auto error = std::error_code{};
    std::filesystem::rename(draft, inbox / std::format("{:04}", sent(inbox) + 1), error);
    if (error) {
        return std::unexpected(std::format("cannot write to {}: {}", inbox.string(), error.message()));
    }
    return {};
}

// The log is the container's output as the runtime wrote it: a time, a
// stream, whether the line is whole, then the text. The adapter's output is
// records: a separator and a kind on one line, then the text.
auto events_in(std::string_view log) -> std::vector<Event>
{
    auto output = std::string{};
    for (auto position = std::size_t{0}; position < log.size();) {
        auto const end = std::min(log.find('\n', position), log.size());
        auto const line = log.substr(position, end - position);
        position = end + 1;
        auto const stream = line.find(' ');
        auto const tag = stream == std::string_view::npos ? stream : line.find(' ', stream + 1);
        auto const text = tag == std::string_view::npos ? tag : line.find(' ', tag + 1);
        if (text != std::string_view::npos) {
            output += line.substr(text + 1);
            output += line.substr(tag + 1, text - tag - 1) == "F" ? "\n" : "";
        }
    }

    auto events = std::vector<Event>{};
    for (auto position = output.find('\x1e'); position != std::string::npos;) {
        auto const next = output.find('\x1e', position + 1);
        auto const record = output.substr(position + 1, next == std::string::npos ? next : next - position - 1);
        auto const kindEnd = std::min(record.find('\n'), record.size());
        events.push_back(Event{record.substr(0, kindEnd), trimmed(record.substr(std::min(kindEnd + 1, record.size())))});
        position = next;
    }
    return events;
}

auto transcript(Config const& config, std::string const& id) -> Transcript
{
    auto file = std::ifstream{session_directory(config, id) / "log", std::ios::binary};
    auto buffer = std::ostringstream{};
    buffer << file.rdbuf();
    auto result = Transcript{events_in(buffer.str()), false};
    // Every message ends in exactly one `done`, so more sent than done means a turn is under way.
    result.working = sent(session_directory(config, id) / "inbox")
        > std::ranges::count(result.events, "done", &Event::kind);
    return result;
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
    (void)run({config.runtime, "stop", "--time", "2", container(id)});
    (void)run({config.runtime, "rm", "--force", "--time", "0", proxy(id)});
    return publish(config, id, project_of(id));
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
    unreachable.insert(unreachable.end(), config.probe.begin(), config.probe.end());
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

    // Each probe is a script that exits zero when the boundary held. They
    // are run inside the session in one visit, each answering for itself.
    auto probes = std::vector<std::pair<std::string, std::string>>{};
    auto const probe = [&](std::string script, std::string what) { probes.emplace_back(std::move(what), std::move(script)); };
    auto const hidden = [](std::filesystem::path const& path) { return "! test -e " + quoted(path.string()); };
    auto const relay = [](std::string const& host, std::string_view const expected) {
        return std::format("test \"$(curl -s -o /dev/null -m 10 -w '%{{http_connect}}' https://{}/)\" = {}", host, expected);
    };

    probe("test \"$(id -u)\" != 0", "a session is not root");
    probe("grep -Eq '^CapEff:[[:space:]]*0+$' /proc/self/status", "a session holds no capabilities");
    probe("grep -Eq '^[^ ]+ / [^ ]+ ro[, ]' /proc/mounts", "a session's root filesystem is read-only");
    probe("! test -w /inbox", "a session cannot write where its messages arrive");
    probe(hidden(sentinel), "a session cannot see the directory that holds the other sessions");
    probe(hidden(default_config_path()), "a session cannot see this tool's configuration");
    if (!config.agentEnv.empty()) {
        probe(hidden(config.agentEnv), "a session cannot see the file its agent's credential came from");
    }
    if (auto const* home = std::getenv("HOME"); home != nullptr) {
        probe("! ls -A " + quoted(home) + " 2>/dev/null | grep -q .", "a session cannot see the owner's home directory");
    }
    probe("! ls /run/podman /run/docker.sock /var/run/docker.sock /run/user/*/podman 2>/dev/null | grep -q .",
          "a session cannot see a container runtime's socket");
    probe("! grep -Eq '^[^[:space:]]+[[:space:]]+00000000[[:space:]]' /proc/net/route", "a session has no default route");
    probe("! timeout 8 getent hosts example.org", "a session cannot resolve a name itself");
    for (auto const& address : unreachable) {
        probe(std::format("out=$(timeout 4 bash -c ': </dev/tcp/{}/22' 2>&1); test $? -ne 0 && ! printf %s \"$out\" | grep -qi refused", address),
              "a session cannot connect to " + address);
        probe(relay(address.contains(':') ? "[" + address + "]" : address, "403"), "the proxy refuses to relay to " + address);
    }
    if (!std::ranges::contains(config.allow, "example.org")) {
        probe(relay("example.org", "403"), "the proxy refuses a name that is not allowed");
    }
    for (auto const& name : config.allow) {
        probe(relay(name, "200"), "the proxy relays to " + name + ", which is allowed");
    }

    auto script = std::string{};
    for (auto index = std::size_t{0}; index < probes.size(); ++index) {
        script += std::format("( {} ) >/dev/null 2>&1 && echo held {}\n", probes[index].second, index);
    }
    auto const answers = run({config.runtime, "exec", container(id), "sh", "-c", script});
    for (auto index = std::size_t{0}; index < probes.size(); ++index) {
        say(answers && ("\n" + answers->output).contains(std::format("\nheld {}\n", index)), probes[index].first);
    }

    say(discard(config, id).has_value(), "the probe session is removed");
    std::filesystem::remove(sentinel, error);
    return held;
}

}
