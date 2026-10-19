#include "phantomcrash/tune_shop.hpp"
#include "phantomcrash/shop_pointer.hpp"
#include <algorithm>

namespace phantomcrash {
InputFrame TuneShop::pointer_input(InputFrame input) {
  if (done() || !faded_in()) {
    return input;
  }
  if (view_ == TuneShopView::list && list_seconds_ >= script_.list.window.seconds) {
    shop_list_pointer(input, script_.list, script_.up, script_.down, script_.cancel_buttons);
  }
  if (view_ == TuneShopView::meter && meter_seconds_ >= script_.meter.window.seconds &&
      shop_clicked(input, script_.meter.cancel_hint)) {
    input.pressed |= script_.cancel_buttons;
  }
  if (view_ == TuneShopView::meter && meter_seconds_ >= script_.meter.window.seconds &&
      shop_clicked(input, script_.meter.confirm_hint)) {
    input.pressed |= script_.confirm_buttons;
  }
  return input;
}
} // namespace phantomcrash
