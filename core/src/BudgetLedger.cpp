#include <algorithm>
#include <cmath>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <paddock/core/BudgetLedger.hpp>

namespace paddock::core {

std::string_view budget_name(Budget budget) noexcept {
  switch (budget) {
    case Budget::DryMatter:
      return "dry_matter";
    case Budget::Water:
      return "water";
    case Budget::Nitrogen:
      return "nitrogen";
  }
  return "unknown";
}

std::string_view budget_unit(Budget budget) noexcept {
  switch (budget) {
    case Budget::DryMatter:
      return "kg DM";
    case Budget::Water:
      return "mm";
    case Budget::Nitrogen:
      return "kg N";
  }
  return "";
}

void KahanSum::add(double value) noexcept {
  const double updated = sum_ + value;
  if (std::abs(sum_) >= std::abs(value)) {
    compensation_ += (sum_ - updated) + value;
  } else {
    compensation_ += (value - updated) + sum_;
  }
  sum_ = updated;
}

void KahanSum::reset() noexcept {
  sum_ = 0.0;
  compensation_ = 0.0;
}

std::size_t BudgetLedger::index_of(Budget budget) noexcept {
  return static_cast<std::size_t>(budget);
}

BudgetLedger::BudgetState& BudgetLedger::state(Budget budget) {
  return budgets_[index_of(budget)];
}

const BudgetLedger::BudgetState& BudgetLedger::state(Budget budget) const {
  return budgets_[index_of(budget)];
}

ProcessEntry& BudgetLedger::entry_for(BudgetState& budget_state, std::string_view process) {
  const auto found =
      std::find_if(budget_state.entries.begin(), budget_state.entries.end(),
                   [process](const ProcessEntry& entry) { return entry.process == process; });
  if (found != budget_state.entries.end()) {
    return *found;
  }
  budget_state.entries.push_back(ProcessEntry{std::string(process), 0.0, 0.0});
  return budget_state.entries.back();
}

namespace {

void require_non_negative(std::string_view process, double amount) {
  if (amount < 0.0) {
    throw std::invalid_argument("BudgetLedger: process '" + std::string(process) +
                                "' reported a negative amount; direction is carried by the "
                                "call, not by the sign");
  }
}

}  // namespace

void BudgetLedger::set_opening_stock(Budget budget, double amount) {
  state(budget).opening_stock = amount;
}

void BudgetLedger::record_inflow(Budget budget, std::string_view process, double amount) {
  require_non_negative(process, amount);
  BudgetState& budget_state = state(budget);
  budget_state.inflow.add(amount);
  entry_for(budget_state, process).inflow += amount;
}

void BudgetLedger::record_outflow(Budget budget, std::string_view process, double amount) {
  require_non_negative(process, amount);
  BudgetState& budget_state = state(budget);
  budget_state.outflow.add(amount);
  entry_for(budget_state, process).outflow += amount;
}

void BudgetLedger::record_internal_transfer(Budget budget, std::string_view process,
                                            double amount) {
  require_non_negative(process, amount);
  BudgetState& budget_state = state(budget);
  ProcessEntry& entry = entry_for(budget_state, process);
  entry.inflow += amount;
  entry.outflow += amount;
}

double BudgetLedger::opening_stock(Budget budget) const {
  return state(budget).opening_stock;
}

double BudgetLedger::total_inflow(Budget budget) const {
  return state(budget).inflow.value();
}

double BudgetLedger::total_outflow(Budget budget) const {
  return state(budget).outflow.value();
}

double BudgetLedger::expected_closing_stock(Budget budget) const {
  const BudgetState& budget_state = state(budget);
  return budget_state.opening_stock + budget_state.inflow.value() - budget_state.outflow.value();
}

double BudgetLedger::residual(Budget budget, double observed_closing_stock) const {
  return observed_closing_stock - expected_closing_stock(budget);
}

bool BudgetLedger::closes(Budget budget, double observed_closing_stock, double tolerance) const {
  return std::abs(residual(budget, observed_closing_stock)) <= tolerance;
}

const std::vector<ProcessEntry>& BudgetLedger::entries(Budget budget) const {
  return state(budget).entries;
}

std::string BudgetLedger::report(Budget budget, double observed_closing_stock) const {
  const BudgetState& budget_state = state(budget);
  std::ostringstream out;
  out.precision(12);
  out << budget_name(budget) << " budget (" << budget_unit(budget) << ")\n"
      << "  opening   " << budget_state.opening_stock << '\n'
      << "  inflow    " << budget_state.inflow.value() << '\n'
      << "  outflow   " << budget_state.outflow.value() << '\n'
      << "  expected  " << expected_closing_stock(budget) << '\n'
      << "  observed  " << observed_closing_stock << '\n'
      << "  residual  " << residual(budget, observed_closing_stock) << '\n';
  for (const ProcessEntry& entry : budget_state.entries) {
    out << "    " << entry.process << ": +" << entry.inflow << " -" << entry.outflow << '\n';
  }
  return out.str();
}

void BudgetLedger::reset() {
  for (BudgetState& budget_state : budgets_) {
    budget_state.opening_stock = 0.0;
    budget_state.inflow.reset();
    budget_state.outflow.reset();
    budget_state.entries.clear();
  }
}

}  // namespace paddock::core
