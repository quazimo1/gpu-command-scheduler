#include "scheduler.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "usage: gpu-scheduler <fifo|priority|wrr> <workload.csv> <events.csv>\n";
    return 2;
  }
  try {
    const auto policy = gpu_scheduler::parse_policy(argv[1]);
    const auto jobs = gpu_scheduler::load_workload(argv[2]);
    const auto result = gpu_scheduler::Simulator(policy).run(jobs);
    gpu_scheduler::write_events(argv[3], result.events);
    std::cout << "policy=" << gpu_scheduler::policy_name(policy)
              << " jobs=" << jobs.size() << " completed=" << result.completed
              << " timed_out=" << result.timed_out << " cancelled=" << result.cancelled
              << " elapsed_ticks=" << result.elapsed
              << " mean_queue_wait=" << result.mean_queue_wait
              << " p95_queue_wait=" << result.p95_queue_wait << '\n';
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
