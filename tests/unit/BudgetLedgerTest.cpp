#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>
#include <string>

#include <paddock/core/BudgetLedger.hpp>

namespace paddock::core {
namespace {

TEST(KahanSumTest, CompensatedSummationSurvivesMixedMagnitudes) {
  KahanSum compensated;
  double naive = 1.0e9;
  compensated.add(1.0e9);

  // A year of small daily flows on top of a large opening stock: exactly the
  // shape of a pasture cover budget, and exactly where naive summation drifts.
  for (int day = 0; day < 365; ++day) {
    compensated.add(1.0e-7);
    naive += 1.0e-7;
  }

  const double expected = 1.0e9 + (365.0 * 1.0e-7);
  EXPECT_NEAR(compensated.value(), expected, kConservationTolerance);
  EXPECT_GT(std::abs(naive - expected), 0.0);
}

TEST(BudgetLedgerTest, ClosingStockIsOpeningPlusInflowsMinusOutflows) {
  BudgetLedger ledger;
  ledger.set_opening_stock(Budget::DryMatter, 2400.0);
  ledger.record_inflow(Budget::DryMatter, "growth", 55.0);
  ledger.record_outflow(Budget::DryMatter, "intake", 30.0);
  ledger.record_outflow(Budget::DryMatter, "senescence", 5.0);

  EXPECT_DOUBLE_EQ(ledger.opening_stock(Budget::DryMatter), 2400.0);
  EXPECT_DOUBLE_EQ(ledger.total_inflow(Budget::DryMatter), 55.0);
  EXPECT_DOUBLE_EQ(ledger.total_outflow(Budget::DryMatter), 35.0);
  EXPECT_DOUBLE_EQ(ledger.expected_closing_stock(Budget::DryMatter), 2420.0);
  EXPECT_TRUE(ledger.closes(Budget::DryMatter, 2420.0));
  EXPECT_DOUBLE_EQ(ledger.residual(Budget::DryMatter, 2420.0), 0.0);
}

TEST(BudgetLedgerTest, AGapWiderThanToleranceDoesNotClose) {
  BudgetLedger ledger;
  ledger.set_opening_stock(Budget::Water, 100.0);
  ledger.record_inflow(Budget::Water, "rainfall", 12.5);
  ledger.record_outflow(Budget::Water, "drainage", 2.5);

  EXPECT_TRUE(ledger.closes(Budget::Water, 110.0));
  EXPECT_TRUE(ledger.closes(Budget::Water, 110.0 + 1e-10));
  EXPECT_FALSE(ledger.closes(Budget::Water, 110.0 + 1e-6));
  EXPECT_NEAR(ledger.residual(Budget::Water, 110.5), 0.5, 1e-12);
}

TEST(BudgetLedgerTest, BudgetsAreIndependent) {
  BudgetLedger ledger;
  ledger.record_inflow(Budget::Nitrogen, "fixation", 4.0);
  ledger.record_inflow(Budget::Water, "rainfall", 20.0);

  EXPECT_DOUBLE_EQ(ledger.total_inflow(Budget::Nitrogen), 4.0);
  EXPECT_DOUBLE_EQ(ledger.total_inflow(Budget::Water), 20.0);
  EXPECT_DOUBLE_EQ(ledger.total_inflow(Budget::DryMatter), 0.0);
}

TEST(BudgetLedgerTest, DirectionIsCarriedByTheCallNotTheSign) {
  BudgetLedger ledger;

  EXPECT_THROW(ledger.record_inflow(Budget::DryMatter, "growth", -1.0), std::invalid_argument);
  EXPECT_THROW(ledger.record_outflow(Budget::Water, "drainage", -1.0), std::invalid_argument);
}

TEST(BudgetLedgerTest, InternalTransfersLeaveTheTotalUnchanged) {
  BudgetLedger ledger;
  ledger.set_opening_stock(Budget::DryMatter, 1000.0);
  ledger.record_internal_transfer(Budget::DryMatter, "grazing", 40.0);

  EXPECT_DOUBLE_EQ(ledger.expected_closing_stock(Budget::DryMatter), 1000.0);
  ASSERT_EQ(ledger.entries(Budget::DryMatter).size(), 1U);
  EXPECT_EQ(ledger.entries(Budget::DryMatter).front().process, "grazing");
  EXPECT_DOUBLE_EQ(ledger.entries(Budget::DryMatter).front().inflow, 40.0);
  EXPECT_DOUBLE_EQ(ledger.entries(Budget::DryMatter).front().outflow, 40.0);
}

TEST(BudgetLedgerTest, EntriesAccumulatePerProcessInReportingOrder) {
  BudgetLedger ledger;
  ledger.record_inflow(Budget::DryMatter, "growth", 10.0);
  ledger.record_outflow(Budget::DryMatter, "intake", 4.0);
  ledger.record_inflow(Budget::DryMatter, "growth", 6.0);

  ASSERT_EQ(ledger.entries(Budget::DryMatter).size(), 2U);
  EXPECT_EQ(ledger.entries(Budget::DryMatter)[0].process, "growth");
  EXPECT_DOUBLE_EQ(ledger.entries(Budget::DryMatter)[0].inflow, 16.0);
  EXPECT_EQ(ledger.entries(Budget::DryMatter)[1].process, "intake");
}

TEST(BudgetLedgerTest, ReportNamesTheProcessesAndTheResidual) {
  BudgetLedger ledger;
  ledger.set_opening_stock(Budget::Nitrogen, 20.0);
  ledger.record_outflow(Budget::Nitrogen, "leaching", 3.0);

  const std::string report = ledger.report(Budget::Nitrogen, 16.0);

  EXPECT_NE(report.find("nitrogen"), std::string::npos);
  EXPECT_NE(report.find("kg N"), std::string::npos);
  EXPECT_NE(report.find("leaching"), std::string::npos);
  EXPECT_NE(report.find("residual"), std::string::npos);
}

TEST(BudgetLedgerTest, ResetClearsEveryBudget) {
  BudgetLedger ledger;
  ledger.set_opening_stock(Budget::Water, 80.0);
  ledger.record_inflow(Budget::Water, "rainfall", 10.0);

  ledger.reset();

  EXPECT_DOUBLE_EQ(ledger.opening_stock(Budget::Water), 0.0);
  EXPECT_DOUBLE_EQ(ledger.total_inflow(Budget::Water), 0.0);
  EXPECT_TRUE(ledger.entries(Budget::Water).empty());
}

}  // namespace
}  // namespace paddock::core
