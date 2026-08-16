#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

#include <paddock/core/SimulationClock.hpp>

namespace paddock::core {

namespace {

constexpr std::array<int, 12> kDaysInMonth = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

std::string two_digits(int value) {
  const std::string text = std::to_string(value);
  return text.size() < 2 ? "0" + text : text;
}

}  // namespace

bool is_leap_year(int year) noexcept {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int days_in_month(int year, int month) noexcept {
  if (month < 1 || month > 12) {
    return 0;
  }
  if (month == 2 && is_leap_year(year)) {
    return 29;
  }
  return kDaysInMonth[static_cast<std::size_t>(month - 1)];
}

// days_from_civil / civil_from_days, after Howard Hinnant's public-domain
// calendar algorithms. Exact for the whole range of int64 days.
std::int64_t Date::days_since_epoch() const noexcept {
  const std::int64_t adjusted_year = year - static_cast<int>(month <= 2);
  const std::int64_t era = (adjusted_year >= 0 ? adjusted_year : adjusted_year - 399) / 400;
  const std::int64_t year_of_era = adjusted_year - (era * 400);
  const std::int64_t day_of_year = (((153 * (month + (month > 2 ? -3 : 9))) + 2) / 5) + day - 1;
  const std::int64_t day_of_era =
      (year_of_era * 365) + (year_of_era / 4) - (year_of_era / 100) + day_of_year;
  return (era * 146097) + day_of_era - 719468;
}

Date Date::from_days_since_epoch(std::int64_t days) noexcept {
  const std::int64_t shifted = days + 719468;
  const std::int64_t era = (shifted >= 0 ? shifted : shifted - 146096) / 146097;
  const std::int64_t day_of_era = shifted - (era * 146097);
  const std::int64_t year_of_era =
      (day_of_era - (day_of_era / 1460) + (day_of_era / 36524) - (day_of_era / 146096)) / 365;
  const std::int64_t year = year_of_era + (era * 400);
  const std::int64_t day_of_year =
      day_of_era - ((365 * year_of_era) + (year_of_era / 4) - (year_of_era / 100));
  const std::int64_t month_prime = ((5 * day_of_year) + 2) / 153;
  const std::int64_t day = day_of_year - (((153 * month_prime) + 2) / 5) + 1;
  const std::int64_t month = month_prime + (month_prime < 10 ? 3 : -9);
  return Date{static_cast<int>(year + static_cast<std::int64_t>(month <= 2)),
              static_cast<int>(month), static_cast<int>(day)};
}

int Date::day_of_year() const noexcept {
  const Date first_of_year{year, 1, 1};
  return static_cast<int>(days_since_epoch() - first_of_year.days_since_epoch()) + 1;
}

bool Date::is_valid() const noexcept {
  return month >= 1 && month <= 12 && day >= 1 && day <= days_in_month(year, month);
}

std::string Date::to_iso_string() const {
  return std::to_string(year) + '-' + two_digits(month) + '-' + two_digits(day);
}

bool operator==(const Date& lhs, const Date& rhs) noexcept {
  return lhs.year == rhs.year && lhs.month == rhs.month && lhs.day == rhs.day;
}

bool operator!=(const Date& lhs, const Date& rhs) noexcept {
  return !(lhs == rhs);
}

bool operator<(const Date& lhs, const Date& rhs) noexcept {
  return lhs.days_since_epoch() < rhs.days_since_epoch();
}

Season season_of(const Date& date) noexcept {
  switch (date.month) {
    case 12:
    case 1:
    case 2:
      return Season::Summer;
    case 3:
    case 4:
    case 5:
      return Season::Autumn;
    case 6:
    case 7:
    case 8:
      return Season::Winter;
    default:
      return Season::Spring;
  }
}

std::string_view season_name(Season season) noexcept {
  switch (season) {
    case Season::Summer:
      return "summer";
    case Season::Autumn:
      return "autumn";
    case Season::Winter:
      return "winter";
    case Season::Spring:
      return "spring";
  }
  return "unknown";
}

SimulationClock::SimulationClock(Date start) : start_epoch_day_(start.days_since_epoch()) {
  if (!start.is_valid()) {
    throw std::invalid_argument("SimulationClock: invalid start date " + start.to_iso_string());
  }
}

void SimulationClock::advance(int days) {
  if (days < 0) {
    throw std::invalid_argument("SimulationClock: time does not run backwards");
  }
  day_index_ += days;
}

void SimulationClock::reset() {
  day_index_ = 0;
}

Date SimulationClock::date() const noexcept {
  return Date::from_days_since_epoch(start_epoch_day_ + day_index_);
}

Date SimulationClock::start_date() const noexcept {
  return Date::from_days_since_epoch(start_epoch_day_);
}

}  // namespace paddock::core
