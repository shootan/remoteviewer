// Unit test for the viewer's layout arithmetic (viewer split refactor Phase 2 / T4): aspect-fit
// letterboxing, the card grid at 96 and 144 DPI, the picker vs. stream-view layout of a client rect,
// the client-point -> video-coordinate mapping (rounding, edges) and the card hit test (gaps,
// scroll rows). Pure functions from viewer_layout_math.hpp; no window.
//
// Build: remote60_viewer_layout_test (CMake). Run: prints "viewer_layout_test: PASS".

#include <cstdint>
#include <cstdio>

#include "viewer_layout_math.hpp"

using namespace remote60::native_poc::viewer;

namespace {

int gFailures = 0;
#define CHECK(cond)                                                                  \
  do {                                                                               \
    if (!(cond)) {                                                                   \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                  \
      ++gFailures;                                                                   \
    }                                                                                \
  } while (0)

int width(const RECT& r) { return r.right - r.left; }
int height(const RECT& r) { return r.bottom - r.top; }
bool empty(const RECT& r) { return width(r) == 0 && height(r) == 0; }

void test_aspect_fit() {
  std::printf("[T4] aspect_fit_rect: wide content letterboxes top/bottom, tall content pillarboxes, zero content fills\n");
  const RECT box = make_rect(0, 0, 1600, 900);
  // 16:9 content in a 16:9 box: fills
  RECT r = aspect_fit_rect(box, 1920, 1080);
  CHECK(r.left == 0 && r.top == 0 && width(r) == 1600 && height(r) == 900);
  // 4:3 content: pillarbox, centered
  r = aspect_fit_rect(box, 1024, 768);
  CHECK(height(r) == 900 && width(r) == 1200 && r.left == 200 && r.top == 0);
  // 21:9 content: letterbox
  r = aspect_fit_rect(box, 2100, 900);
  CHECK(width(r) == 1600 && height(r) == 686 && r.top == 107);
  // zero content: the container itself
  r = aspect_fit_rect(box, 0, 1080);
  CHECK(r.left == box.left && r.right == box.right && r.bottom == box.bottom);
  // offset container
  const RECT off = make_rect(100, 50, 400, 400);
  r = aspect_fit_rect(off, 200, 100);
  CHECK(width(r) == 400 && height(r) == 200 && r.left == 100 && r.top == 150);
}

void test_card_grid() {
  std::printf("[T4] compute_card_grid_at: columns from the preferred card width, rows from the grid height, DPI scaled\n");
  const RECT grid = make_rect(24, 90, 1552, 700);   // 1600 wide window, 24 px margins at 96 dpi
  CardGridMetrics m = compute_card_grid_at(grid, 96);
  CHECK(m.gap == 14);
  CHECK(m.cols == 6);                      // (1552 + 14) / (232 + 14) = 6
  CHECK(m.cardW == (1552 - 5 * 14) / 6);   // 247
  CHECK(m.thumbH == (m.cardW * 10) / 16);
  CHECK(m.cardH == m.thumbH + 30);
  CHECK(m.visibleRows == (700 + 14) / (m.cardH + 14));
  CHECK(m.visibleCards == m.visibleRows * m.cols);
  // at 144 dpi everything scales by 1.5: fewer, larger cards
  CardGridMetrics m2 = compute_card_grid_at(grid, 144);
  CHECK(m2.gap == 21);
  CHECK(m2.cols == 4);                     // (1552 + 21) / (348 + 21) = 4
  CHECK(m2.cardW >= 210);                  // never below the 140-at-96dpi minimum
  CHECK(m2.cardH == m2.thumbH + 45);
  // a tiny grid still yields one column and one row
  CardGridMetrics m3 = compute_card_grid_at(make_rect(0, 0, 10, 10), 96);
  CHECK(m3.cols == 1 && m3.visibleRows == 1 && m3.cardW == 140);
  // slot rects tile left-to-right then top-to-bottom
  const RECT s0 = card_rect_for_slot(grid, m, 0);
  const RECT s1 = card_rect_for_slot(grid, m, 1);
  const RECT s6 = card_rect_for_slot(grid, m, 6);
  CHECK(s0.left == grid.left && s0.top == grid.top);
  CHECK(s1.left == grid.left + m.cardW + m.gap && s1.top == grid.top);
  CHECK(s6.left == grid.left && s6.top == grid.top + m.cardH + m.gap);
}

void test_client_layout() {
  std::printf("[T4] compute_client_layout_at: stream view has only the video rect; the picker owns the window\n");
  const RECT client = make_rect(0, 0, 1600, 900);
  ClientLayout stream = compute_client_layout_at(client, false, 96);
  CHECK(width(stream.videoRect) == 1600 && height(stream.videoRect) == 900);
  CHECK(empty(stream.panelRect) && empty(stream.listRect) && empty(stream.desktopButtonRect) &&
        empty(stream.refreshButtonRect) && empty(stream.toggleButtonRect) && empty(stream.macroButtonRect));
  ClientLayout picker = compute_client_layout_at(client, true, 96);
  CHECK(picker.panelRect.right == 1600 && picker.panelRect.bottom == 900);
  CHECK(empty(picker.toggleButtonRect) && empty(picker.macroButtonRect));   // the ghost buttons stay dead
  // header: Desktop button at the right margin, Refresh to its left, info band on the left
  CHECK(picker.desktopButtonRect.right == 1600 - 24 && width(picker.desktopButtonRect) == 130 && height(picker.desktopButtonRect) == 30);
  CHECK(picker.refreshButtonRect.right == picker.desktopButtonRect.left - 8 && width(picker.refreshButtonRect) == 96);
  CHECK(picker.refreshButtonRect.top == picker.desktopButtonRect.top);
  // width = refresh.left - 2 * margin, placed at x = margin: it ends one margin short of the Refresh button
  CHECK(picker.selectedInfoRect.left == 24 && picker.selectedInfoRect.right == picker.refreshButtonRect.left - 24);
  // grid below the header, footer below the grid, both inside the window
  CHECK(picker.listRect.top == 12 + 56 + 10 && picker.listRect.left == 24 && picker.listRect.right == 1600 - 24);
  CHECK(picker.statsRect.top == picker.listRect.bottom + 6 && height(picker.statsRect) == 36);
  CHECK(picker.statsRect.bottom <= 900);
  // a very short window keeps a minimum grid height
  ClientLayout tiny = compute_client_layout_at(make_rect(0, 0, 500, 120), true, 96);
  CHECK(height(tiny.listRect) == 120);
  // DPI scales the margins
  ClientLayout hi = compute_client_layout_at(client, true, 192);
  CHECK(hi.listRect.left == 48 && height(hi.desktopButtonRect) == 60);
}

void test_point_mapping() {
  std::printf("[T4] map_point_to_video: edges map to 0 and w-1, the centre rounds, outside points clamp\n");
  const RECT content = make_rect(100, 50, 800, 450);   // 1600x900 frame shown at half size
  int32_t vx = 0, vy = 0;
  map_point_to_video(content, 1600, 900, 100, 50, &vx, &vy);
  CHECK(vx == 0 && vy == 0);
  // the far edge: rel = 799 of 800 -> (799 * 1599 + 400) / 800 = 1597 (the last client pixel maps two
  // video pixels short of w-1; the mapping scales rel/videoW, not rel/(videoW-1) -- findings F-16)
  map_point_to_video(content, 1600, 900, 899, 499, &vx, &vy);
  CHECK(vx == 1597 && vy == 897);
  map_point_to_video(content, 1600, 900, 500, 275, &vx, &vy);   // centre
  CHECK(vx == 800 && vy == 450);
  map_point_to_video(content, 1600, 900, 101, 51, &vx, &vy);    // one client pixel = two video pixels
  CHECK(vx == 2 && vy == 2);
  map_point_to_video(content, 1600, 900, 5000, -20, &vx, &vy);  // clamps to the content rect
  CHECK(vx == 1597 && vy == 0);
  // a 1x1 content rect never divides by zero
  map_point_to_video(make_rect(0, 0, 1, 1), 1600, 900, 0, 0, &vx, &vy);
  CHECK(vx == 0 && vy == 0);
}

void test_card_hit_test() {
  std::printf("[T4] card_hit_test: cards by column/row, gaps rejected, scroll rows offset the index\n");
  const RECT list = make_rect(24, 90, 1552, 700);
  const CardGridMetrics m = compute_card_grid_at(list, 96);
  int idx = -1;
  CHECK(card_hit_test(list, m, 0, list.left + 1, list.top + 1, &idx) && idx == 0);
  CHECK(card_hit_test(list, m, 0, list.left + m.cardW + m.gap + 1, list.top + 1, &idx) && idx == 1);
  CHECK(card_hit_test(list, m, 0, list.left + 1, list.top + m.cardH + m.gap + 1, &idx) && idx == m.cols);
  // the gap between the first two cards
  CHECK(!card_hit_test(list, m, 0, list.left + m.cardW + 1, list.top + 1, &idx));
  // the gap below the first row
  CHECK(!card_hit_test(list, m, 0, list.left + 1, list.top + m.cardH + 1, &idx));
  // outside the grid
  CHECK(!card_hit_test(list, m, 0, list.left - 1, list.top + 1, &idx));
  CHECK(!card_hit_test(list, m, 0, list.right + 5, list.top + 1, &idx));
  // scrolled two rows: the top-left card is index 2 * cols
  CHECK(card_hit_test(list, m, 2, list.left + 1, list.top + 1, &idx) && idx == 2 * m.cols);
  // beyond the visible rows
  CHECK(!card_hit_test(list, m, 0, list.left + 1, list.top + m.visibleRows * (m.cardH + m.gap) + 1, &idx));
}

}  // namespace

int main() {
  test_aspect_fit();
  test_card_grid();
  test_client_layout();
  test_point_mapping();
  test_card_hit_test();
  if (gFailures != 0) {
    std::printf("viewer_layout_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("viewer_layout_test: PASS\n");
  return 0;
}
