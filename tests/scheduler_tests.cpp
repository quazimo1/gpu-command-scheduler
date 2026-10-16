#include "scheduler.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using gpu_scheduler::Event;
using gpu_scheduler::Job;
using gpu_scheduler::Policy;
using gpu_scheduler::Simulator;
using gpu_scheduler::load_workload;

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

void fifo_orders_by_runnable_time() {
  const std::vector<Job> jobs{{1, 0, 1, 2, 10, false, {}},
                              {2, 1, 3, 1, 10, false, {}},
                              {3, 0, 2, 1, 10, false, {}}};
  const auto result = Simulator(Policy::fifo).run(jobs);
  expect(jobs_for(result.events, "dispatched") == std::vector<int>({1, 2, 3}),
         "FIFO tie-breaks by submission order");

  const std::vector<Job> delayed_jobs{{1, 0, 1, 1, 10, false, {}},
                                      {2, 1, 1, 1, 10, false, {}},
                                      {3, 0, 1, 1, 10, false, {1}},
                                      {4, 1, 1, 1, 10, false, {}}};
  const auto delayed_result = Simulator(Policy::fifo).run(delayed_jobs);
  expect(jobs_for(delayed_result.events, "dispatched") == std::vector<int>({1, 2, 4, 3}),
         "FIFO must preserve the order jobs became runnable");
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

  const std::vector<Job> reversed_chain{{3, 0, 1, 1, 10, false, {2}},
                                        {2, 0, 1, 1, 10, false, {1}},
                                        {1, 0, 1, 10, 3, true, {}}};
  const auto chain_result = Simulator(Policy::fifo).run(reversed_chain);
  expect(chain_result.timed_out == 1 && chain_result.cancelled == 2,
         "transitive timeout cancellation");
  expect(jobs_for(chain_result.events, "recovered") == std::vector<int>({1}),
         "transitive timeout recovery event");
}

void invalid_workloads_are_rejected() {
  const std::vector<Job> jobs{{1, 0, 1, 1, 10, false, {2}},
                              {2, 0, 1, 1, 10, false, {1}}};
  try {
    Simulator(Policy::fifo).run(jobs);
    throw std::runtime_error("cycle was accepted");
  } catch (const std::invalid_argument&) {
  }

  const std::string path = "scheduler-invalid-workload-test.csv";
  const std::string header = "id,context,priority,duration,timeout,hang,dependencies\n";
  for (const auto& contents : {
           std::string("1,0,1,1,10,false,\n"),
           header + "1,0,1,-1,10,false,\n",
           header + "1,0,1,1,10,ture,\n",
           header + "1junk,0,1,1,10,false,\n",
           header + "1,0,1,1,10,false,2junk\n",
       }) {
    std::ofstream output(path);
    output << contents;
    output.close();
    try {
      load_workload(path);
      throw std::runtime_error("invalid CSV workload was accepted");
    } catch (const std::invalid_argument&) {
    }
  }
  std::remove(path.c_str());
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
    fifo_orders_by_runnable_time();
    dependencies_block_until_completion();
    priority_selects_highest_first();
    weighted_round_robin_services_contexts();
    timeout_cancels_dependents_and_recovers();
    invalid_workloads_are_rejected();
    deterministic_stress_reaches_a_terminal_state();
    std::cout << "all scheduler tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
