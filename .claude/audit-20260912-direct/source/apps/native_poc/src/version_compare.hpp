#pragma once

// Version comparison -- the C++ side of a contract three runtimes have to agree on.
//
// "Is the manifest newer than what is installed" gets asked in three places: here (installer and
// updater), in the directory server's JavaScript, and in the Android client's Kotlin. A header
// cannot cross those boundaries, so the agreement lives in a data file --
// apps/shared/version_compare_vectors.txt -- which all three test suites read. This header is
// only the C++ implementation of that contract; the contract itself, and the reasoning behind
// each rule, is written at the top of the vectors file. The design note is section 2(d) of the
// update-feature design document under docs/.
//
// Extracted from installer_main.cpp, where it was file-local with no test at all. The behaviour
// is preserved except for one deliberate change: a component now saturates instead of wrapping
// (rule 6), because three runtimes cannot be made to overflow the same way and the inputs that
// reach it cannot occur in a real version string.
//
// Kept ASCII-only on purpose: the installer compiles without /utf-8 and MSVC warns (C4819) on
// source it cannot represent in the system code page.

#include <cstdint>
#include <string>
#include <string_view>

namespace remote60::native_poc {

// The ceiling a single component saturates at. Chosen to fit every runtime's safe integer range.
inline constexpr uint32_t kVersionComponentMax = 2147483647u;

namespace detail {

template <typename CharT>
int compare_versions_impl(std::basic_string_view<CharT> a, std::basic_string_view<CharT> b) {
  const auto isDigit = [](CharT c) {
    return c >= static_cast<CharT>('0') && c <= static_cast<CharT>('9');
  };
  const auto accumulate = [](uint32_t value, CharT c) {
    const uint32_t digit = static_cast<uint32_t>(c - static_cast<CharT>('0'));
    // Saturate rather than wrap (rule 6). Once at the ceiling it stays there, so the remaining
    // digits are consumed without changing the value.
    if (value > (kVersionComponentMax - digit) / 10u) return kVersionComponentMax;
    return value * 10u + digit;
  };

  size_t i = 0;
  size_t j = 0;
  while (i < a.size() || j < b.size()) {
    // A run of digits is one component. No digits at this position -- end of string, a separator
    // straight away, or a non-numeric character -- reads as zero, which is what makes "0.1" and
    // "0.1.0" compare equal (rule 3) and ".1" equal "0.1".
    uint32_t left = 0;
    while (i < a.size() && isDigit(a[i])) left = accumulate(left, a[i++]);
    uint32_t right = 0;
    while (j < b.size() && isDigit(b[j])) right = accumulate(right, b[j++]);
    if (left != right) return left < right ? -1 : 1;

    if (i < a.size() && a[i] == static_cast<CharT>('.')) ++i;
    if (j < b.size() && b[j] == static_cast<CharT>('.')) ++j;

    // Anything that is not a digit or a separator ends the comparison (rule 5): a suffix like
    // "-beta" or "+build3" can never decide the outcome, in either direction.
    if ((i < a.size() && !isDigit(a[i])) || (j < b.size() && !isDigit(b[j]))) break;
  }
  return 0;
}

}  // namespace detail

// Two overloads rather than one template, so a plain string literal works at the call site:
// template argument deduction does not apply the conversion from const wchar_t[N] to a view, and
// the installer compares against kProductVersion, which is exactly that.

/** Negative when a is older, 0 when equal, positive when a is newer. */
inline int compare_versions(std::wstring_view a, std::wstring_view b) {
  return detail::compare_versions_impl<wchar_t>(a, b);
}

/** Narrow overload, for a manifest version that arrived as UTF-8. */
inline int compare_versions(std::string_view a, std::string_view b) {
  return detail::compare_versions_impl<char>(a, b);
}

}  // namespace remote60::native_poc
