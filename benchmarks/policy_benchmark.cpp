#include "scheduler.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
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

Tick max_low_priority_wait(const std::vector<Event>& events) {
  std::unordered_map<int, Tick> ready_at;
  Tick maximum = 0;
  for (const auto& event : events) {
    if (event.type == "runnable") {
      ready_at[event.job] = event.time;
    } else if (event.type == "dispatched" && event.context >= 6) {
      maximum = std::max(maximum, event.time - ready_at.at(event.job));
    }
  }
  return maximum;
}

}  // namespace

int main() {
  const auto jobs = make_workload();
  std::cout << "policy,jobs,elapsed_ticks,mean_wait,p95_wait,low_priority_max_wait\n";
  for (Policy policy : {Policy::fifo, Policy::priority, Policy::weighted_round_robin}) {
    const auto result = gpu_scheduler::Simulator(policy).run(jobs);
    std::cout << gpu_scheduler::policy_name(policy) << ',' << jobs.size() << ','
              << result.elapsed << ',' << std::fixed << std::setprecision(2)
              << result.mean_queue_wait << ',' << result.p95_queue_wait << ','
              << max_low_priority_wait(result.events) << '\n';
  }
}
