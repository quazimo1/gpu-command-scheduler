#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gpu_scheduler {

using Tick = std::uint64_t;

enum class Policy { fifo, priority, weighted_round_robin };

struct Job {
  int id{};
  int context{};
  int priority{1};
  Tick duration{1};
  Tick timeout{100};
  bool hangs{false};
  std::vector<int> dependencies;
};

struct Event {
  Tick time{};
  std::string type;
  int job{};
  int context{};
  std::string detail;
};

struct RunResult {
  std::vector<Event> events;
  Tick elapsed{};
  double mean_queue_wait{};
  Tick p95_queue_wait{};
  std::size_t completed{};
  std::size_t timed_out{};
  std::size_t cancelled{};
};

class Simulator {
 public:
  explicit Simulator(Policy policy);
  RunResult run(const std::vector<Job>& jobs);

 private:
  Policy policy_;
};

Policy parse_policy(const std::string& text);
std::string policy_name(Policy policy);
std::vector<Job> load_workload(const std::string& path);
void write_events(const std::string& path, const std::vector<Event>& events);

}  // namespace gpu_scheduler
