// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#ifndef PADDOCK_TESTS_SUPPORT_VALUE_OF_HPP
#define PADDOCK_TESTS_SUPPORT_VALUE_OF_HPP

#include <optional>
#include <stdexcept>
#include <string>

namespace paddock::tests {

/// The value inside an optional a test has already asserted is there.
///
/// **This exists because a gtest assertion is invisible to static analysis.**
/// The tests are full of
///
///     ASSERT_TRUE(bundle.management.has_value());
///     run_managed_scenario(bundle, *bundle.management, ...);
///
/// which is correct - ASSERT_TRUE returns from the test on failure, so the
/// dereference below it cannot run on an empty optional. clang-tidy's
/// `bugprone-unchecked-optional-access` cannot see that, because the return is
/// inside a macro expansion its dataflow analysis does not follow, and it
/// reported thirty-two of these across the suite.
///
/// **`.value()` does not satisfy it either**, which is worth writing down
/// because it is the obvious first guess and it is wrong: the check flags a
/// `value()` call on an optional it cannot prove is engaged, exactly as it
/// flags `operator*`. The only thing it accepts is a guard it can follow, and
/// that is what this is - one explicit `if` in one place, in a function small
/// enough that the analysis reaches the end of it.
///
/// It also improves what happens when the assumption is wrong. `*empty` is
/// undefined behaviour and may do anything at all; this throws, and gtest turns
/// a throw into a reported failure naming which optional was empty.
template <typename T>
const T& value_of(const std::optional<T>& maybe, const char* what) {
  if (!maybe.has_value()) {
    throw std::logic_error(std::string("this test needs ") + what +
                           ", and the bundle does not have one");
  }
  return *maybe;
}

/// The same, for a test that edits what it found - one that breaks a bundle on
/// purpose to prove the bundle is checked.
template <typename T>
T& value_of(std::optional<T>& maybe, const char* what) {
  if (!maybe.has_value()) {
    throw std::logic_error(std::string("this test needs ") + what +
                           ", and the bundle does not have one");
  }
  return *maybe;
}

}  // namespace paddock::tests

#endif  // PADDOCK_TESTS_SUPPORT_VALUE_OF_HPP
