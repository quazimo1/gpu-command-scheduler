#include "scheduler.hpp"

#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using gpu_scheduler::Event;
using gpu_scheduler::Job;
using gpu_scheduler::Policy;
using gpu_scheduler::Simulator;

namespace {

std::vector<int> jobs_for(const std::vector<Event>& events, const std::string& type) {
  std::vector<int> jobs;
  for (const auto& event : events)
    if (event.type == type) jobs.push_back(event.job);
  return jobs;
}

void expect(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

void fifo_preserves_submission_order() {
  const std::vector<Job> jobs{{1, 0, 1, 2, 10, false, {}},
                              {2, 1, 3, 1, 10, false, {}},
                              {3, 0, 2, 1, 10, false, {}}};
  const auto result = Simulator(Policy::fifo).run(jobs);
  expect(jobs_for(result.events, "dispatched") == std::vector<int>({1, 2, 3}),
         "FIFO dispatch order");
}

void dependencies_block_until_completion() {
  const std::vector<Job> jobs{{1, 0, 1, 3, 10, false, {2}},
                              {2, 0, 1, 2, 10, false, {}}};
  const auto result = Simulator(Policy::fifo).run(jobs);
  expect(jobs_for(result.events, "dispatched") == std::vector<int>({2, 1}),
         "dependency dispatch order");
}

void priority_selects_highest_first() {
  const std::vector<Job> jobs{{1, 0, 1, 1, 10, false, {}},
                              {2, 1, 4, 1, 10, false, {}},
                              {3, 2, 2, 1, 10, false, {}}};
  const auto result = Simulator(Policy::priority).run(jobs);
  expect(jobs_for(result.events, "dispatched") == std::vector<int>({2, 3, 1}),
         "priority dispatch order");
}

void weighted_round_robin_services_contexts() {
  const std::vector<Job> jobs{{1, 0, 2, 1, 10, false, {}},
                              {2, 0, 2, 1, 10, false, {}},
                              {3, 0, 2, 1, 10, false, {}},
                              {4, 1, 1, 1, 10, false, {}},
                              {5, 1, 1, 1, 10, false, {}}};
  const auto result = Simulator(Policy::weighted_round_robin).run(jobs);
  expect(jobs_for(result.events, "dispatched") == std::vector<int>({1, 2, 4, 3, 5}),
         "weighted round-robin dispatch order");
}

void timeout_cancels_dependents_and_recovers() {
  const std::vector<Job> jobs{{1, 0, 1, 10, 3, true, {}},
                              {2, 0, 1, 1, 10, false, {1}},
                              {3, 1, 1, 2, 10, false, {}}};
  const auto result = Simulator(Policy::fifo).run(jobs);
  expect(result.timed_out == 1 && result.cancelled == 1 && result.completed == 1,
         "timeout accounting");
  expect(jobs_for(result.events, "recovered") == std::vector<int>({1}),
         "engine recovery event");
}

void cycles_are_rejected() {
  const std::vector<Job> jobs{{1, 0, 1, 1, 10, false, {2}},
                              {2, 0, 1, 1, 10, false, {1}}};
  try {
    Simulator(Policy::fifo).run(jobs);
    throw std::runtime_error("cycle was accepted");
  } catch (const std::invalid_argument&) {
  }
}

void deterministic_stress_reaches_a_terminal_state() {
  std::mt19937 generator(7);
  std::vector<Job> jobs;
  for (int id = 1; id <= 100; ++id) {
    jobs.push_back({id,
                    static_cast<int>(generator() % 4),
                    1 + static_cast<int>(generator() % 3),
                    1 + generator() % 8,
                    20,
                    false,
                    {}});
  }
  const auto result = Simulator(Policy::weighted_round_robin).run(jobs);
  expect(result.completed == jobs.size() && result.timed_out == 0 &&
             result.cancelled == 0,
         "stress workload reaches terminal states");
}

}  // namespace

int main() {
  try {
    fifo_preserves_submission_order();
    dependencies_block_until_completion();
    priority_selects_highest_first();
    weighted_round_robin_services_contexts();
    timeout_cancels_dependents_and_recovers();
    cycles_are_rejected();
    deterministic_stress_reaches_a_terminal_state();
    std::cout << "all scheduler tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
