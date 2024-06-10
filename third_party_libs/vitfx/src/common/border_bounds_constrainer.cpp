// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "border_bounds_constrainer.h"
#include "full_interface.h"
#include "load_save.h"
#include "synth_gui_interface.h"

void BorderBoundsConstrainer::checkBounds(Rectangle<int>& bounds, const Rectangle<int>& previous,
                                          const Rectangle<int>& limits,
                                          bool stretching_top, bool stretching_left,
                                          bool stretching_bottom, bool stretching_right) {
  border_.subtractFrom(bounds);
  double aspect_ratio = getFixedAspectRatio();

  ComponentBoundsConstrainer::checkBounds(bounds, previous, limits,
                                          stretching_top, stretching_left,
                                          stretching_bottom, stretching_right);

  Rectangle<int> display_area = Desktop::getInstance().getDisplays().getTotalBounds(true);
  if (gui_) {
    ComponentPeer* peer = gui_->getPeer();
    if (peer)
      peer->getFrameSize().subtractFrom(display_area);
  }

  if (display_area.getWidth() < bounds.getWidth()) {
    int new_width = display_area.getWidth();
    int new_height = std::round(new_width / aspect_ratio);
    bounds.setWidth(new_width);
    bounds.setHeight(new_height);
  }
  if (display_area.getHeight() < bounds.getHeight()) {
    int new_height = display_area.getHeight();
    int new_width = std::round(new_height * aspect_ratio);
    bounds.setWidth(new_width);
    bounds.setHeight(new_height);
  }

  border_.addTo(bounds);
}

void BorderBoundsConstrainer::resizeStart() {
  if (gui_)
    gui_->enableRedoBackground(false);
}

void BorderBoundsConstrainer::resizeEnd() {
  if (gui_) {
    LoadSave::saveWindowSize(gui_->getWidth() / (1.0f * vital::kDefaultWindowWidth));
    gui_->enableRedoBackground(true);
  }
}
