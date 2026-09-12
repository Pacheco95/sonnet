#include <sonnet/core/Log.h>

#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace sonnet::core {

namespace {

// [time] [level] [module] [file:line] message. %s is the base name; the record keeps the full
// path and the function for sinks that want them. %* is the level in the short spelling of
// docs/conventions.md ("warn", not spdlog's "warning").
constexpr const char *LinePattern = "[%H:%M:%S.%e] [%*] [%n] [%s:%#] %v";

class LevelFlag final : public spdlog::custom_flag_formatter {
public:
  void format(const spdlog::details::log_msg &msg, const std::tm &, spdlog::memory_buf_t &dest) override {
    if (msg.level == spdlog::level::warn) {
      constexpr std::string_view name = "warn";
      dest.append(name.data(), name.data() + name.size());
      return;
    }
    const auto name = spdlog::level::to_string_view(msg.level);
    dest.append(name.data(), name.data() + name.size());
  }
  [[nodiscard]] std::unique_ptr<custom_flag_formatter> clone() const override {
    return std::make_unique<LevelFlag>();
  }
};

std::unique_ptr<spdlog::formatter> makeFormatter() {
  auto formatter = std::make_unique<spdlog::pattern_formatter>();
  formatter->add_flag<LevelFlag>('*').set_pattern(LinePattern);
  return formatter;
}

struct Registry {
  std::mutex mutex;
  bool initialised{false};
  std::vector<spdlog::sink_ptr> sinks;
  std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> loggers;
  spdlog::level::level_enum level{spdlog::level::debug};

  static Registry &instance() {
    static Registry s_registry;
    return s_registry;
  }

  void initLocked() {
    if (initialised) {
      return;
    }
    initialised = true;
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sinks.push_back(std::move(console));
    applyLocked();
  }

  void applyLocked() {
    for (auto &[name, logger] : loggers) {
      logger->sinks() = sinks;
      logger->set_formatter(makeFormatter());
      logger->set_level(level);
    }
  }
};

} // namespace

void Log::init() {
  Registry &registry = Registry::instance();
  std::scoped_lock lock(registry.mutex);
  registry.initLocked();
}

void Log::flush() {
  Registry &registry = Registry::instance();
  std::scoped_lock lock(registry.mutex);
  for (auto &[name, logger] : registry.loggers) {
    logger->flush();
  }
}

spdlog::logger &Log::get(std::string_view module) {
  Registry &registry = Registry::instance();
  std::scoped_lock lock(registry.mutex);
  registry.initLocked();
  if (auto it = registry.loggers.find(std::string{module}); it != registry.loggers.end()) {
    return *it->second;
  }
  auto logger = std::make_shared<spdlog::logger>(std::string{module}, registry.sinks.begin(), registry.sinks.end());
  logger->set_formatter(makeFormatter());
  logger->set_level(registry.level);
  // Flushing from debug up keeps lifecycle lines visible when the process dies or is killed with
  // stdout on a pipe, as under CTest. Trace is the only level meant for per-frame volume.
  logger->flush_on(spdlog::level::debug);
  spdlog::logger &ref = *logger;
  registry.loggers.emplace(std::string{module}, std::move(logger));
  return ref;
}

void Log::addSink(spdlog::sink_ptr sink) {
  Registry &registry = Registry::instance();
  std::scoped_lock lock(registry.mutex);
  registry.initLocked();
  registry.sinks.push_back(std::move(sink));
  registry.applyLocked();
}

void Log::removeSink(const spdlog::sink_ptr &sink) {
  Registry &registry = Registry::instance();
  std::scoped_lock lock(registry.mutex);
  std::erase(registry.sinks, sink);
  registry.applyLocked();
}

void Log::setLevel(spdlog::level::level_enum level) {
  Registry &registry = Registry::instance();
  std::scoped_lock lock(registry.mutex);
  registry.level = level;
  registry.applyLocked();
}

} // namespace sonnet::core
