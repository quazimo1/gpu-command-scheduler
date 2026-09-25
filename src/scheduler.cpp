#include "scheduler.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace gpu_scheduler {
namespace {

enum class State { waiting, runnable, completed, timed_out, cancelled };

struct RuntimeJob {
  Job job;
  std::size_t order{};
  State state{State::waiting};
};

std::vector<std::string> split(const std::string& text, char delimiter) {
  std::vector<std::string> parts;
  std::stringstream stream(text);
  std::string part;
  while (std::getline(stream, part, delimiter)) {
    parts.push_back(part);
  }
  if (!text.empty() && text.back() == delimiter) parts.emplace_back();
  return parts;
}

void validate(const std::vector<Job>& jobs) {
  std::set<int> ids;
  for (const auto& job : jobs) {
    if (job.id <= 0 || job.context < 0 || job.priority <= 0 ||
        job.duration == 0 || job.timeout == 0) {
      throw std::invalid_argument("job fields must be positive (context may be zero)");
    }
    if (!ids.insert(job.id).second) {
      throw std::invalid_argument("duplicate job id: " + std::to_string(job.id));
    }
  }

  std::unordered_map<int, const Job*> by_id;
  for (const auto& job : jobs) by_id[job.id] = &job;
  for (const auto& job : jobs) {
    for (int dependency : job.dependencies) {
      if (!by_id.count(dependency)) {
        throw std::invalid_argument("unknown dependency: " + std::to_string(dependency));
      }
      if (dependency == job.id) {
        throw std::invalid_argument("job cannot depend on itself");
      }
    }
  }

  std::unordered_map<int, int> marks;
  const auto visit = [&](const auto& self, int id) -> void {
    if (marks[id] == 1) throw std::invalid_argument("dependency cycle detected");
    if (marks[id] == 2) return;
    marks[id] = 1;
    for (int dependency : by_id.at(id)->dependencies) self(self, dependency);
    marks[id] = 2;
  };
  for (const auto& job : jobs) visit(visit, job.id);
}

bool terminal(State state) {
  return state == State::completed || state == State::timed_out ||
         state == State::cancelled;
}

std::string escape_csv(std::string text) {
  std::size_t position = 0;
  while ((position = text.find('"', position)) != std::string::npos) {
    text.insert(position, 1, '"');
    position += 2;
  }
  return '"' + text + '"';
}

}  // namespace

Simulator::Simulator(Policy policy) : policy_(policy) {}

RunResult Simulator::run(const std::vector<Job>& jobs) {
  validate(jobs);
  RunResult result;
  std::vector<RuntimeJob> runtime;
  runtime.reserve(jobs.size());
  for (std::size_t i = 0; i < jobs.size(); ++i) {
    runtime.push_back({jobs[i], i, State::waiting});
    result.events.push_back({0, "submitted", jobs[i].id, jobs[i].context, ""});
  }

  std::unordered_map<int, State> states;
  for (const auto& item : runtime) states[item.job.id] = State::waiting;

  std::vector<int> contexts;
  std::map<int, int> weights;
  for (const auto& item : runtime) {
    if (!weights.count(item.job.context)) contexts.push_back(item.job.context);
    weights[item.job.context] = std::max(weights[item.job.context], item.job.priority);
  }
  std::sort(contexts.begin(), contexts.end());
  std::map<int, int> credits = weights;
  std::size_t context_cursor = 0;
  std::unordered_map<int, Tick> ready_at;
  std::vector<Tick> queue_waits;
  Tick now = 0;

  const auto choose = [&]() -> RuntimeJob* {
    std::vector<RuntimeJob*> ready;
    for (auto& item : runtime) {
      if (item.state == State::runnable) ready.push_back(&item);
    }
    if (ready.empty()) return nullptr;

    if (policy_ == Policy::fifo) {
      return *std::min_element(ready.begin(), ready.end(), [](auto* a, auto* b) {
        return a->order < b->order;
      });
    }
    if (policy_ == Policy::priority) {
      return *std::min_element(ready.begin(), ready.end(), [](auto* a, auto* b) {
        if (a->job.priority != b->job.priority)
          return a->job.priority > b->job.priority;
        return a->order < b->order;
      });
    }

    for (std::size_t attempts = 0; attempts < contexts.size() * 2; ++attempts) {
      const int context = contexts[context_cursor];
      if (credits[context] == 0) {
        credits[context] = weights[context];
        context_cursor = (context_cursor + 1) % contexts.size();
        continue;
      }
      auto found = std::find_if(ready.begin(), ready.end(), [context](auto* item) {
        return item->job.context == context;
      });
      if (found != ready.end()) {
        --credits[context];
        if (credits[context] == 0)
          context_cursor = (context_cursor + 1) % contexts.size();
        return *found;
      }
      context_cursor = (context_cursor + 1) % contexts.size();
    }
    return ready.front();
  };

  while (std::any_of(runtime.begin(), runtime.end(), [](const auto& item) {
    return !terminal(item.state);
  })) {
    for (auto& item : runtime) {
      if (item.state != State::waiting) continue;
      bool failed_dependency = false;
      bool all_complete = true;
      for (int dependency : item.job.dependencies) {
        const State state = states.at(dependency);
        failed_dependency |= state == State::timed_out || state == State::cancelled;
        all_complete &= state == State::completed;
      }
      if (failed_dependency) {
        item.state = State::cancelled;
        states[item.job.id] = item.state;
        ++result.cancelled;
        result.events.push_back(
            {now, "cancelled", item.job.id, item.job.context, "dependency failed"});
      } else if (all_complete) {
        item.state = State::runnable;
        states[item.job.id] = item.state;
        ready_at[item.job.id] = now;
        result.events.push_back({now, "runnable", item.job.id, item.job.context, ""});
      }
    }

    RuntimeJob* selected = choose();
    if (selected == nullptr) {
      if (std::all_of(runtime.begin(), runtime.end(), [](const auto& item) {
            return terminal(item.state);
          }))
        break;
      throw std::logic_error("no runnable job; dependency state is inconsistent");
    }

    result.events.push_back(
        {now, "dispatched", selected->job.id, selected->job.context, policy_name(policy_)});
    queue_waits.push_back(now - ready_at.at(selected->job.id));
    const bool timeout = selected->job.hangs || selected->job.duration > selected->job.timeout;
    now += timeout ? selected->job.timeout : selected->job.duration;
    if (timeout) {
      selected->state = State::timed_out;
      states[selected->job.id] = selected->state;
      ++result.timed_out;
      result.events.push_back(
          {now, "timed_out", selected->job.id, selected->job.context, "engine reset"});
      ++now;
      result.events.push_back({now, "recovered", selected->job.id, selected->job.context, ""});
    } else {
      selected->state = State::completed;
      states[selected->job.id] = selected->state;
      ++result.completed;
      result.events.push_back({now, "completed", selected->job.id, selected->job.context, ""});
    }
  }

  result.elapsed = now;
  if (!queue_waits.empty()) {
    Tick total = 0;
    for (Tick wait : queue_waits) total += wait;
    result.mean_queue_wait = static_cast<double>(total) / queue_waits.size();
    std::sort(queue_waits.begin(), queue_waits.end());
    const std::size_t p95_index = (queue_waits.size() - 1) * 95 / 100;
    result.p95_queue_wait = queue_waits[p95_index];
  }
  return result;
}

Policy parse_policy(const std::string& text) {
  if (text == "fifo") return Policy::fifo;
  if (text == "priority") return Policy::priority;
  if (text == "wrr") return Policy::weighted_round_robin;
  throw std::invalid_argument("policy must be fifo, priority, or wrr");
}

std::string policy_name(Policy policy) {
  if (policy == Policy::fifo) return "fifo";
  if (policy == Policy::priority) return "priority";
  return "wrr";
}

std::vector<Job> load_workload(const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open workload: " + path);
  std::vector<Job> jobs;
  std::string line;
  std::getline(input, line);  // header
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') continue;
    const auto fields = split(line, ',');
    if (fields.size() != 7) throw std::invalid_argument("expected 7 CSV fields: " + line);
    Job job;
    job.id = std::stoi(fields[0]);
    job.context = std::stoi(fields[1]);
    job.priority = std::stoi(fields[2]);
    job.duration = std::stoull(fields[3]);
    job.timeout = std::stoull(fields[4]);
    job.hangs = fields[5] == "1" || fields[5] == "true";
    if (!fields[6].empty()) {
      for (const auto& dependency : split(fields[6], '|'))
        job.dependencies.push_back(std::stoi(dependency));
    }
    jobs.push_back(job);
  }
  return jobs;
}

void write_events(const std::string& path, const std::vector<Event>& events) {
  std::ofstream output(path);
  if (!output) throw std::runtime_error("cannot write events: " + path);
  output << "time,type,job,context,detail\n";
  for (const auto& event : events) {
    output << event.time << ',' << event.type << ',' << event.job << ',' << event.context
           << ',' << escape_csv(event.detail) << '\n';
  }
}

}  // namespace gpu_scheduler
