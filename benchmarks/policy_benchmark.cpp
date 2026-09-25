#include "scheduler.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace {

using gpu_scheduler::Event;
using gpu_scheduler::Job;
using gpu_scheduler::Policy;
using gpu_scheduler::Tick;

std::vector<Job> make_workload() {
  constexpr int context_count = 8;
  constexpr int jobs_per_context = 50;
  std::vector<Job> jobs;
  std::vector<int> previous(context_count, 0);
  int id = 1;

  for (int round = 0; round < jobs_per_context; ++round) {
    for (int context = 0; context < context_count; ++context) {
      const int priority = 4 - context / 2;
      const Tick duration = 1 + (round * 13 + context * 7) % 8;
      std::vector<int> dependencies;
      if (previous[context] != 0) dependencies.push_back(previous[context]);
      jobs.push_back({id, context, priority, duration, 100, false, dependencies});
      previous[context] = id++;
    }
  }
  return jobs;
}

struct WaitStats {
  double low_priority_mean{};
  Tick low_priority_max{};
  double worst_context_mean{};
};

WaitStats calculate_wait_stats(const std::vector<Event>& events) {
  std::unordered_map<int, Tick> ready_at;
  std::map<int, std::vector<Tick>> waits_by_context;
  for (const auto& event : events) {
    if (event.type == "runnable") {
      ready_at[event.job] = event.time;
    } else if (event.type == "dispatched") {
      waits_by_context[event.context].push_back(event.time - ready_at.at(event.job));
    }
  }

  std::vector<Tick> low_priority_waits;
  double worst_context_mean = 0.0;
  for (const auto& [context, waits] : waits_by_context) {
    const Tick total = std::accumulate(waits.begin(), waits.end(), Tick{0});
    const double mean = static_cast<double>(total) / waits.size();
    worst_context_mean = std::max(worst_context_mean, mean);
    if (context >= 6)
      low_priority_waits.insert(low_priority_waits.end(), waits.begin(), waits.end());
  }

  const Tick low_total =
      std::accumulate(low_priority_waits.begin(), low_priority_waits.end(), Tick{0});
  return {static_cast<double>(low_total) / low_priority_waits.size(),
          *std::max_element(low_priority_waits.begin(), low_priority_waits.end()),
          worst_context_mean};
}

}  // namespace

int main() {
  const auto jobs = make_workload();
  std::cout << "policy,jobs,elapsed_ticks,mean_wait,p95_wait,"
               "low_priority_mean_wait,low_priority_max_wait,worst_context_mean_wait\n";
  for (Policy policy : {Policy::fifo, Policy::priority, Policy::weighted_round_robin}) {
    const auto result = gpu_scheduler::Simulator(policy).run(jobs);
    const auto stats = calculate_wait_stats(result.events);
    std::cout << gpu_scheduler::policy_name(policy) << ',' << jobs.size() << ','
              << result.elapsed << ',' << std::fixed << std::setprecision(2)
              << result.mean_queue_wait << ',' << result.p95_queue_wait << ','
              << stats.low_priority_mean << ',' << stats.low_priority_max << ','
              << stats.worst_context_mean << '\n';
  }
}
