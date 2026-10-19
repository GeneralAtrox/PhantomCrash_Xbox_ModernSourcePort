#include "phantomcrash/module_shop.hpp"
#include "phantomcrash/shop_pointer.hpp"
#include <algorithm>

namespace phantomcrash {
InputFrame ModuleShop::pointer_input(InputFrame input) {
  if (done() || !faded_in()) {
    return input;
  }
  if (view_ == ModuleShopView::list && list_seconds_ >= script_.list.window.seconds) {
    shop_list_pointer(input, script_.list, script_.up, script_.down, script_.cancel_buttons);
    details_pointer(input);
  }
  return input;
}
} // namespace phantomcrash
